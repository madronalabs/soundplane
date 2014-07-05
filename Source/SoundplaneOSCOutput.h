
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_OSC_OUTPUT_H_
#define __SOUNDPLANE_OSC_OUTPUT_H_

#include <vector>
#include <list>
#include <tr1/memory>

#include "MLDebug.h"
#include "MLTime.h"
#include "SoundplaneDataListener.h"
#include "SoundplaneModelA.h"
#include "TouchTracker.h"
#include "JuceHeader.h"

#include "OscOutboundPacketStream.h"
#include "UdpSocket.h"

extern const char* kDefaultHostnameString;
const int kDefaultUDPPort = 3123;
const int kDefaultUDPReceivePort = 3124;
const int kUDPOutputBufferSize = 4096;

class OSCVoice
{
public:
	OSCVoice();
	~OSCVoice();

	float startX;
	float startY;
    float x;
    float y;
    float z;
    float z1;
    float note;
	VoiceState mState;
};

class SoundplaneOSCOutput :
	public SoundplaneDataListener
{
public:
	SoundplaneOSCOutput();
	~SoundplaneOSCOutput();
	void initialize();
	
	void connect(const char* name, int port);
	int getKymaMode();
	void setKymaMode(bool m);
	
    // SoundplaneDataListener
    void processSoundplaneMessage(const SoundplaneDataMessage* msg);
    
	void modelStateChanged();
	void setDataFreq(float f) { mDataFreq = f; }
	
	void setActive(bool v);
	void setMaxTouches(int t) { mMaxTouches = clamp(t, 0, kSoundplaneMaxTouches); }
	
	void setSerialNumber(int s) { mSerialNumber = s; }
	void notify(int connected);
	
	void doInfrequentTasks();

private:	

	int mMaxTouches;	
	OSCVoice mOSCVoices[kSoundplaneMaxTouches];
    SoundplaneDataMessage mMessagesByZone[kSoundplaneAMaxZones];
    
	float mDataFreq;
	UInt64 mCurrFrameStartTime;
	UInt64 mLastFrameStartTime;
    bool mTimeToSendNewFrame;

	UdpTransmitSocket* mpUDPSocket;		
    char* mpOSCBuf;
	osc::int32 mFrameId;
	int mSerialNumber;
	
	UInt64 lastInfrequentTaskTime;
	bool mKymaMode;
    bool mGotNoteChangesThisFrame;
    bool mGotMatrixThisFrame;
    SoundplaneDataMessage mMatrixMessage;
};


#endif // __SOUNDPLANE_OSC_OUTPUT_H_
