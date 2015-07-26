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
}

void SoundplaneDriverLibusb::init()
{
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
