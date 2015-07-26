
// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_DRIVER_LIBUSB__
#define __SOUNDPLANE_DRIVER_LIBUSB__

#include <condition_variable>
#include <mutex>
#include <thread>

#include <libusb.h>

#include "SoundplaneDriver.h"
#include "SoundplaneModelA.h"

class SoundplaneDriverLibusb : public SoundplaneDriver
{
public:
	/**
	 * listener may be nullptr
	 */
	SoundplaneDriverLibusb(SoundplaneDriverListener* listener);
	~SoundplaneDriverLibusb();

	void init();

	virtual int readSurface(float* pDest) override;
	virtual void flushOutputBuffer() override;
	virtual MLSoundplaneState getDeviceState() const override;
	virtual UInt16 getFirmwareVersion() const override;
	virtual std::string getSerialNumberString() const override;

	virtual const unsigned char *getCarriers() const override;
	virtual int setCarriers(const unsigned char *carriers) override;
	virtual int enableCarriers(unsigned long mask) override;

private:
	/**
	 * Returns true if the process thread should quit.
	 *
	 * May spuriously wait for a shorter than the specified time.
	 */
	bool processThreadWait(int ms);
	/**
	 * Returns nullptr if the process thread should quit.
	 */
	libusb_device_handle* processThreadOpenDevice();
	void processThread();

	bool mQuitting = false;
	std::mutex mMutex;  // Used with mCondition and protects mQuitting
	std::condition_variable mCondition;  // Used to wake up the process thread

	libusb_context				*mLibusbContext = nullptr;
	SoundplaneDriverListener	*mListener;
	unsigned char				mCurrentCarriers[kSoundplaneSensorWidth];

	std::thread					mProcessThread;
};

#endif // __SOUNDPLANE_DRIVER_LIBUSB__
