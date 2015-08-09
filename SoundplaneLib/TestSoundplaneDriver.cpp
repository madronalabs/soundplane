// TestSoundplaneDriver.cpp
//
// A dummy implementation of a Soundplane driver for testing purposes.

#include "TestSoundplaneDriver.h"

#include <unistd.h>

#include "MLDSP.h"
#include "MLTime.h"

TestSoundplaneDriver::TestSoundplaneDriver(SoundplaneDriverListener* listener) :
	mQuitting(false),
	mListener(listener),
	mProcessThread(std::thread(&TestSoundplaneDriver::processThread, this))
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

uint16_t TestSoundplaneDriver::getFirmwareVersion() const
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

	uint64_t startTime = getMicroseconds();
	uint64_t sentPackets = 0;

	while (!mQuitting.load(std::memory_order_acquire))
	{
		for (auto &value : frame)
		{
			value = fabs(MLRand())*0.1f;
		}

		mListener->receivedFrame(*this, frame.data(), frame.size());
		sentPackets++;

		uint64_t now = getMicroseconds();
		uint64_t timeForNextFrame = startTime + kSoundplaneAUpdateFrequency * 1000 * sentPackets;
		if (timeForNextFrame > now)
		{
			usleep(timeForNextFrame - now);
		}
	}
}
