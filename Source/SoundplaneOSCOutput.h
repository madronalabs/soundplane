
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
#include "TouchTracker.h"
#include "JuceHeader.h"

#include "OscOutboundPacketStream.h"
#include "UdpSocket.h"

extern const char* kDefaultHostnameString;
const int kDefaultUDPPort = 3123;
const int kDefaultUDPReceivePort = 3124;
const int kUDPOutputBufferSize = 1500;

const int kMaxVoices = 16;

class OSCVoice
{
public:
	OSCVoice();
	~OSCVoice();

	float mStartX;
	float mStartY;
	int mAge;
	int mNoteOn;
	int mNoteOff;
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
	
	void modelStateChanged();
	void processFrame(const MLSignal& touchFrame);
	void setDataFreq(float f) { mDataFreq = f; }
	
	void setActive(bool v);
	void setMaxTouches(int t) { mVoices = clamp(t, 0, kMaxVoices); }
	
	void setSerialNumber(int s) { mSerialNumber = s; }
	void notify(int connected);
	
private:
	
	void doInfrequentTasks();

	bool mActive;
	int mVoices;
	
	OSCVoice mOSCVoices[kMaxVoices];
	
	float mDataFreq;
	UInt64 mLastTimeDataWasSent;

	UdpTransmitSocket* mpUDPSocket;		
    char* mpOSCBuf;
	osc::int32 mFrameId;
	int mSerialNumber;
	
	UInt64 lastInfrequentTaskTime;
	bool mKymaMode;
	
};


#endif // __SOUNDPLANE_OSC_OUTPUT_H_
