
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

struct SoundplaneOutputMessage
{
	ml::Symbol type;
    ml::Symbol subtype;
    ml::Symbol zoneName;
    int offset;				// offset for OSC port or MIDI channel
    float data[8];
};

class SoundplaneOutput
{
public:
	SoundplaneOutput() : mActive(false) {}
	virtual ~SoundplaneOutput() {}
    virtual void processSoundplaneMessage(const SoundplaneOutputMessage message) = 0;
    bool isActive() { return mActive; }

protected:
	bool mActive;
};
