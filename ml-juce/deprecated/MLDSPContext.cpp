
// MadronaLib: a C++ framework for DSP applications.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "MLDSPContext.h"

using namespace ml;

#pragma mark MLDSPContext

MLDSPContext::MLDSPContext() : 
	mMaxVoices(1),
	mEnabled(false),
	mpRootContext(nullptr)
{
	mVectorSize = 0;		
	mSampleRate = kToBeCalculated;	
	mInvSampleRate = 1.f;
	mNullInput.setToConstant(0.f);
}

MLDSPContext::~MLDSPContext()
{
}

int MLDSPContext::setVectorSize(unsigned newSize)
{	
	int retErr = 0;
	mVectorSize = newSize;
	mNullInput.setDims(newSize);
	mNullOutput.setDims(newSize);
	return retErr;
}

void MLDSPContext::setSampleRate(float newRate)
{	
	mSampleRate = newRate;
	mNullInput.setRate(newRate);
	mNullOutput.setRate(newRate);
	mInvSampleRate = 1.0f / mSampleRate;
}

ml::Matrix& MLDSPContext::getNullInput()
{
	return mNullInput;
}

ml::Matrix& MLDSPContext::getNullOutput()
{
	return mNullOutput;
}

ml::Time MLDSPContext::getTime() 
{ 
	return mClock.now(); 
}

