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

using namespace std::chrono;

class HelloSoundplaneDriverListener : 
public SoundplaneDriverListener
{	
	// this callback is called from the driver's process thread for each frame. 
	// if we return too slowly, the driver may lose its place creating gaps in the sensor data.
	// 
	void onFrame(const SensorFrame& frame) override
	{ 
		if(mCalibrating)
		{
			// gather calibration for first second
			mCalibrateSum = add(mCalibrateSum, frame);
			mCalibrateSamples++;

			if(system_clock::now() - mStartCalibrateTime > seconds(1))
			{
				// finish calibration
				mCalibrating = false;
				std::cout << "calibrate done.\n";
				mCalibrateMean = divide(mCalibrateSum, mCalibrateSamples);
			}
		}
		else
		{
			mCalibratedFrame = multiply(calibrate(frame, mCalibrateMean), 2.f);		
			if(++mFrameCounter > 500)
			{
				dumpFrameAsASCII(std::cout, mCalibratedFrame);
				mFrameCounter = 0;
			}
		}
	}

	void onStartup() override
	{ 
		std::cout << "calibrating: please don't touch...\n";
		mStartCalibrateTime = system_clock::now();
		mCalibrating = true;
	}

	time_point<system_clock> mStartCalibrateTime;
	SensorFrame mCalibrateSum, mCalibrateMean, mCalibratedFrame;
	bool mCalibrating{false};
	int mFrameCounter{0};
	int mCalibrateSamples{0};
};

int main(int argc, const char * argv[])
{	
	HelloSoundplaneDriverListener listener;
	auto driver = SoundplaneDriver::create(listener);
	
	time_point<system_clock> start, now;
	start = now = system_clock::now();
	auto secondsSinceStart = duration_cast<seconds>(now - start).count();	
	auto prevSecondsSinceStart = secondsSinceStart;
	
	std::cout << "Hello, Soundplane!\n";
	
	const int kTestDuration = 4; 
	while(now - start < seconds(kTestDuration))
	{		
		now = system_clock::now();
		secondsSinceStart = duration_cast<seconds>(now - start).count();	
		if(prevSecondsSinceStart != secondsSinceStart)
		{
			std::cout << "seconds: " << secondsSinceStart << " \n";
		}
		
		prevSecondsSinceStart = secondsSinceStart;
		std::this_thread::sleep_for(milliseconds(100));		
	}
	
	std::cout << "goodbye.\n";
	return 0;
}

/*
 
 // finally, we have sync.
 //
 Carriers carriers;
 std::copy(kDefaultCarriers, kDefaultCarriers + sizeof(kDefaultCarriers), carriers.begin());
 setCarriers(carriers);
 
 mDeviceState = kDeviceHasIsochSync;
 lost = false;
 */
				

