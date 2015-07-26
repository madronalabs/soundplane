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
    mListener(listener)
{
}

SoundplaneDriverLibusb::~SoundplaneDriverLibusb()
{
	libusb_exit(mLibusbContext);
}

void SoundplaneDriverLibusb::init()
{
	if (libusb_init(&mLibusbContext) < 0) {
		throw new std::runtime_error("Failed to initialize libusb");
	}

	// create device grab thread
	mGrabThread = std::thread(&SoundplaneDriverLibusb::grabThread, this);
	mGrabThread.detach();  // REVIEW: mGrabThread is leaked
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
	return kNoDevice;
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

void SoundplaneDriverLibusb::grabThread() {
	for (;;)
	{
		libusb_device_handle *handle = libusb_open_device_with_vid_pid(
			mLibusbContext, kSoundplaneUSBVendor, kSoundplaneUSBProduct);

		printf("Handle: %p\n", handle);
		sleep(1);
	}
}
