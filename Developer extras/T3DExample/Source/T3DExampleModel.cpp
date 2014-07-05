
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "T3DExampleModel.h"

void *T3DExampleModelProcessThreadStart(void *arg);

T3DExampleModel::T3DExampleModel() :
    mUDPPortNum(3123),
    mT3DConnected(0),
    mT3DWaitTime(0)
{
}

T3DExampleModel::~T3DExampleModel()
{
}

void T3DExampleModel::initialize()
{
    // TODO send variable port number to engine
	publishUDPService("T3D Example", mUDPPortNum);
	
	// setup listener thread
	//
	listenToOSC(mUDPPortNum);
}

void T3DExampleModel::clear()
{
}

//--------------------------------------------------------------------------------
#pragma mark MLOSCListener

void T3DExampleModel::ProcessBundle( const osc::ReceivedBundle& b,
    const IpEndpointName& remoteEndpoint )
{
	// process all messages in bundle
	//
	for( osc::ReceivedBundle::const_iterator i = b.ElementsBegin(); i != b.ElementsEnd(); ++i )
	{
		if( i->IsBundle() )
			ProcessBundle( osc::ReceivedBundle(*i), remoteEndpoint );
		else
			ProcessMessage( osc::ReceivedMessage(*i), remoteEndpoint );
	}
    
	// write frame of touches to synthesizer or whatever
	//
    bool anyTouches = false;
	for(int i = 0; i < kMaxTouches; ++i)
    {
        if(mTouches[i].z > 0)
        {
            anyTouches = true;
            float x = mTouches[i].x;
            float y = mTouches[i].y;
            float z = mTouches[i].z;
            debug() << std::setprecision(2);
            debug() << "[t" << i << ": " << x << ", " << y << ", " << z << "]";
        }
    }
    if(anyTouches)
    {
        debug() << "\n";
    }
}

void T3DExampleModel::ProcessMessage( const osc::ReceivedMessage& msg,
    const IpEndpointName&  )
{
	osc::TimeTag frameTime;
	osc::int32 timestamp, frameID, touchID, deviceID;
	float x, y, z, note;
	int alive[kMaxTouches] = {0};
	
	try
	{
		osc::ReceivedMessageArgumentStream args = msg.ArgumentStream();
		const char * addy = msg.AddressPattern();
		
		if ( strcmp( addy, "/t3d/frm" )==0)
		{
			// frame message, read time and so on
			// /t3d/frm (int)frameID (int)time (int)deviceID
			args >> frameID >> timestamp >> deviceID;
            //debug() << "FRM " << frameID << "\n";
		}
		else if (strcmp(addy,"/t3d/tch")==0)
		{
			// t3d/tch (int)touchID, (float)x, (float)y, (float)z, (float)note
			args >> touchID >> x >> y >> z >> note;
			touchID -= 1;
			
            //debug() << "TCH " << touchID << " " << x << " " << y << " " << z << " " << note << "\n";
            if(touchID < kMaxTouches)
            {
                mTouches[touchID].x = x;
                mTouches[touchID].y = y;
                mTouches[touchID].z = z;
                mTouches[touchID].note = note;
            }
		}
		else if (strcmp(addy,"/t3d/alv")==0)
		{
			osc::ReceivedMessage::const_iterator it = msg.ArgumentsBegin();
			// alive message
            //debug() << "ALV " ;
            
			// get all args in message and turn off touches that are not alive.
			for(int i=0; i < kMaxTouches; ++i)
			{
				alive[i] = 0;
			}
			
			int liveCount = 0;
			while(it != msg.ArgumentsEnd())
			{
				int t = it->AsInt32();
				int voice = t - 1;
				if ((voice < kMaxTouches) && (voice >= 0))
				{
					alive[voice] = 1;
				}
				it++;
				liveCount++;
			}
			
			// turn off deadbeats
			for(int i=0; i < kMaxTouches; ++i)
			{
				int a = alive[i];
				if (!a)
				{
                    mTouches[i].z = 0.f;
				}
			}
		}
		// receive data rate
		else if (strcmp(addy,"/t3d/dr")==0)
		{
			osc::int32 r;
			args >> r;
			setModelParam("data_rate", r);
		}
		else
		{
            //debug() << "osc:" << addy << "\n";
		}
	}
	catch( osc::Exception& e )
	{
		MLError() << "error parsing t3d message: " << e.what() << "\n";
	}
}
