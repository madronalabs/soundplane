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

int main(int argc, const char * argv[])
{	
	SensorFrame frame;
	SensorFrame calibrateSum, calibrateMean;
	SensorFrame calibratedFrame;
	int calibrateSamples = 0;
	
	const auto driver = SoundplaneDriver::create();
	const int kTestDuration = 8; // seconds

	std::cout << "Hello, Soundplane!\n";
	
	int secondsSinceStart = 0;
	int previousSecondsSinceStart = 0;
	int frameCounter = 0;
	int noFrameCounter = 0;
	int gapCounter = 0;
	bool calibrating = false;
	std::chrono::time_point<std::chrono::system_clock> previous, now, start, calibrateStart;
	start = previous = now = std::chrono::system_clock::now();
	
	// wait for connect, then calibrate
	while(secondsSinceStart < kTestDuration)
	{
		SoundplaneDriver::returnValue r = driver->process(&frame);
		now = std::chrono::system_clock::now();
		previousSecondsSinceStart = secondsSinceStart;
		secondsSinceStart = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();	

		if(r.errorCode == kDevNoErr)
		{
			switch(r.deviceState)
			{
				case kNoDevice:
					break;
				case kDeviceConnected:
					break;
				case kDeviceHasIsochSync:
					if(r.stateChanged)
					{
						// if we just changed state to isoch sync, begin calibration
						calibrating = true;
						calibrateSum = multiply(calibrateSum, 0.f);
						calibrateSamples = 0.f;
						calibrateStart = now;
						std::cout << "calibrating, don't touch...\n";
					}
					else
					{
						if(calibrating)
						{
							// gather calibration
							calibrateSum = add(calibrateSum, frame);
							calibrateSamples++;
							std::cout << ".";
							
							int secondsCalibrating = std::chrono::duration_cast<std::chrono::seconds>(now - calibrateStart).count();		
							if(secondsCalibrating > 0)
							{
								// finish calibration
								calibrating = false;
								std::cout << "calibrate done.\n";
								calibrateMean = divide(calibrateSum, calibrateSamples);
							}
						}
						else
						{
							// every frame, just count frames
							{
								frameCounter++;
							}
						}
					}
					break;
				case kDeviceIsTerminating:
				default:
					break;
			}
		}
		else if(r.errorCode == kDevNoNewFrame)
		{
			noFrameCounter++;
		}
		else if(r.errorCode == kDevGapInSequence)
		{
			gapCounter++;
		}
						
		// if synched, print calibrated pressure data every second 
		if((r.deviceState == kDeviceHasIsochSync) && (previousSecondsSinceStart != secondsSinceStart))
		{
			std::cout << "seconds since start: " << secondsSinceStart << "   frames: " << frameCounter << 
			"   no frames: " << noFrameCounter << "   gaps: " << gapCounter << "\n";
			calibratedFrame = multiply(calibrate(frame, calibrateMean), 2.f);							
			dumpFrameAsASCII(std::cout, calibratedFrame);
			
			std::cout << "\n";
			frameCounter = noFrameCounter = gapCounter = 0;
		}
		
		// sleep, longer if not synched
		if(r.deviceState == kDeviceHasIsochSync)
		{
			std::this_thread::sleep_for(std::chrono::microseconds(500));
		}
		else
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}		
	}
	
	std::cout << "goodbye.\n";

    return 0;
}
		
		

