// TestSoundplaneDriver.cpp
//
// A dummy implementation of a Soundplane driver for testing purposes.

#include "TestSoundplaneDriver.h"

#include "MLDSP.h"
#include "MLTime.h"

TestSoundplaneDriver::TestSoundplaneDriver(SoundplaneDriverListener* listener) :
	mQuitting(false),
	mListener(listener)
{
	assert(listener);
}

TestSoundplaneDriver::~TestSoundplaneDriver()
{
	// This causes getDeviceState to return kDeviceIsTerminating
	mQuitting.store(true, std::memory_order_release);
	mProcessThread.join();
}

MLSoundplaneState TestSoundplaneDriver::getDeviceState() const
{
	return mQuitting.load(std::memory_order_acquire) ?
		kDeviceIsTerminating :
		kDeviceHasIsochSync;
}

UInt16 TestSoundplaneDriver::getFirmwareVersion() const
{
	return 0;
}

std::string TestSoundplaneDriver::getSerialNumberString() const
{
	return "test";
}

const unsigned char *TestSoundplaneDriver::getCarriers() const
{
	return mCurrentCarriers.data();
}

void TestSoundplaneDriver::setCarriers(const Carriers& carriers)
{
}

void TestSoundplaneDriver::enableCarriers(unsigned long mask)
{
}

void TestSoundplaneDriver::processThread()
{
	SoundplaneOutputFrame frame;

	UInt64 startTime = getMicroseconds();
	UInt64 sentPackets = 0;

	while (!mQuitting.load(std::memory_order_acquire))
	{
		for (auto &value : frame)
		{
			value = fabs(MLRand())*0.1f;
		}

		mListener->receivedFrame(frame.data(), frame.size());
		sentPackets++;

		UInt64 now = getMicroseconds();
		UInt64 timeForNextFrame = startTime + kSoundplaneAUpdateFrequency * 1000 * sentPackets;
		if (timeForNextFrame > now)
		{
			usleep(timeForNextFrame - now);
		}
	}
}
