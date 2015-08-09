// Mock test driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __TEST_SOUNDPLANE_DRIVER__
#define __TEST_SOUNDPLANE_DRIVER__

#include <atomic>
#include <thread>

#include "SoundplaneDriver.h"
#include "SoundplaneModelA.h"

class TestSoundplaneDriver : public SoundplaneDriver
{
public:
	TestSoundplaneDriver(SoundplaneDriverListener* listener);
	~TestSoundplaneDriver();

	virtual MLSoundplaneState getDeviceState() const override;
	virtual uint16_t getFirmwareVersion() const override;
	virtual std::string getSerialNumberString() const override;

	virtual const unsigned char *getCarriers() const override;
	virtual void setCarriers(const Carriers& carriers) override;
	virtual void enableCarriers(unsigned long mask) override;

private:
	void processThread();

	/**
	 * Only there to have some allocated memory that getCarriers can return.
	 */
	Carriers mCurrentCarriers;

	/**
	 * mQuitting is set to true by the destructor, and is read by the processing
	 * thread and getDeviceState in order to know if the driver is quitting.
	 */
	std::atomic<bool> mQuitting;

	SoundplaneDriverListener * const mListener;

	std::thread	mProcessThread;
};

#endif // __TEST_SOUNDPLANE_DRIVER__
