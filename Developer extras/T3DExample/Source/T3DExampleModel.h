
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __T3D_EXAMPLE_MODEL__
#define __T3D_EXAMPLE_MODEL__

#include "MLTime.h"
#include "MLModel.h"
#include "MLNetServiceHub.h"
#include "MLOSCListener.h"
#include "MLSymbol.h"
#include "MLParameter.h"
#include <list>
#include <map>

const int kMaxTouches = 10;

class ExampleTouch
{
public:
    ExampleTouch() : x(0), y(0), z(0) {}
    ~ExampleTouch() {}
    float x;
    float y;
    float z;
    float note;
};

class T3DExampleModel :
	public MLNetServiceHub,
    public MLOSCListener,
	public MLModel
{

public:
	T3DExampleModel();
	~T3DExampleModel();	
	
	void initialize();
    void clear();

	// MLOSCListener
	void ProcessBundle(const osc::ReceivedBundle& b, const IpEndpointName& remoteEndpoint);
	void ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint);

private:
	int mUDPPortNum;
	int mT3DWaitTime;
	bool mT3DConnected;
    
    ExampleTouch mTouches[kMaxTouches];
};


#endif // __T3D_EXAMPLE_MODEL__