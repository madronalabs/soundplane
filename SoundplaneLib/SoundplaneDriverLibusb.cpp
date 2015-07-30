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

namespace
{

constexpr int kInterfaceNumber = 0;

using GotFrameCallback = std::function<void (const SoundplaneOutputFrame& frame)>;

template<typename GlitchCallback>
GotFrameCallback makeAnomalyFilter(
	const GlitchCallback &glitchCallback,
	const GotFrameCallback &successCallback)
{
	int startupCtr = 0;
	SoundplaneOutputFrame previousFrame;
	return [startupCtr, previousFrame, glitchCallback, successCallback](
		const SoundplaneOutputFrame& frame) mutable
	{
		if (startupCtr > kSoundplaneStartupFrames)
		{
			float df = frameDiff(previousFrame, frame);
			if (df < kMaxFrameDiff)
			{
				// We are OK, the data gets out normally
				successCallback(frame);
			}
			else
			{
				// Possible sensor glitch.  also occurs when changing carriers.
				glitchCallback(startupCtr, df, previousFrame, frame);
				startupCtr = 0;
			}
		}
		else
		{
			// Wait for initialization
			startupCtr++;
		}

		previousFrame = frame;
	};
}

}

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
	PaUtil_InitializeRingBuffer(&mOutputBuf, sizeof(mpOutputData) / kSoundplaneOutputBufFrames, kSoundplaneOutputBufFrames, mpOutputData);
}

SoundplaneDriverLibusb::~SoundplaneDriverLibusb()
{
	// This causes getDeviceState to return kDeviceIsTerminating
	mQuitting.store(true, std::memory_order_release);
	mCondition.notify_one();
	mProcessThread.join();

	delete mEnableCarriersRequest.load(std::memory_order_acquire);
	delete mSetCarriersRequest.load(std::memory_order_acquire);

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
	// FIXME: Change to a push based interface
	int result = 0;
	if(PaUtil_GetRingBufferReadAvailable(&mOutputBuf) >= 1)
	{
		result = PaUtil_ReadRingBuffer(&mOutputBuf, pDest, 1);
	}
	return result;
}

void SoundplaneDriverLibusb::flushOutputBuffer()
{
	PaUtil_FlushRingBuffer(&mOutputBuf);
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
	// FIXME: Change this interface to specify the size of the data
	return mCurrentCarriers.data();
}

int SoundplaneDriverLibusb::setCarriers(const unsigned char *carriers)
{
	// FIXME: Change this interface to specify the size of the data
	// FIXME: Change this interface to return void

	auto * const sentCarriers = new Carriers;
	std::copy(carriers, carriers + sentCarriers->size(), sentCarriers->begin());
	std::copy(carriers, carriers + sentCarriers->size(), mCurrentCarriers.begin());
	delete mSetCarriersRequest.exchange(sentCarriers, std::memory_order_release);
	return 0;
}

int SoundplaneDriverLibusb::enableCarriers(unsigned long mask)
{
	// FIXME: Change this interface to return void
	delete mEnableCarriersRequest.exchange(
		new unsigned long(mask), std::memory_order_release);
	return 0;
}

libusb_error SoundplaneDriverLibusb::sendControl(
	libusb_device_handle *device,
	uint8_t request,
	uint16_t value,
	uint16_t index,
	const unsigned char *data,
	size_t dataSize,
	libusb_transfer_cb_fn cb,
	void *userData)
{
	unsigned char *buf = static_cast<unsigned char*>(malloc(LIBUSB_CONTROL_SETUP_SIZE + dataSize));
	struct libusb_transfer *transfer;

	if (!buf)
	{
		return LIBUSB_ERROR_NO_MEM;
	}

	memcpy(buf + LIBUSB_CONTROL_SETUP_SIZE, data, dataSize);

	transfer = libusb_alloc_transfer(0);
	if (!transfer)
	{
		free(buf);
		return LIBUSB_ERROR_NO_MEM;
	}

	static constexpr auto kCtrlOut = LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT;
	libusb_fill_control_setup(buf, kCtrlOut, request, value, index, dataSize);
	libusb_fill_control_transfer(transfer, device, buf, cb, userData, 1000);

	transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK
		| LIBUSB_TRANSFER_FREE_BUFFER
		| LIBUSB_TRANSFER_FREE_TRANSFER;
	return static_cast<libusb_error>(libusb_submit_transfer(transfer));
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
	Transfers &transfers,
	LibusbUnpacker *unpacker,
	libusb_device_handle *device)
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
	for (int i = 0; i < transfers.size(); i++) {
		const struct libusb_endpoint_descriptor& endpoint = interfaceDescriptor.endpoint[i];
		auto &transfersForEndpoint = transfers[i];
		for (int j = 0; j < transfersForEndpoint.size(); j++)
		{
			auto &transfer = transfersForEndpoint[j];
			transfer.endpointId = i;
			transfer.endpointAddress = endpoint.bEndpointAddress;
			transfer.parent = this;
			transfer.unpacker = unpacker;
			// Divide the transfers into groups of kInFlightMultiplier. Within
			// each group, each transfer points to the previous one, except for
			// the first, which points to the last one. With this scheme, the
			// nextTransfer pointers form cycles of length kInFlightMultiplier.
			//
			// @see The nextTransfer member declaration
			const bool isAtStartOfCycle = j % kInFlightMultiplier == 0;
			transfer.nextTransfer = &transfersForEndpoint[j + (isAtStartOfCycle ? kInFlightMultiplier - 1 : -1)];
		}
	}

	return true;
}

bool SoundplaneDriverLibusb::processThreadSetInitialCarriers(
	libusb_device_handle *device)
{
	return processThreadSetCarriers(device, kDefaultCarriers, sizeof(kDefaultCarriers)) >= 0;
}

bool SoundplaneDriverLibusb::processThreadSetDeviceState(MLSoundplaneState newState)
{
	mState.store(newState, std::memory_order_release);
	if (mListener) {
		mListener->deviceStateChanged(newState);
	}
	return !mQuitting.load(std::memory_order_acquire);
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
		reinterpret_cast<unsigned char *>(transfer.packets),
		sizeof(transfer.packets),
		transfer.numPackets(),
		processThreadTransferCallbackStatic,
		&transfer,
		200);
	libusb_set_iso_packet_lengths(
		transfer.transfer,
		sizeof(transfer.packets) / transfer.numPackets());

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
		for (int buffer = 0; buffer < kSoundplaneABuffersInFlight; buffer++)
		{
			auto &transfer = transfers[endpoint][buffer * kInFlightMultiplier];
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
	// Check if the transfer was successful
	if (transfer.transfer->status != LIBUSB_TRANSFER_COMPLETED)
	{
		fprintf(stderr, "Failed USB transfer failed: %s\n", libusb_error_name(transfer.transfer->status));
		mUsbFailed = true;
		return;
	}

	// Report kDeviceHasIsochSync if appropriate
	if (mState.load(std::memory_order_acquire) == kDeviceConnected)
	{
		// FIXME: Set default carriers
		processThreadSetDeviceState(kDeviceHasIsochSync);
	}

	transfer.unpacker->gotTransfer(
		transfer.endpointId,
		transfer.packets,
		transfer.transfer->num_iso_packets);

	// Schedule another transfer
	Transfer& nextTransfer = *transfer.nextTransfer;
	if (!processThreadScheduleTransfer(nextTransfer, transfer.transfer->dev_handle))
	{
		mUsbFailed = true;
		return;
	}
}

libusb_error SoundplaneDriverLibusb::processThreadSetCarriers(
	libusb_device_handle *device, const unsigned char *carriers, size_t carriersSize)
{
	return sendControl(
		device,
		kRequestMask,
		0,
		kRequestCarriersIndex,
		carriers,
		carriersSize,
		nullptr,
		nullptr);
}

void SoundplaneDriverLibusb::processThreadHandleRequests(libusb_device_handle *device)
{
	const auto carrierMask = mEnableCarriersRequest.exchange(nullptr, std::memory_order_acquire);
	if (carrierMask)
	{
		unsigned long mask = *carrierMask;
		sendControl(
			device,
			kRequestMask,
			mask >> 16,
			mask,
			nullptr,
			0,
			nullptr,
			nullptr);
		delete carrierMask;
	}
	const auto carriers = mSetCarriersRequest.exchange(nullptr, std::memory_order_acquire);
	if (carriers)
	{
		processThreadSetCarriers(device, carriers->data(), carriers->size());
		delete carriers;
	}
}

void SoundplaneDriverLibusb::processThread()
{
	// Each iteration of this loop is one cycle of finding a Soundplane device,
	// using it, and the device going away.
	while (!mQuitting.load(std::memory_order_acquire))
	{
		mUsbFailed = false;
		Transfers transfers;
		LibusbClaimedDevice handle;
		LibusbUnpacker unpacker(makeAnomalyFilter(
			[this](int startupCtr, float df, const SoundplaneOutputFrame& previousFrame, const SoundplaneOutputFrame& frame)
			{
				if (mListener)
				{
					mListener->handleDeviceError(kDevDataDiffTooLarge, startupCtr, 0, df, 0.);
					mListener->handleDeviceDataDump(previousFrame.data(), previousFrame.size());
					mListener->handleDeviceDataDump(frame.data(), frame.size());
				}
			},
			[this](const SoundplaneOutputFrame& frame)
			{
				PaUtil_WriteRingBuffer(&mOutputBuf, frame.data(), 1);
			}));

		bool success =
			processThreadOpenDevice(handle) &&
			processThreadGetDeviceInfo(handle.get()) &&
			processThreadSelectIsochronousInterface(handle.get()) &&
			processThreadFillTransferInformation(transfers, &unpacker, handle.get()) &&
			processThreadSetInitialCarriers(handle.get()) &&
			processThreadSetDeviceState(kDeviceConnected) &&
			processThreadScheduleInitialTransfers(transfers, handle.get());

		if (!success) continue;

		// FIXME: Handle debugger interruptions

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
			processThreadHandleRequests(handle.get());
		}

		if (!processThreadSetDeviceState(kNoDevice)) continue;
	}

	processThreadSetDeviceState(kDeviceIsTerminating);
}
