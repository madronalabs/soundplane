
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneOSCOutput.h"
#include "MLTextUtils.h"

#include <thread>

using namespace ml;

const char* kDefaultHostnameString = "localhost";

// --------------------------------------------------------------------------------
#pragma mark SoundplaneOSCOutput

SoundplaneOSCOutput::SoundplaneOSCOutput() :
mCurrentBaseUDPPort(kDefaultUDPPort),
mFrameId(0),
mSerialNumber(0),
mKymaMode(false)
{
	try
	{
		// create buffers for UDP packet streams
		mUDPBuffers.resize(kNumUDPPorts);
		for(int i=0; i<kNumUDPPorts; ++i)
		{
			mUDPBuffers[i].resize(kUDPOutputBufferSize);
		}
		mUDPPacketStreams.resize(kNumUDPPorts);
		mUDPSockets.clear();
		mUDPSockets.resize(kNumUDPPorts);
	}
	catch(std::runtime_error err)
	{
		MLConsole() << "Failed to allocate OSC output: " << err.what() << "\n";
	}
}

SoundplaneOSCOutput::~SoundplaneOSCOutput()
{
	clear();
}

void SoundplaneOSCOutput::reconnect()
{
	setActive(false);
	
	std::string kymaStr("beslime");
	int kymaLen = kymaStr.length();
	setKymaMode(mHostName.substr(0, kymaLen) == kymaStr);
	
	try
	{
		MLConsole() << "SoundplaneOSCOutput: connecting to host " << mHostName << " starting at port " << mCurrentBaseUDPPort << " \n";

		for(int portOffset = 0; portOffset < kNumUDPPorts; portOffset++)
		{
			mUDPPacketStreams[portOffset] = std::unique_ptr< osc::OutboundPacketStream >
				(new osc::OutboundPacketStream( mUDPBuffers[portOffset].data(), kUDPOutputBufferSize ));
			
			mUDPSockets[portOffset] = std::unique_ptr< UdpTransmitSocket >
				(new UdpTransmitSocket(IpEndpointName(mHostName.c_str(), mCurrentBaseUDPPort + portOffset)));
			
			osc::OutboundPacketStream* p = getPacketStreamForOffset(portOffset);
			if(!p) return;
			UdpTransmitSocket* socket = getTransmitSocketForOffset(portOffset);
			if(!socket) return;
			
			*p << osc::BeginBundleImmediate;
			*p << osc::BeginMessage( "/t3d/dr" );
			*p << (osc::int32)mDataRate;
			*p << osc::EndMessage;
			*p << osc::EndBundle;
			socket->Send( p->Data(), p->Size() );
			
			MLConsole() << "                     connected to port " << mCurrentBaseUDPPort + portOffset << "\n";
		}
		
		setActive(true);
	}
	catch(std::runtime_error err)
	{
		MLConsole() << "                     connect error: " << err.what() << "\n";
		mCurrentBaseUDPPort = kDefaultUDPPort;
	}
}

int SoundplaneOSCOutput::getKymaMode()
{
	return mKymaMode;
}

void SoundplaneOSCOutput::setKymaMode(bool m)
{
	MLConsole() << "Kyma mode: " << m << "\n";
	mKymaMode = m;
}

void SoundplaneOSCOutput::setActive(bool v)
{
	mActive = v;
	
	// reset frame ID
	mFrameId = 0;
}

osc::OutboundPacketStream* SoundplaneOSCOutput::getPacketStreamForOffset(int portOffset)
{
	osc::OutboundPacketStream* p = mUDPPacketStreams[portOffset].get();
	if(p)
	{
		p->Clear();
	}
	return p;
}

UdpTransmitSocket* SoundplaneOSCOutput::getTransmitSocketForOffset(int portOffset)
{
	return &(*mUDPSockets[portOffset]);
}

const ml::Symbol startFrameSym("start_frame");
const ml::Symbol touchSym("touch");
const ml::Symbol onSym("on");
const ml::Symbol continueSym("continue");
const ml::Symbol offSym("off");
const ml::Symbol controllerSym("controller");
const ml::Symbol xSym("x");
const ml::Symbol ySym("y");
const ml::Symbol xySym("xy");
const ml::Symbol zSym("z");
const ml::Symbol toggleSym("toggle");
const ml::Symbol endFrameSym("end_frame");
const ml::Symbol matrixSym("matrix");
const ml::Symbol nullSym;

void SoundplaneOSCOutput::beginOutputFrame(time_point<system_clock> now)
{
	if(!mActive) return;
	mFrameTime = now;
	
	// update all voice states
	for(int offset=0; offset < kNumUDPPorts; ++offset)
	{
		for(int voiceIdx=0; voiceIdx < kMaxTouches; ++voiceIdx)
		{
			Touch& t = (mTouchesByPort[offset])[voiceIdx];
			if (t.state == kTouchStateOff)
			{
				t.state = kTouchStateInactive;
			}
		}
	}
	
	// clear controller array
	// TODO refactor, this Controller stuff is dumb
	for(int i=0; i<kSoundplaneAMaxZones; ++i)
	{
		mControllersByZone[i] = Controller{};
	}
		
}

void SoundplaneOSCOutput::processTouch(int i, int offset, const Touch& t)
{
	if(!mActive) return;
	// store incoming touch by port offset and index
	mTouchesByPort[offset][i] = t;
}

void SoundplaneOSCOutput::processController(int zoneID, int h, const Controller& m)
{
	if(!mActive) return;
	
	// store incoming controller by zone ID
	mControllersByZone[zoneID] = m;
	
	// store offset into Controller
	mControllersByZone[zoneID].offset = h;
}

void SoundplaneOSCOutput::endOutputFrame()
{
	if(!mActive) return;
	
	if(mKymaMode)
	{
		sendFrameToKyma();
	}
	else
	{
		sendFrame();
	}
}

void SoundplaneOSCOutput::clear()
{
	// TODO should add critical section on mActive, currently we're
	// not using it anywhere else so we're OK
	setActive(false);
	
	// allow process thread frames to finish
	std::this_thread::sleep_for(std::chrono::microseconds(2000));
	
	if(mKymaMode)
	{
		clearTouches();
		sendFrameToKyma();
	}
	else
	{
		clearTouches();
		sendFrame();
	}
}

void SoundplaneOSCOutput::sendFrame()
{
	// for each zone, send and clear any controller messages received since last frame
	// to the output port for that zone. controller messages are not sent in bundles.
	for(int i=0; i<kSoundplaneAMaxZones; ++i)
	{
		const Controller c = mControllersByZone[i];
		const Controller d = mSentControllersByZone[i];
		if(c != d)
		{
			int portOffset = c.offset;
			
			// send controller message: /zoneName val1 (val2) on port (kDefaultUDPPort + offset).
			osc::OutboundPacketStream* p = getPacketStreamForOffset(portOffset);
			UdpTransmitSocket* socket = getTransmitSocketForOffset(portOffset);
			if((!p) || (!socket)) return;
			
			TextFragment ctrlStr(TextFragment("/"), c.name.getTextFragment());
			
			*p << osc::BeginMessage( ctrlStr.getText() );
			
			Symbol t = c.type;
			
			if(t == "x")
			{
				*p << c.x;
			}
			else if(t == "y")
			{
				*p << c.y;
			}
			else if(t == "xy")
			{
				*p << c.x << c.y;
			}
			else if(t == "z")
			{
				*p << c.z;
			}
			else if(t == "toggle")
			{
				int t = (c.x > 0.5f);
					*p << t;
			}
			
			*p << osc::EndMessage;
			
			socket->Send( p->Data(), p->Size() );
			mSentControllersByZone[i] = c;
		}
	}
	
	// for each port, send an OSC bundle containing any touches.
	for(int portOffset=0; portOffset<kNumUDPPorts; ++portOffset)
	{
		// begin OSC bundle for this frame
		// timestamp is now stored in the bundle, synchronizing all info for this frame.
		osc::OutboundPacketStream* p = getPacketStreamForOffset(portOffset);
		UdpTransmitSocket* socket = getTransmitSocketForOffset(portOffset);
		if((!p) || (!socket)) return;
		
		osc::uint64 micros = duration_cast<microseconds>(mFrameTime.time_since_epoch()).count();
		*p << osc::BeginBundle(micros);
		
		// send frame start message
		*p << osc::BeginMessage( "/t3d/frm" );
		*p << mFrameId++ << mSerialNumber;
		*p << osc::EndMessage;
		
		for(int voiceIdx=0; voiceIdx < kMaxTouches; ++voiceIdx)
		{
			Touch& t = mTouchesByPort[portOffset][voiceIdx];
			
			if(touchIsActive(t))
			{
				osc::int32 touchID = voiceIdx + 1; // 1-based for OSC
				TextFragment address("/t3d/tch", ml::textUtils::naturalNumberToText(touchID));
				*p << osc::BeginMessage( address.getText() );
				*p << t.x << t.y << t.z << t.note;
				*p << osc::EndMessage;
			}
		}
		
		*p << osc::EndBundle;
		socket->Send( p->Data(), p->Size() );
	}
}

void SoundplaneOSCOutput::clearTouches()
{
	for(int portOffset=0; portOffset<kNumUDPPorts; ++portOffset)
	{
		for(int voiceIdx=0; voiceIdx < kMaxTouches; ++voiceIdx)
		{
			Touch& t = mTouchesByPort[portOffset][voiceIdx];
			
			// clear
			t.z = 0.f;
			t.state = kTouchStateOff;
		}
	}
}

void SoundplaneOSCOutput::sendFrameToKyma()
{
	osc::OutboundPacketStream* p = getPacketStreamForOffset(0);
	UdpTransmitSocket* socket = getTransmitSocketForOffset(0);
	if((!p) || (!socket)) return;
	
	*p << osc::BeginBundleImmediate;
	for(int voiceIdx=0; voiceIdx < kMaxTouches; ++voiceIdx)
	{
		Touch& t = mTouchesByPort[0][voiceIdx];
		osc::int32 touchID = voiceIdx; // 0-based for Kyma
		osc::int32 offOn = 1;
		
		if(t.state == kTouchStateOn)
		{
			offOn = -1;
		}
		else if(t.state == kTouchStateOff)
		{
			offOn = 0; // TODO periodically turn off silent voices
		}
		// note this is called for on and off
		if(t.state != kTouchStateInactive)
		{
			*p << osc::BeginMessage( "/key" );
			*p << touchID << offOn << t.note << t.z << t.y;
			*p << osc::EndMessage;
		}
	}
	
	*p << osc::EndBundle;
	socket->Send( p->Data(), p->Size() );
}

void SoundplaneOSCOutput::doInfrequentTasks()
{
	if(mKymaMode)
	{
		sendInfrequentDataToKyma();
	}
	else
	{
		sendInfrequentData();
	}
}

void SoundplaneOSCOutput::sendInfrequentData()
{
	for(int portOffset = 0; portOffset < kNumUDPPorts; portOffset++)
	{
		osc::OutboundPacketStream* p = getPacketStreamForOffset(portOffset);
		UdpTransmitSocket* socket = getTransmitSocketForOffset(portOffset);
		if((!p) || (!socket)) return;
		
		// send data rate to receiver
		*p << osc::BeginBundleImmediate;
		*p << osc::BeginMessage( "/t3d/dr" );
		*p << (osc::int32)mDataRate;
		*p << osc::EndMessage;
		*p << osc::EndBundle;
		socket->Send( p->Data(), p->Size() );
	}
}

void SoundplaneOSCOutput::sendInfrequentDataToKyma()
{
	osc::OutboundPacketStream* p = getPacketStreamForOffset(0);
	UdpTransmitSocket* socket = getTransmitSocketForOffset(0);
	if((!p) || (!socket)) return;
	
	// tell the Kyma that we want to receive info on our listening port
	*p << osc::BeginBundleImmediate;
	*p << osc::BeginMessage( "/osc/respond_to" );
	*p << (osc::int32)kDefaultUDPReceivePort;
	*p << osc::EndMessage;
	
	// tell Kyma we are a Soundplane
	*p << osc::BeginMessage( "/osc/notify/midi/Soundplane" );
	*p << (osc::int32)1;
	*p << osc::EndMessage;
	*p << osc::EndBundle;
	socket->Send( p->Data(), p->Size() );
	
	// send data rate to receiver
	*p << osc::BeginBundleImmediate;
	*p << osc::BeginMessage( "/t3d/dr" );
	*p << (osc::int32)mDataRate;
	*p << osc::EndMessage;
	*p << osc::EndBundle;
	socket->Send( p->Data(), p->Size() );
}


void SoundplaneOSCOutput::processMatrix(const MLSignal& m)
{
	osc::OutboundPacketStream* p = getPacketStreamForOffset(0);
	UdpTransmitSocket* socket = getTransmitSocketForOffset(0);
	if((!p) || (!socket)) return;
	
	*p << osc::BeginMessage( "/t3d/matrix" );
	*p << osc::Blob( m.getConstBuffer(), m.getSize()*sizeof(float) );
	*p << osc::EndMessage;
	
	socket->Send( p->Data(), p->Size() );
}


