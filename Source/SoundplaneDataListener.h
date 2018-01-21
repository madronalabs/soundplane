
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include "MLSignal.h"
#include "MLModel.h"
#include "SoundplaneDriver.h"
#include "SoundplaneModelA.h"

const int kSoundplaneMaxControllerNumber = 127;

enum VoiceState
{
    kVoiceStateInactive = 0,
    kVoiceStateOn,
    kVoiceStateActive,
    kVoiceStateOff
};

struct SoundplaneZoneMessage
{
	ml::Symbol mType;
    ml::Symbol mSubtype;
	int mOffset;				// offset for OSC port or MIDI channel
    ml::Symbol mZoneName;
    float mData[8];
};

class SoundplaneDataListener
{
public:
	SoundplaneDataListener() : mActive(false) {}
	virtual ~SoundplaneDataListener() {}
    virtual void processSoundplaneMessage(const SoundplaneZoneMessage message) = 0;
    bool isActive() { return mActive; }

protected:
	bool mActive;
};

typedef std::list<SoundplaneDataListener*> SoundplaneListenerList;

inline std::ostream& operator<< (std::ostream& out, const SoundplaneZoneMessage & r)
{
    using namespace std;
    cout << "{" << r.mType << ", " << r.mSubtype << ", " << r.mOffset << ", " << r.mZoneName << ", ";
    for(float f : r.mData)
    {
        cout << f << " ";
    }
    cout << "}";
    return out;
}



