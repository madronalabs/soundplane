//
//  main.cpp
//  HelloSoundplane
//
//  Created by Randy Jones on 7/22/15.
//  Copyright (c) 2015 Madrona Labs. All rights reserved.
//

#include <iostream>
#include <chrono>
#include <thread>
#include <unistd.h>

#include "SoundplaneDriver.h"
#include "SoundplaneModelA.h"
#include "MLSignal.h"

namespace
{

class HelloSoundplaneDriverListener : public SoundplaneDriverListener
{
public:
	HelloSoundplaneDriverListener()	:
		mSurface(kSoundplaneWidth, kSoundplaneHeight),
		mCalibration(kSoundplaneWidth, kSoundplaneHeight) {}

	virtual void deviceStateChanged(SoundplaneDriver& driver, MLSoundplaneState s) override
	{
		std::cout << "Device state changed: " << s << std::endl;
	}

	virtual void receivedFrame(SoundplaneDriver& driver, const float* data, int size) override
	{
		if (!mHasCalibration)
		{
			memcpy(mCalibration.getBuffer(), data, sizeof(float) * size);
			mHasCalibration = true;
		}
		else if (mFrameCounter == 0)
		{
			memcpy(mSurface.getBuffer(), data, sizeof(float) * size);
			mSurface.subtract(mCalibration);
			mSurface.scale(100.f);
			mSurface.flipVertical();

			std::cout << "\n";
			mSurface.dumpASCII(std::cout);
			mSurface.dump(std::cout);
		}

		mFrameCounter = (mFrameCounter + 1) % 1000;
	}

private:
	int mFrameCounter = 0;
	bool mHasCalibration = false;
	MLSignal mSurface;
	MLSignal mCalibration;
};

}

int main(int argc, const char * argv[])
{
	HelloSoundplaneDriverListener listener;
	const auto driver = SoundplaneDriver::create(&listener);

	std::cout << "Hello, Soundplane?\n";
	std::cout << "Initial device state: " << driver->getDeviceState() << std::endl;

	for (;;)
	{
		sleep(1);
	}

    return 0;
}
