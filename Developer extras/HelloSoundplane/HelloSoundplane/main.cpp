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

#include "SoundplaneDriver.h"
#include "SoundplaneModelA.h"
#include "MLSignal.h"

int main(int argc, const char * argv[]) 
{
	MLSignal mSurface(kSoundplaneWidth, kSoundplaneHeight);
	MLSignal mCalibration(kSoundplaneWidth, kSoundplaneHeight);	
	int driverState = 0;	
	SoundplaneDriver driver;
	
	std::cout << "Hello, Soundplane?\n";	
	
	driver.init();	
	while(driverState != kDeviceHasIsochSync)
	{
		driverState = driver.getDeviceState();
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		std::cout << "waiting for driver, state:" << driverState << "\n";
	}
	
	// read a single frame as calibration snapshot
	driver.readSurface(mCalibration.getBuffer());
	
	int framesRead, frameCounter = 0;
	while(1)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		
		// read all available frames from driver
		do
		{
			framesRead = driver.readSurface(mSurface.getBuffer());
			frameCounter += framesRead;
		}
		while(framesRead);
			
		// print snapshot of latest frame, minus calibration
		if(frameCounter > 1000)
		{
			frameCounter -= 1000;
			mSurface.subtract(mCalibration);
			mSurface.scale(100.f);
			mSurface.flipVertical();
			
			std::cout << "\n";
			mSurface.dumpASCII(std::cout);
			mSurface.dump(std::cout);
		}
	}
	
    return 0;
}
