// SoundplaneDriver.cpp
//
// Returns raw data frames from the Soundplane.  The frames are reclocked if needed (TODO)
// to reconstruct a steady sample rate.
//
// Two threads are used to do this work.  A grab thread maintains a stream of
// low-latency isochronous transfers. A process thread looks through the buffers
// specified by these transfers every ms or so.  When new frames of data arrive,
// the process thread reclocks them and pushes them to a ring buffer where they
// can be read by clients.

#include "SoundplaneDriverLibusb.h"

#include <unistd.h>

static constexpr int kInterfaceNumber = 0;

std::unique_ptr<SoundplaneDriver> SoundplaneDriver::create(SoundplaneDriverListener *listener)
{
    auto *driver = new SoundplaneDriverLibusb(listener);
    driver->init();
    return std::unique_ptr<SoundplaneDriverLibusb>(driver);
}


SoundplaneDriverLibusb::SoundplaneDriverLibusb(SoundplaneDriverListener* listener) :
    mState(kNoDevice),
    mQuitting(false),
    mListener(listener)
{
}

SoundplaneDriverLibusb::~SoundplaneDriverLibusb()
{
	// This causes getDeviceState to return kDeviceIsTerminating
	mQuitting.store(true, std::memory_order_release);
	emitDeviceStateChanged(kDeviceIsTerminating);
	mCondition.notify_one();
	mProcessThread.join();
	libusb_exit(mLibusbContext);
}

void SoundplaneDriverLibusb::init()
{
	if (libusb_init(&mLibusbContext) < 0) {
		throw new std::runtime_error("Failed to initialize libusb");
	}

	// create device grab thread
	mProcessThread = std::thread(&SoundplaneDriverLibusb::processThread, this);
}

int SoundplaneDriverLibusb::readSurface(float* pDest)
{
	return 0;
}

void SoundplaneDriverLibusb::flushOutputBuffer()
{
}

MLSoundplaneState SoundplaneDriverLibusb::getDeviceState() const
{
	return mQuitting.load(std::memory_order_acquire) ?
		kDeviceIsTerminating :
		mState.load(std::memory_order_acquire);
}

UInt16 SoundplaneDriverLibusb::getFirmwareVersion() const
{
	return mFirmwareVersion.load(std::memory_order_acquire);
}

std::string SoundplaneDriverLibusb::getSerialNumberString() const
{
	const std::array<unsigned char, 64> serialNumber = mSerialNumber.load();
	return std::string(reinterpret_cast<const char *>(serialNumber.data()));
}

const unsigned char *SoundplaneDriverLibusb::getCarriers() const
{
	// FIXME: Not implemented
	return mCurrentCarriers;
}

int SoundplaneDriverLibusb::setCarriers(const unsigned char *carriers)
{
	// FIXME: Not implemented
	return 0;
}

int SoundplaneDriverLibusb::enableCarriers(unsigned long mask)
{
	// FIXME: Not implemented
	return 0;
}

void SoundplaneDriverLibusb::emitDeviceStateChanged(MLSoundplaneState newState) const
{
	if (mListener) {
		mListener->deviceStateChanged(newState);
	}
}

bool SoundplaneDriverLibusb::processThreadWait(int ms) const
{
	std::unique_lock<std::mutex> lock(mMutex);
	mCondition.wait_for(lock, std::chrono::milliseconds(ms));
	return !mQuitting.load(std::memory_order_acquire);
}

bool SoundplaneDriverLibusb::processThreadOpenDevice(LibusbClaimedDevice &outDevice) const
{
	for (;;)
	{
		libusb_device_handle* handle = libusb_open_device_with_vid_pid(
			mLibusbContext, kSoundplaneUSBVendor, kSoundplaneUSBProduct);
		LibusbClaimedDevice result(LibusbDevice(handle), kInterfaceNumber);
		if (result)
		{
			std::swap(result, outDevice);
			return true;
		}
		if (!processThreadWait(1000))
		{
			return false;
		}
	}
}

bool SoundplaneDriverLibusb::processThreadGetDeviceInfo(libusb_device_handle *device)
{
	libusb_device_descriptor descriptor;
	if (libusb_get_device_descriptor(libusb_get_device(device), &descriptor) < 0) {
		fprintf(stderr, "Failed to get the device descriptor\n");
		return false;
	}

	std::array<unsigned char, 64> buffer;
	int len = libusb_get_string_descriptor_ascii(device, descriptor.iSerialNumber, buffer.data(), buffer.size());
	if (len < 0) {
		fprintf(stderr, "Failed to get the device serial number\n");
		return false;
	}
	buffer[len] = 0;

	mFirmwareVersion.store(descriptor.bcdDevice, std::memory_order_release);
	mSerialNumber = buffer;

	return true;
}

bool SoundplaneDriverLibusb::processThreadFillTransferInformation(
	Transfers &transfers, libusb_device_handle *device)
{
	libusb_config_descriptor *descriptor;
	if (libusb_get_active_config_descriptor(libusb_get_device(device), &descriptor) < 0) {
		fprintf(stderr, "Failed to get device debug information\n");
		return false;
	}

	printf("Available Bus Power: %d mA\n", 2 * static_cast<int>(descriptor->MaxPower));

	if (descriptor->bNumInterfaces <= kInterfaceNumber) {
		fprintf(stderr, "No available interfaces: %d\n", descriptor->bNumInterfaces);
		return false;
	}
	const struct libusb_interface& interface = descriptor->interface[kInterfaceNumber];
	if (interface.num_altsetting <= kSoundplaneAlternateSetting) {
		fprintf(stderr, "Desired alt setting %d is not available\n", kSoundplaneAlternateSetting);
		return false;
	}
	const struct libusb_interface_descriptor& interfaceDescriptor = interface.altsetting[kSoundplaneAlternateSetting];
	if (interfaceDescriptor.bNumEndpoints < kSoundplaneANumEndpoints) {
		fprintf(
			stderr,
			"Alt setting %d has too few endpoints (has %d, needs %d)\n",
			kSoundplaneAlternateSetting,
			interfaceDescriptor.bNumEndpoints,
			kSoundplaneANumEndpoints);
		return false;
	}
	for (int i = 0; i < kSoundplaneANumEndpoints; i++) {
		const struct libusb_endpoint_descriptor& endpoint = interfaceDescriptor.endpoint[i];
		for (auto &transfer : transfers[i])
		{
			transfer.endpointAddress = endpoint.bEndpointAddress;
			transfer.parent = this;
		}
	}

	return true;
}

bool SoundplaneDriverLibusb::processThreadSetDeviceState(MLSoundplaneState newState)
{
	mState.store(newState, std::memory_order_release);
	if (mQuitting.load(std::memory_order_acquire)) {
		return false;
	} else {
		emitDeviceStateChanged(newState);
		return true;
	}
}

bool SoundplaneDriverLibusb::processThreadSelectIsochronousInterface(libusb_device_handle *device) const
{
	if (libusb_set_interface_alt_setting(device, kInterfaceNumber, kSoundplaneAlternateSetting) < 0) {
		fprintf(stderr, "Failed to select alternate setting on the Soundplane\n");
		return false;
	}
	return true;
}

bool SoundplaneDriverLibusb::processThreadScheduleTransfer(
	Transfer &transfer,
	libusb_device_handle *device) const
{
	libusb_fill_iso_transfer(
		transfer.transfer,
		device,
		transfer.endpointAddress,
		transfer.buffer,
		sizeof(transfer.buffer),
		transfer.numPackets(),
		processThreadTransferCallbackStatic,
		&transfer,
		200);
	libusb_set_iso_packet_lengths(
		transfer.transfer,
		sizeof(transfer.buffer) / transfer.numPackets());

	const auto result = libusb_submit_transfer(transfer.transfer);
	if (result < 0)
	{
		fprintf(stderr, "Failed to submit USB transfer: %s\n", libusb_error_name(result));
		return false;
	}
	return true;
}

bool SoundplaneDriverLibusb::processThreadScheduleInitialTransfers(
	Transfers &transfers,
	libusb_device_handle *device) const
{
	for (int endpoint = 0; endpoint < kSoundplaneANumEndpoints; endpoint++)
	{
		for (int buffer = 0; buffer < kSoundplaneABuffers; buffer++)
		{
			auto &transfer = transfers[endpoint][buffer];
			if (!processThreadScheduleTransfer(transfer, device)) {
				return false;
			}
		}
	}
	return true;
}

void SoundplaneDriverLibusb::processThreadTransferCallbackStatic(struct libusb_transfer *xfr)
{
	Transfer *transfer = static_cast<Transfer*>(xfr->user_data);
	transfer->parent->processThreadTransferCallback(*transfer);
}

void SoundplaneDriverLibusb::processThreadTransferCallback(Transfer &transfer)
{
	printf("Transfer\n");

	// Schedule another transfer
	if (!processThreadScheduleTransfer(transfer, transfer.transfer->dev_handle)) {
		mUsbFailed = true;
	}
}

void SoundplaneDriverLibusb::processThread() {
	// Each iteration of this loop is one cycle of finding a Soundplane device,
	// using it, and the device going away.
	while (!mQuitting.load(std::memory_order_acquire))
	{
		mUsbFailed = false;
		Transfers transfers;
		LibusbClaimedDevice handle;

		bool success =
			processThreadOpenDevice(handle) &&
			processThreadGetDeviceInfo(handle.get()) &&
			processThreadSelectIsochronousInterface(handle.get()) &&
			processThreadFillTransferInformation(transfers, handle.get()) &&
			processThreadSetDeviceState(kDeviceConnected) &&
			processThreadScheduleInitialTransfers(transfers, handle.get());

		if (!success) continue;

		/// Run the main event loop
		while (!mQuitting.load(std::memory_order_acquire)) {
			if (libusb_handle_events(mLibusbContext) != LIBUSB_SUCCESS)
			{
				fprintf(stderr, "Libusb error!\n");
				break;
			}
			if (mUsbFailed)
			{
				break;
			}
		}

		if (!processThreadSetDeviceState(kNoDevice)) continue;
	}
}
