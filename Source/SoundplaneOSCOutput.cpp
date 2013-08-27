
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneOSCOutput.h"

const char* kDefaultHostnameString = "localhost";

OSCVoice::OSCVoice() :
	mStartX(0), mStartY(0), mAge(0)
{
}

OSCVoice::~OSCVoice()
{
}

// --------------------------------------------------------------------------------
#pragma mark SoundplaneOSCOutput

SoundplaneOSCOutput::SoundplaneOSCOutput() :
	mDataFreq(250.),
	mLastTimeDataWasSent(0),
	mpUDPSocket(0),
	mFrameId(0),
	mSerialNumber(0),
	lastInfrequentTaskTime(0),
	mKymaMode(false)
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

void SoundplaneOSCOutput::processFrame(const MLSignal& touchFrame)
{
	if (!mActive) return;
	float x, y, z, note;
	UInt64 now = getMicroseconds();	
	const UInt64 dataPeriodMicrosecs = 1000*1000 / mDataFreq;
	bool sendData = false;
	
	// store touch ages and find note-ons, note-offs. 
	// if there are any note ons or offs, send this frame of data.
	for(int i=0; i<mVoices; ++i)
	{
		OSCVoice* pVoice = &mOSCVoices[i];
		pVoice->mNoteOn = false;
		pVoice->mNoteOff = false;
		int age = touchFrame(ageColumn, i);
		if (age == 1) // note-on
		{
			// always send note on 
			sendData = true;
			pVoice->mNoteOn = true;
			pVoice->mStartY = touchFrame(yColumn, i);
		}
		else if (pVoice->mAge && !age)
		{
			// always send note off
			sendData = true;	
			pVoice->mNoteOff = true;
		}
		pVoice->mAge = age;
	}
	
	// if not already sending data due to note on or off, look at the time
	// and decide to send based on that. 
	if (!sendData)
	{
		if (now > mLastTimeDataWasSent + (UInt64)dataPeriodMicrosecs)
		{
			mLastTimeDataWasSent = now;
			sendData = true;
		}
	}
	
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
			for(int i=0; i<mVoices; ++i)
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
			for(int i=0; i<mVoices; ++i)
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
		
			for(int i=0; i<mVoices; ++i)
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
