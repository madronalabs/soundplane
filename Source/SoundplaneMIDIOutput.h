
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_MIDI_OUTPUT_H_
#define __SOUNDPLANE_MIDI_OUTPUT_H_

//#include "JuceHeader.h"

#include <vector>
#include <memory>
#include <chrono>
#include <stdint.h>
#include <stdlib.h>


//#include "TouchTracker.h"
#include "SoundplaneModelA.h"
#include "SoundplaneOutput.h"
#include "Touch.h"

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
	
    TouchState mState;
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

class SoundplaneMIDIOutput :
	public SoundplaneOutput
{
public:
	SoundplaneMIDIOutput();
	~SoundplaneMIDIOutput();
	void initialize();
    
    // SoundplaneOutput
    void beginOutputFrame(time_point<system_clock> now) override;
    void processTouch(int i, int offset, const Touch& m) override;
    void processController(int z, int offset, const Controller& m) override;
    void endOutputFrame() override; 
	
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

	void setMPEExtended(bool v);
	void setMPE(bool v);
	void setStartChannel(int v);
	void setKymaMode(bool v);
	
	void doInfrequentTasks();
	
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
	void pollKymaViaMIDI();
	void dumpVoices();
	
	int mVoices;
	
	MIDIVoice mMIDIVoices[kMaxMIDIVoices];
    
    std::array< TouchArray, kSoundplaneAMaxZones > mTouchesByZone;
    std::array< Controller, kSoundplaneAMaxZones > mControllersByZone;

	std::vector<MIDIDevicePtr> mDevices;
	std::vector<std::string> mDeviceList;
	juce::MidiOutput* mpCurrentDevice;

    bool mGotControllerChanges;
    
	bool mPressureActive;

	int mBendRange;
	int mTranspose;
	int mGlissando;
	int mAbsRel;
	float mHysteresis;
	
	bool mMPEExtended;
    bool mMPEMode;
	int mMPEChannels;
	
	// channel to be used for single-channel output
	int mChannel;
	
	bool mKymaMode;
	bool mVerbose;
};


#endif // __SOUNDPLANE_MIDI_OUTPUT_H_
