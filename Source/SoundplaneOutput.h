// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include "MLSignal.h"
#include "MLModel.h"
#include "SoundplaneDriver.h"
#include "SoundplaneModelA.h"
#include "Touch.h"
#include "Zone.h"

#include <chrono>
using namespace std::chrono;

class SoundplaneOutput
{
public:
	SoundplaneOutput() : mActive(false) {}
	virtual ~SoundplaneOutput() {}
	
	bool isActive() { return mActive; }
	
	virtual void beginOutputFrame(time_point<system_clock> now) = 0;
	virtual void processTouch(int i, int offset, const Touch& m) = 0;
	virtual void processController(int z, int offset, const ZoneMessage& m) = 0;
	virtual void endOutputFrame() = 0;	
	virtual void clear() = 0;
	
protected:
	bool mActive;
};

