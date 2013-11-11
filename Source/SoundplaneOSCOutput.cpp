
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneOSCOutput.h"

const char* kDefaultHostnameString = "localhost";

OSCVoice::OSCVoice() : startX(0), startY(0), mState(kInactive)
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
	mpUDPSocket(0),
	mFrameId(0),
	mSerialNumber(0),
	lastInfrequentTaskTime(0),
	mKymaMode(false),
    mGotNoteChangesThisFrame(false)
{
}

SoundplaneOSCOutput::~SoundplaneOSCOutput()
{
	if (mpOSCBuf) delete[] mpOSCBuf;
}

void SoundplaneOSCOutput::initialize()
{
    mpUDPSocket = new UdpTransmitSocket( IpEndpointName( kDefaultHostnameString, kDefaultUDPPort ) );
	mpOSCBuf = new char[kUDPOutputBufferSize];
}

void SoundplaneOSCOutput::connect(const char* name, int port)
{
    if(mpUDPSocket)
	{
		try
		{
			mpUDPSocket->Connect(IpEndpointName(name, port));
		}
		catch(std::runtime_error err)
		{
			MLError() << "SoundplaneOSCOutput: error connecting to " << name << ", port " << port << "\n";
			return;
		}
		debug() << "SoundplaneOSCOutput:connected to " << name << ", port " << port << "\n";

	}
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

void SoundplaneOSCOutput::modelStateChanged()
{

}

void SoundplaneOSCOutput::doInfrequentTasks()
{
	if(!mpUDPSocket) return;
	if(!mpOSCBuf) return;
	osc::OutboundPacketStream p( mpOSCBuf, kUDPOutputBufferSize );
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
		mpUDPSocket->Send( p.Data(), p.Size() );
	}
	
	// send data rate to receiver
	p << osc::BeginBundleImmediate;
	p << osc::BeginMessage( "/t3d/dr" );	
	p << (osc::int32)mDataFreq;
	p << osc::EndMessage;
	p << osc::EndBundle;
	mpUDPSocket->Send( p.Data(), p.Size() );
}

void SoundplaneOSCOutput::notify(int connected)
{
	if(!mpUDPSocket) return;
	if(!mpOSCBuf) return;
	if(!mActive) return;
	osc::OutboundPacketStream p( mpOSCBuf, kUDPOutputBufferSize );
	p << osc::BeginBundleImmediate;
	p << osc::BeginMessage( "/t3d/con" );	
	p << (osc::int32)connected;
	p << osc::EndMessage;
	p << osc::EndBundle;
	mpUDPSocket->Send( p.Data(), p.Size() );
	//debug() << "SoundplaneOSCOutput::notify\n";
}

void SoundplaneOSCOutput::processMessage(const SoundplaneDataMessage* msg)
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
    static const MLSymbol endFrameSym("end_frame");
    static const MLSymbol matrixSym("matrix");
    static const MLSymbol nullSym;
    
	if (!mActive) return;
    MLSymbol type = msg->mType;
    MLSymbol subtype = msg->mSubtype;
    
    int i;
	float x, y, z, note;
    
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
    }
    else if(type == touchSym)
    {
        // get touch data
        i = msg->mData[0];
        x = msg->mData[1];
        y = msg->mData[2];
        z = msg->mData[3];
        note = msg->mData[4];
        OSCVoice* pVoice = &mOSCVoices[i];        
        pVoice->x = x;
        pVoice->y = y;
        pVoice->z = z;
        pVoice->note = note;
        
//debug() << "SoundplaneOSCOutput touch: " ;
        
        if(subtype == onSym)
        {
//debug() << " ON  ";
            pVoice->startX = x;
            pVoice->startY = y;
            pVoice->mState = kOn;
            mGotNoteChangesThisFrame = true;
        }
        if(subtype == continueSym)
        {
//debug() << " ... ";
            pVoice->mState = kActive;
        }
        if(subtype == offSym)
        {
//debug() << " OFF ";
            pVoice->mState = kOff;
            pVoice->z = 0;
            mGotNoteChangesThisFrame = true;
        }
//debug() << i << " " << note << "\n";
    }
    else if(type == controllerSym)
    {
        // when a controller message comes in, make a local copy of the message and store by zone ID.
        int zoneID = msg->mData[0];
        mMessagesByZone[zoneID] = *msg;
    }
    else if(type == matrixSym)
    {
        // format and send matrix in OSC blob
        if(mTimeToSendNewFrame)
        {
            osc::OutboundPacketStream p( mpOSCBuf, kUDPOutputBufferSize );            
            p << osc::BeginMessage( "/t3d/matrix" );
            p << osc::Blob( &(msg->mMatrix), sizeof(msg->mMatrix) );
            p << osc::EndMessage;
            mpUDPSocket->Send( p.Data(), p.Size() );
        }
    }
    else if(type == endFrameSym)
    {
        if(mGotNoteChangesThisFrame || mTimeToSendNewFrame)
        {
            // begin OSC bundle for this frame
            osc::OutboundPacketStream p( mpOSCBuf, kUDPOutputBufferSize );
            
            // timestamp is now stored in the bundle, synchronizing all info for this frame.
            p << osc::BeginBundle(mCurrFrameStartTime);
            
			// send frame message
			// /k1/frm frameID serialNumber
			//
			p << osc::BeginMessage( "/t3d/frm" );
			p << mFrameId++ << mSerialNumber;
			p << osc::EndMessage;
            
            // for each zone, send and clear any controller messages received since last frame
            for(int i=0; i<kSoundplaneAMaxZones; ++i)
            {
                SoundplaneDataMessage* pMsg = &(mMessagesByZone[i]);
                if(pMsg->mType == controllerSym)
                {
                    // send controller message: /t3d/[zoneName] val1 (val2)
                    // TODO allow zones to split touches and controls across different ports
                    // using the channel attribute. (channel = port number offset for OSC)
                    int channel = pMsg->mData[1];
                    x = pMsg->mData[4];
                    y = pMsg->mData[5];
                    std::string ctrlStr("/");
                    ctrlStr += *(pMsg->mZoneName);
                    
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
                    p << osc::EndMessage;
                    
                    // clear
                    mMessagesByZone[i].mType = nullSym;
                }
            }
            // send notes, either in Kyma mode, or not
            if (!mKymaMode)
            {
                // send 1 message for each live touch: /t3d/tch[touchID] x y z note
                // age is not sent over OSC-- to be reconstructed on the receiving end if needed.
                //
                for(int i=0; i<mMaxTouches; ++i)
                {
                    OSCVoice* pVoice = &mOSCVoices[i];
                    if(pVoice->mState != kInactive)
                    {
                        osc::int32 touchID = i + 1; // 1-based for OSC
                        std::string address("/t3d/tch");
                        int maxSize = 4;
                        char idBuf[maxSize];
                        snprintf(idBuf, maxSize, "%d", touchID);                                 
                        address += std::string(idBuf);
                        p << osc::BeginMessage( address.c_str() );
                        p << pVoice->x << pVoice->y << pVoice->z << pVoice->note;
                        p << osc::EndMessage;
                    }
                }               
            }
            else // kyma
            {
                for(int i=0; i<mMaxTouches; ++i)
                {
                    OSCVoice* pVoice = &mOSCVoices[i];			
                    osc::int32 touchID = i; // 0-based for Kyma
                    osc::int32 offOn = 1;
                    if(pVoice->mState == kOn)
                    {
                        offOn = -1;
                    }
                    else if (pVoice->mState == kOff)
                    {
                        offOn = 0; // TODO periodically turn off silent voices 
                    }
                
                    if(pVoice->mState != kInactive)
                    {
                        p << osc::BeginMessage( "/key" );	
                        p << touchID << offOn << pVoice->note << pVoice->z << pVoice->y ;
                        p << osc::EndMessage;
                    }
                }
            }
            
            // end OSC bundle and send
            p << osc::EndBundle;
            mpUDPSocket->Send( p.Data(), p.Size() );
        }
    }
}


//
/*
 void SoundplaneOSCOutput::processFrame(const MLSignal& touchFrame)
    {
	if (!mActive) return;
	float x, y, z, note;
	UInt64 now = getMicroseconds();	
	const UInt64 dataPeriodMicrosecs = 1000*1000 / mDataFreq;
	
	if (sendData) 
	{
		if (!mKymaMode)
		{
			osc::OutboundPacketStream p( mpOSCBuf, kUDPOutputBufferSize );
			
			p << osc::BeginBundleImmediate;
		
			// send frame message
			// /k1/frm frameID timestamp serialNumber
			//
			p << osc::BeginMessage( "/t3d/frm" );	
			
			// time val for sending in osc signed int32. 
			// Will wrap every 2<<31 / 1000000 seconds. 
			// That's only 35 minutes, so clients need to handle wrapping.
			UInt32 now31 = now & 0x7FFFFFFF;	
			
			p << mFrameId++ << (osc::int32)now31 << mSerialNumber;			
			p << osc::EndMessage;
			
			// send 1 message for each live touch.
			// k1/touch touchID x y z zone [...]
			// age is not sent-- to be reconstructed on the receiving end if needed. 
			//
			for(int i=0; i<mMaxTouches; ++i)
			{
				OSCVoice* pVoice = &mOSCVoices[i];
				x = touchFrame(xColumn, i);
				y = touchFrame(yColumn, i);
				z = touchFrame(zColumn, i);
				note = touchFrame(noteColumn, i);
				
				osc::int32 touchID = i + 1; // 1-based for OSC
				if (pVoice->mAge > 0)
				{
					// start or continue touch
					p << osc::BeginMessage( "/t3d/tch" );	
					// send data. any additional quantites could follow.
					p << touchID << x << y << z << note;
					p << osc::EndMessage;
				}
				else if(pVoice->mNoteOff)
				{
					// send touch off, just a normal frame except z is guaranteed to be 0.
					z = 0;
					p << osc::BeginMessage( "/t3d/tch" );	
					// send data. any additional quantites could follow.
					p << touchID << x << y << z << note;
					p << osc::EndMessage;
				}				
			}
			
			// TODO /t3d/raw for matrix data

			// send list of live touch IDs
			//
			p << osc::BeginMessage( "/t3d/alv" );	
			for(int i=0; i<mMaxTouches; ++i)
			{
				OSCVoice* pVoice = &mOSCVoices[i];
				if (pVoice->mAge > 0)
				{
					p << (osc::int32)(i + 1); // 1-based for OSC
				}
			}
			p << osc::EndMessage;					
			p << osc::EndBundle;			
			mpUDPSocket->Send( p.Data(), p.Size() );
		}
		else // kyma
		{
			osc::OutboundPacketStream p( mpOSCBuf, kUDPOutputBufferSize );
			p << osc::BeginBundleImmediate;
		
			for(int i=0; i<mMaxTouches; ++i)
			{
				OSCVoice* pVoice = &mOSCVoices[i];
				x = touchFrame(xColumn, i);
				y = touchFrame(yColumn, i);
				z = touchFrame(zColumn, i);
				note = touchFrame(noteColumn, i);				
				osc::int32 touchID = i; // 0-based for Kyma
				osc::int32 offOn = 1;
				if(pVoice->mNoteOn)
				{
					offOn = -1;
				}
				else if (pVoice->mNoteOff)
				{
					offOn = 0; // TODO periodically turn off silent voices 
				}
				
				if(pVoice->mAge || pVoice->mNoteOff)
				{
					p << osc::BeginMessage( "/key" );	
					// send data. any additional quantites could follow.
					p << touchID << offOn << note << z << y ;
					p << osc::EndMessage;
				}
			}
			p << osc::EndBundle;
			mpUDPSocket->Send( p.Data(), p.Size() );
		}
	}
		
	if(now - lastInfrequentTaskTime > 4*1000*1000)
	{
		doInfrequentTasks();
		lastInfrequentTaskTime = now;
	}
}
*/
