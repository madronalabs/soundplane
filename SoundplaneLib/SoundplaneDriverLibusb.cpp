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
	return 0;
}

std::string SoundplaneDriverLibusb::getSerialNumberString() const
{
	return "";
}

const unsigned char *SoundplaneDriverLibusb::getCarriers() const {
	return mCurrentCarriers;
}

int SoundplaneDriverLibusb::setCarriers(const unsigned char *carriers) {
	return 0;
}

int SoundplaneDriverLibusb::enableCarriers(unsigned long mask) {
	return 0;
}

bool SoundplaneDriverLibusb::processThreadWait(int ms)
{
	std::unique_lock<std::mutex> lock(mMutex);
	mCondition.wait_for(lock, std::chrono::milliseconds(ms));
	return mQuitting.load(std::memory_order_acquire);
}

bool SoundplaneDriverLibusb::processThreadOpenDevice(LibusbClaimedDevice &outDevice)
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

void SoundplaneDriverLibusb::processThread() {
	// Each iteration of this loop is one cycle of finding a Soundplane device,
	// using it, and the device going away.
	for (;;)
	{
		LibusbClaimedDevice handle;
		if (processThreadOpenDevice(handle)) return;

		printf("Handle: %p\n", handle.get());
		mState.store(kDeviceConnected, std::memory_order_release);
		sleep(10);
		mState.store(kNoDevice, std::memory_order_release);
		sleep(10);
	}
}
