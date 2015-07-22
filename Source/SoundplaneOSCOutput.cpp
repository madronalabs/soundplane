
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneOSCOutput.h"

const char* kDefaultHostnameString = "localhost";

OSCVoice::OSCVoice() :
    startX(0),
    startY(0),
    x(0),
    y(0),
    z(0),
    note(0),
    mState(kVoiceStateInactive),
	portOffset(0),
	portOffset1(0)
{
}

OSCVoice::~OSCVoice()
{
}

// --------------------------------------------------------------------------------
#pragma mark SoundplaneOSCOutput

SoundplaneOSCOutput::SoundplaneOSCOutput() :
	mDataFreq(250.),
	mLastFrameStartTime(0),
//	mUDPSockets(0),
	mFrameId(0),
	mSerialNumber(0),
	lastInfrequentTaskTime(0),
	mKymaMode(false),
    mGotNoteChangesThisFrame(false),
    mGotMatrixThisFrame(false)
{
	// create buffers for UDP packet streams
	mUDPBuffers.resize(kNumUDPPorts);
	for(int i=0; i<kNumUDPPorts; ++i)
	{
		mUDPBuffers[i].resize(kUDPOutputBufferSize);
	}
	mUDPPacketStreams.resize(kNumUDPPorts);
	mUDPSockets.resize(kNumUDPPorts);
	mPortInitialized.resize(kNumUDPPorts);
	mPortInitialized = {false};
}

SoundplaneOSCOutput::~SoundplaneOSCOutput()
{
}

void SoundplaneOSCOutput::connect(const char* name, int port)
{
 //   if(mpUDPSocket)
	/*
	{
		debug() << "connect: " << port << "\n";
		
		//TODO check port change? 
		try
		{
//			mpUDPSocket->Connect(IpEndpointName(name, port));
		}
		catch(std::runtime_error err)
		{
			debug() << "SoundplaneOSCOutput: error connecting to " << name << ", port " << port << "\n";
			debug() << "        " << err.what() << "\n";
			return;
		}
		debug() << "SoundplaneOSCOutput:connected to " << name << ", port " << port << "\n";
	}
	 */
}

int SoundplaneOSCOutput::getKymaMode()
{
	return mKymaMode;
}

void SoundplaneOSCOutput::setKymaMode(bool m)
{
	mKymaMode = m;
}

void SoundplaneOSCOutput::setActive(bool v)
{
	mActive = v;
	
	// reset frame ID
	mFrameId = 0;
}

void SoundplaneOSCOutput::doInfrequentTasks()
{
	// for each possible offset: if a socket has been created at that offset, send data rate and notifications 
	for(int portOffset = 0; portOffset < kNumUDPPorts; portOffset++)
	{
		if(mPortInitialized[portOffset])	
		{
			osc::OutboundPacketStream& p = getPacketStreamForPort(portOffset);
			UdpTransmitSocket& socket = getTransmitSocketForPort(portOffset);

			if(mKymaMode)
			{
				p << osc::BeginBundleImmediate;
				p << osc::BeginMessage( "/osc/respond_to" );	
				p << (osc::int32)kDefaultUDPReceivePort;
				p << osc::EndMessage;
				
				p << osc::BeginMessage( "/osc/notify/midi/Soundplane" );	
				p << (osc::int32)1;
				p << osc::EndMessage;
				p << osc::EndBundle;
				socket.Send( p.Data(), p.Size() );
			}
			
			// send data rate to receiver
			p << osc::BeginBundleImmediate;
			p << osc::BeginMessage( "/t3d/dr" );	
			p << (osc::int32)mDataFreq;
			p << osc::EndMessage;
			p << osc::EndBundle;
			socket.Send( p.Data(), p.Size() );
		}
	}
}

int SoundplaneOSCOutput::initializePort(int portOffset)
{
	if(	mPortInitialized[portOffset] ) return true;
	
	mUDPPacketStreams[portOffset] = std::unique_ptr< osc::OutboundPacketStream >
		(new osc::OutboundPacketStream( mUDPBuffers[portOffset].data(), kUDPOutputBufferSize ));
	
	mUDPSockets[portOffset] = std::unique_ptr< UdpTransmitSocket >
	(new UdpTransmitSocket(IpEndpointName(kDefaultHostnameString, kDefaultUDPPort + portOffset)));
	
	mPortInitialized[portOffset] = true;
	return true;
}

osc::OutboundPacketStream& SoundplaneOSCOutput::getPacketStreamForPort(int portOffset)
{
	if(!mPortInitialized[portOffset])
	{
		initializePort(portOffset);
	}
	osc::OutboundPacketStream& p (*mUDPPacketStreams[portOffset]);
	p.Clear();
	return p;
}

UdpTransmitSocket& SoundplaneOSCOutput::getTransmitSocketForPort(int portOffset)
{
	if(!mPortInitialized[portOffset])
	{
		initializePort(portOffset);
	}
	return *mUDPSockets[portOffset];
}

void SoundplaneOSCOutput::processSoundplaneMessage(const SoundplaneDataMessage* msg)
{
    static const MLSymbol startFrameSym("start_frame");
    static const MLSymbol touchSym("touch");
    static const MLSymbol onSym("on");
    static const MLSymbol continueSym("continue");
    static const MLSymbol offSym("off");
    static const MLSymbol controllerSym("controller");
    static const MLSymbol xSym("x");
    static const MLSymbol ySym("y");
    static const MLSymbol xySym("xy");
    static const MLSymbol zSym("z");
    static const MLSymbol toggleSym("toggle");
    static const MLSymbol endFrameSym("end_frame");
    static const MLSymbol matrixSym("matrix");
    static const MLSymbol nullSym;
    
	if (!mActive) return;
    MLSymbol type = msg->mType;
    MLSymbol subtype = msg->mSubtype;
    
    int i;
	float x, y, z, dz, note, vibrato;
    
    if(type == startFrameSym)
    {
        const UInt64 dataPeriodMicrosecs = 1000*1000 / mDataFreq;
        mCurrFrameStartTime = getMicroseconds();
        if (mCurrFrameStartTime > mLastFrameStartTime + (UInt64)dataPeriodMicrosecs)
        {
            mLastFrameStartTime = mCurrFrameStartTime;
            mTimeToSendNewFrame = true;
        }
        else
        {
            mTimeToSendNewFrame = false;
        }        
        mGotNoteChangesThisFrame = false;
        mGotMatrixThisFrame = false;
    }
    else if(type == touchSym)
    {
        // get touch data
        i = msg->mData[0];
        x = msg->mData[1];
        y = msg->mData[2];
        z = msg->mData[3];
        dz = msg->mData[4];
		note = msg->mData[5];
		vibrato = msg->mData[6];
		
        OSCVoice* pVoice = &mOSCVoices[i];        
        pVoice->x = x;
        pVoice->y = y;
        pVoice->z = z;
        pVoice->note = note + vibrato;
		pVoice->portOffset1 = pVoice->portOffset;
		pVoice->portOffset = msg->mOffset;
		
        if(subtype == onSym)
        {
            pVoice->startX = x;
            pVoice->startY = y;
			
			// send dz (velocity) as first z value
			pVoice->z = dz;
			
            pVoice->mState = kVoiceStateOn;
            mGotNoteChangesThisFrame = true;
        }
        if(subtype == continueSym)
        {
            pVoice->mState = kVoiceStateActive;
        }
        if(subtype == offSym)
        {
            if((pVoice->mState == kVoiceStateActive) || (pVoice->mState == kVoiceStateOn))
            {
                pVoice->mState = kVoiceStateOff;
                pVoice->z = 0;
                mGotNoteChangesThisFrame = true;
            }
        }
    }
    else if(type == controllerSym)
    {
        // when a controller message comes in, make a local copy of the message and store by zone ID.
        int zoneID = msg->mData[0];
        mMessagesByZone[zoneID] = *msg;
    }
    else if(type == matrixSym)
    {
        // store matrix to send with bundle
        mGotMatrixThisFrame = true;
        mMatrixMessage = *msg;
    }
    else if(type == endFrameSym)
    {
        if(mGotNoteChangesThisFrame || mTimeToSendNewFrame)
        {
            // for each zone, send and clear any controller messages received since last frame
			// to the output port for that zone. controller messages are not sent in bundles.
            for(int i=0; i<kSoundplaneAMaxZones; ++i)
            {
                SoundplaneDataMessage* pMsg = &(mMessagesByZone[i]);
				int portOffset = pMsg->mOffset;
				
                if(pMsg->mType == controllerSym)
                {
                    // send controller message: /t3d/[zoneName] val1 (val2) on port (3123 + offset).
					osc::OutboundPacketStream& p = getPacketStreamForPort(portOffset);
					UdpTransmitSocket& socket = getTransmitSocketForPort(portOffset);

					// int channel = pMsg->mData[1];
                    // int ctrlNum1 = pMsg->mData[2];
                    // int ctrlNum2 = pMsg->mData[3];
                    // int ctrlNum3 = pMsg->mData[4];
                    x = pMsg->mData[5];
                    y = pMsg->mData[6];
                    z = pMsg->mData[7];
                    std::string ctrlStr("/");
                    ctrlStr += (pMsg->mZoneName);
                    
                    p << osc::BeginMessage( ctrlStr.c_str() );
                    
                    // get control data by type and add to message
                    if(pMsg->mSubtype == xSym)
                    {
                        p << x;
                    }
                    else if(pMsg->mSubtype == ySym)
                    {
                        p << y;
                    }
                    else if (pMsg->mSubtype == xySym)
                    {
                        p << x << y;
                    }
                    else if (pMsg->mSubtype == zSym)
                    {
                        p << z;
                    }
                    else if (pMsg->mSubtype == toggleSym)
                    {
                        int t = (x > 0.5f);
                        p << t;
                    }
                    p << osc::EndMessage;
                    
                    // clear
                    mMessagesByZone[i].mType = nullSym;

					socket.Send( p.Data(), p.Size() );
                }
            }
						
			// gather active voices by port offset. 
			std::vector< std::vector<int> > activeVoicesByPortOffset;
			activeVoicesByPortOffset.resize(kNumUDPPorts);
			for(int i=0; i<mMaxTouches; ++i)
			{
				OSCVoice* pVoice = &mOSCVoices[i];
				
				// send any active voices to their port offset group
				if(pVoice->mState != kVoiceStateInactive)
				{
					activeVoicesByPortOffset[pVoice->portOffset].push_back(i);
				}
				// send voices that changed port offset to the group of the previous offset
				// so they can be turned off
				if(pVoice->portOffset1 != pVoice->portOffset)
				{
					activeVoicesByPortOffset[pVoice->portOffset1].push_back(i);
				}
			}		
			
			// for each port, send an OSC bundle containing any touches.
			for(int portOffset=0; portOffset<kNumUDPPorts; ++portOffset)
			{
				if(activeVoicesByPortOffset[portOffset].size() > 0)
				{
					// begin OSC bundle for this frame
					// timestamp is now stored in the bundle, synchronizing all info for this frame.
					osc::OutboundPacketStream& p = getPacketStreamForPort(portOffset);					 
					UdpTransmitSocket& socket = getTransmitSocketForPort(portOffset);

					// begin bundle
					p << osc::BeginBundle(mCurrFrameStartTime);
					
					// send frame start message
					p << osc::BeginMessage( "/t3d/frm" );
					p << mFrameId++ << mSerialNumber;
					p << osc::EndMessage;
						
					for(int voiceIdx : activeVoicesByPortOffset[portOffset])
					{
						OSCVoice* pVoice = &mOSCVoices[voiceIdx];
						if (!mKymaMode)
						{
							osc::int32 touchID = voiceIdx + 1; // 1-based for OSC

							// see if the voice is in this port offset group. if not, we know it moved out of this group, so turn it off.
							float zOut;
							if(portOffset != pVoice->portOffset1)
							{
								zOut = 0.;
							}
							else
							{
								zOut = pVoice->z;
							}
							
							std::string address("/t3d/tch" + std::to_string(touchID));
							p << osc::BeginMessage( address.c_str() );
							p << pVoice->x << pVoice->y << zOut << pVoice->note;
							p << osc::EndMessage;						
							
							
							if (pVoice->mState == kVoiceStateOn)
							{
								debug() << "ON: " << portOffset << "\n";
							}
							
							if (pVoice->mState == kVoiceStateOff)
							{
								debug() << "OFF: " << portOffset << "\n";
								pVoice->mState = kVoiceStateInactive;
							}
							
						}
						else
						{		
							osc::int32 offOn = 1;
							if(pVoice->mState == kVoiceStateOn)
							{
								offOn = -1;
							}
							else if (pVoice->mState == kVoiceStateOff)
							{
								offOn = 0; // TODO periodically turn off silent voices 
							}
							if(pVoice->mState != kVoiceStateInactive)
							{
								p << osc::BeginMessage( "/key" );	
								p << voiceIdx << offOn << pVoice->note << pVoice->z << pVoice->y;
								p << osc::EndMessage;
							}						
						}
					}
					
					// end bundle
					p << osc::EndBundle;
					
					// send it
					socket.Send( p.Data(), p.Size() );
					
				}
			}
			            
            // format and send matrix in OSC blob if we got one.
			// matrix is always sent to the default port. 
            if(mGotMatrixThisFrame)
            {
				osc::OutboundPacketStream& p = getPacketStreamForPort(0);					 
				UdpTransmitSocket& socket = getTransmitSocketForPort(0);
                p << osc::BeginMessage( "/t3d/matrix" );
                p << osc::Blob( &(msg->mMatrix), sizeof(msg->mMatrix) );
                p << osc::EndMessage;
                mGotMatrixThisFrame = false;
				socket.Send( p.Data(), p.Size() );
            }
        }
    }
}

