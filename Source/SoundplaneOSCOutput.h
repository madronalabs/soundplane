
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_OSC_OUTPUT_H_
#define __SOUNDPLANE_OSC_OUTPUT_H_

#include <vector>
#include <list>
#include <memory>
#include <chrono>
#include <stdint.h>

#include "MLT3D.h"
#include "MLDebug.h"
#include "SoundplaneOutput.h"
#include "SoundplaneModelA.h"
#include "JuceHeader.h"

#include "Controller.h"
#include "Touch.h"

#include "OSC/osc/OscOutboundPacketStream.h"
#include "OSC/ip/UdpSocket.h"

extern const char* kDefaultHostnameString;

const int kUDPOutputBufferSize = 4096;

using namespace std::chrono;

class SoundplaneOSCOutput :
	public SoundplaneOutput
{
public:
	SoundplaneOSCOutput();
	~SoundplaneOSCOutput();
	
	int getKymaMode();
	void setKymaMode(bool m);
	void setKymaPort(int p);

	void connect();

    // SoundplaneOutput
    void beginOutputFrame(time_point<system_clock> now) override;
    void processTouch(int i, int offset, const Touch& m) override;
    void processController(int z, int offset, const Controller& m) override;
    void endOutputFrame() override;

	void setDataRate(int r) { mDataRate = r; }
	
	void setActive(bool v);
	void setMaxTouches(int t) { mMaxTouches = ml::clamp(t, 0, kMaxTouches); }
	
	void setSerialNumber(int s) { mSerialNumber = s; }
	void notify(int connected);
    void doInfrequentTasks();
    
    void processMatrix(const MLSignal& m);
    
private:
	void initializeSocket(int port);
	osc::OutboundPacketStream* getPacketStreamForOffset(int offset);
	UdpTransmitSocket* getTransmitSocketForOffset(int portOffset);
	
    void sendFrame();
    void sendFrameToKyma();

	void sendInfrequentData();
	void sendInfrequentDataToKyma();

	int mMaxTouches;	
	
	std::array< TouchArray, kNumUDPPorts > mTouchesByPort;
    std::array< Controller, kSoundplaneAMaxZones > mControllersByZone;
    
    int mDataRate = 100;
    time_point<system_clock> mFrameTime;

	std::vector< std::vector < char > > mUDPBuffers;
	std::vector< std::unique_ptr< osc::OutboundPacketStream > > mUDPPacketStreams;
	std::vector< std::unique_ptr< UdpTransmitSocket > > mUDPSockets;
	
	int mCurrentBaseUDPPort;
	osc::int32 mFrameId;
	int mSerialNumber;
	
	bool mKymaMode;
	int mKymaPort;
};


#endif // __SOUNDPLANE_OSC_OUTPUT_H_
