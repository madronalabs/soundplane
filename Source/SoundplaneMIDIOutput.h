
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_MIDI_OUTPUT_H_
#define __SOUNDPLANE_MIDI_OUTPUT_H_

#include "JuceHeader.h"

#include <vector>
#include <memory>
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
	float vibrato;

	int mMIDINote;
	int mPreviousMIDINote;
	int mMIDIVel;	
	int mMIDIBend;
	int mMIDIXCtrl;
	int mMIDIYCtrl;
	int mMIDIPressure;
	int mMIDIChannel;
	
	bool mSendNoteOff;
	bool mSendNoteOn;
	bool mSendPressure;
	bool mSendPitchBend;
	bool mSendXCtrl;
	bool mSendYCtrl;
	
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

typedef std::shared_ptr<MIDIDevice> MIDIDevicePtr;

enum MidiMode
{
    single_1,
    single_2,
    mpe,
    multi_1,
    multi_2
};

class SoundplaneMIDIOutput :
	public SoundplaneDataListener
{
public:
	SoundplaneMIDIOutput();
	~SoundplaneMIDIOutput();
	void initialize();
	
    // SoundplaneDataListener
    void processSoundplaneMessage(const SoundplaneDataMessage* msg);

	void setDataFreq(float f) { mDataFreq = f; }
	
	void findMIDIDevices ();
	void setDevice(int d);
	void setDevice(const std::string& deviceStr);
	int getNumDevices();
	const std::string& getDeviceName(int d);
	const std::vector<std::string>& getDeviceList();
	
	void setActive(bool v);
	void setPressureActive(bool v);

	void setMaxTouches(int t);
	void setBendRange(int r);
	void setTranspose(int t) { mTranspose = t; }
	void setGlissando(int t) { mGlissando = t; }
	void setAbsRel(int t) { mAbsRel = t; }
	void setHysteresis(float t) { mHysteresis = t; }

	void setMode(MidiMode v);
	void setStartChannel(int v);
	void setKymaPoll(bool v) { mKymaPoll = v; }
	
private:
	int getMPEMainChannel();
	int getMPEVoiceChannel(int voice);
	int getVoiceChannel(int voice);
	int getMIDIPitchBend(MIDIVoice* pVoice);
	int getMIDIVelocity(MIDIVoice* pVoice);
	int getRetriggerVelocity(MIDIVoice* pVoice);
	int getMostRecentVoice();

	int getMIDIPressure(MIDIVoice* pVoice);

	void sendMIDIChannelPressure(int chan, int p);
	void sendAllMIDIChannelPressures(int p);
	void sendAllMIDINotesOff();
	
	void sendMPEChannels();
    void sendPitchbendRange();

	void setupVoiceChannels();
	void updateVoiceStates();
	void sendMIDIVoiceMessages();
	void sendMIDIControllerMessages();
	void pollKyma();
	void dumpVoices();

	
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
	bool mGotControllerChanges;
    
	bool mPressureActive;
	UInt64 mLastTimeNRPNWasSent;

	int mBendRange;
	int mTranspose;
	int mGlissando;
	int mAbsRel;
	float mHysteresis;
	
    MidiMode mMidiMode;
	int      mMPEChannels;
	
	// channel to be used for single-channel output
	int mChannel;
	
	bool mKymaPoll;
	bool mVerbose;
	UInt64 mLastTimeVerbosePrint;
};


#endif // __SOUNDPLANE_MIDI_OUTPUT_H_
