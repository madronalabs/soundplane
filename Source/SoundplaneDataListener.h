
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_DATA_LISTENER__
#define __SOUNDPLANE_DATA_LISTENER__

#include "MLSignal.h"
#include "MLModel.h"


class SoundplaneDataListener 
{
public:
	SoundplaneDataListener() {}
	virtual ~SoundplaneDataListener() {}
	virtual void processFrame(const MLSignal& touchFrame) {}
	virtual void notify(int) {}
};


#endif // __SOUNDPLANE_DATA_LISTENER__

