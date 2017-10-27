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
		mSurface(SensorGeometry::width, SensorGeometry::height),
		mCalibration(SensorGeometry::width, SensorGeometry::height) {}

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
			
			// TODO even simple calibration should be dividing by rest value!
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

//	for (;;)
	for(int i=0; i<5; ++i)
	{
		sleep(1);
		
		// ping the driver in order to let it know that we, its parent process, have not been killed
		// or otherwise terminated. In the event of ungraceful termination of this main process, 
		// the driver will not receive pings and therefore, shut itself down more gracefully.
		// DERP
		// this can't work for SIGKILL -- grab thread is in the same process, which is killed.
		// driver->keepAlive();
		
		// TODO
		
		
		// instead, change API so that driver does not create a process thread. owner must call process() repeatedly.
		// This gives clients more flexibility.
	
		
//		driver->process();
		
	}
	
	std::cout << "goodbye.\n";

    return 0;
}
		
		

