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
	return mQuitting.load(std::memory_order_acquire);
}

bool SoundplaneDriverLibusb::processThreadOpenDevice(LibusbClaimedDevice &outDevice) const
{
	for (;;)
	{
		libusb_device_handle* handle = libusb_open_device_with_vid_pid(
			mLibusbContext, kSoundplaneUSBVendor, kSoundplaneUSBProduct);
		LibusbClaimedDevice result(LibusbDevice(handle), 0);
		if (result)
		{
			std::swap(result, outDevice);
			return false;
		}
		if (processThreadWait(1000))
		{
			return true;
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

void SoundplaneDriverLibusb::printDebugInfo(libusb_device_handle *device) const
{
	libusb_config_descriptor *descriptor;
	if (libusb_get_active_config_descriptor(libusb_get_device(device), &descriptor) < 0) {
		fprintf(stderr, "Failed to get device debug information\n");
		return;
	}

	printf("Available Bus Power: %d mA\n", 2 * static_cast<int>(descriptor->MaxPower));
}

bool SoundplaneDriverLibusb::processThreadSetDeviceState(MLSoundplaneState newState)
{
	mState.store(newState, std::memory_order_release);
	if (mQuitting.load(std::memory_order_acquire)) {
		return true;
	} else {
		emitDeviceStateChanged(newState);
		return false;
	}
}

void SoundplaneDriverLibusb::processThread() {
	// Each iteration of this loop is one cycle of finding a Soundplane device,
	// using it, and the device going away.
	while (!mQuitting.load(std::memory_order_acquire))
	{
		/// Open and claim the device
		LibusbClaimedDevice handle;
		if (processThreadOpenDevice(handle)) return;

		/// Retrieve firmware version and serial number
		if (!processThreadGetDeviceInfo(handle.get())) {
			continue;
		}

		/// Print debug info
		printDebugInfo(handle.get());

		printf("Handle: %p\n", handle.get());
		if (processThreadSetDeviceState(kDeviceConnected)) return;
		sleep(10);
		if (processThreadSetDeviceState(kNoDevice)) return;
		sleep(10);
	}
}
