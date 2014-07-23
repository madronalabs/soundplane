
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_MIDI_OUTPUT_H_
#define __SOUNDPLANE_MIDI_OUTPUT_H_

#include "JuceHeader.h"

#include <vector>
#include <tr1/memory>
#include <stdlib.h>

#include "MLDebug.h"
#include "TouchTracker.h"
#include "SoundplaneModelA.h"
#include "SoundplaneDataListener.h"
#include "MLTime.h"

const int kMaxMIDIVoices = 16;

class MIDIVoice
{
public:
	MIDIVoice();
	~MIDIVoice();

    int age;
	float x;
	float y;
	float z;
	float dz;
	float note;
	float startX;
	float startY;
	float startNote;

	int mMIDINote;
	int mMIDIVel;	
	int mMIDIBend;
	int mMIDIXCtrl;
	int mMIDIYCtrl;
	int mMIDIPressure;
	
    VoiceState mState;
};

class MIDIDevice
{
public:
	MIDIDevice(const std::string &, int);
	~MIDIDevice();
	const std::string& getName() { return mName; }
	juce::MidiOutput* getDevice();
	juce::MidiOutput* open();
	void close();
	
private:
	std::string mName;
	int mIndex;
	bool mIsInternal; // for interapplication devices
};

typedef std::tr1::shared_ptr<MIDIDevice> MIDIDevicePtr;

class SoundplaneMIDIOutput :
	public SoundplaneDataListener
{
public:
	SoundplaneMIDIOutput();
	~SoundplaneMIDIOutput();
	void initialize();
	
    // SoundplaneDataListener
    void processSoundplaneMessage(const SoundplaneDataMessage* msg);

    void modelStateChanged();
	void setDataFreq(float f) { mDataFreq = f; }
	
	void findMIDIDevices ();
	void setDevice(int d);
	void setDevice(const std::string& deviceStr);
	int getNumDevices();
	const std::string& getDeviceName(int d);
	std::vector<std::string>& getDeviceList();
	
	void setActive(bool v);
	void setPressureActive(bool v);

	void setMaxTouches(int t) { mVoices = clamp(t, 0, kMaxMIDIVoices); }
	void setBendRange(int r) { mBendRange = r; }
	void setTranspose(int t) { mTranspose = t; }
	void setRetrig(int t) { mRetrig = t; }
	void setAbsRel(int t) { mAbsRel = t; }
	void setHysteresis(float t) { mHysteresis = t; }

	void setMultiChannel(bool v);
	void setStartChannel(int v);
	void setKymaPoll(bool v) { mKymaPoll = v; }
	
private:

    void sendPressure(int chan, float p);

	int mVoices;
	
	MIDIVoice mMIDIVoices[kMaxMIDIVoices];
    SoundplaneDataMessage mMessagesByZone[kSoundplaneAMaxZones];

	std::vector<MIDIDevicePtr> mDevices;
	std::vector<std::string> mDeviceList;
	juce::MidiOutput* mpCurrentDevice;
	
	float mDataFreq;
    UInt64 mCurrFrameStartTime;
	UInt64 mLastFrameStartTime;
    bool mTimeToSendNewFrame;
    bool mGotNoteChangesThisFrame;
    
	bool mPressureActive;
	UInt64 mLastTimeDataWasSent;
	UInt64 mLastTimeNRPNWasSent;
	int mBendRange;
	int mTranspose;
	int mRetrig;
	int mAbsRel;
	float mHysteresis;
	
	bool mMultiChannel;
	int mStartChannel;
	bool mKymaPoll;
};


#endif // __SOUNDPLANE_MIDI_OUTPUT_H_
