
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __FILTERS2D__
#define __FILTERS2D__

#include "MLSignal.h"
#include "MLDebug.h"
#include "MLDSPUtils.h"

// quick and dirty filters code, for when we are not including the whole signal library.

// a biquad filter with scalar coefficients.
class Biquad
{
public:
	Biquad();	
	~Biquad();
	
	MLBiquad mCoeffs;
	
	MLSample mIn, mX1, mX2, mY1, mY2;	

	MLSample* mpIn;
	MLSample* mpOut;
	
	void setInput(MLSample* pIn)
		{ mpIn = pIn; }
	void setOutput(MLSample* pOut)
		{ mpOut = pOut; }
	float getOutput() { return *mpOut; }
	void clear() 
	{
		mIn = mX1 = mX2 = mY1 = mY2 = 0.f;
	}
	void setSampleRate(float sr) 
		{ mCoeffs.setSampleRate(sr); }
	void setNotch(float f, float q)
		{ mCoeffs.setNotch(f, q); }
	void setLopass(float f, float q)
		{ mCoeffs.setLopass(f, q); }
	void setHipass(float f, float q)
		{ mCoeffs.setHipass(f, q); }
	void setOnePole(float f)
		{ mCoeffs.setOnePole(f); }
	void setDifferentiate(void)
		{ mCoeffs.setDifferentiate(); }

	void setState(float f);
	void process(int frames);

	void dump();
	void dumpCoeffs();
};


// a biquad filter applied to each cell of a 2d signal with scalar coefficients.
class Biquad2D
{
public:
	Biquad2D(int w = 0, int h = 0);	
	~Biquad2D();
	
	void setDims(int w, int h);
	
	MLBiquad mCoeffs;
	MLSignal mIn;
	MLSignal mX1;
	MLSignal mX2;
	MLSignal mY1;
	MLSignal mY2;
	MLSignal mTemp1;
	MLSignal mAccum;
	const MLSignal* mpIn;
	MLSignal* mpOut;
	
	void setInputSignal(const MLSignal* pIn)
		{ mpIn = pIn; }
	void setOutputSignal(MLSignal* pOut)
		{ mpOut = pOut; }
	void clear() 
	{
		mIn.clear(); 
		mX1.clear(); 
		mX2.clear(); 
		mY1.clear(); 
		mY2.clear(); 
		mTemp1.clear(); 
		mAccum.clear(); 
	}
	void setSampleRate(float sr) 
		{ mCoeffs.setSampleRate(sr); }
	void setNotch(float f, float q)
		{ mCoeffs.setNotch(f, q); }
	void setLopass(float f, float q)
		{ mCoeffs.setLopass(f, q); }
	void setHipass(float f, float q)
		{ mCoeffs.setHipass(f, q); }
	void setOnePole(float f)
		{ mCoeffs.setOnePole(f); }
	void setDifferentiate(void)
		{ mCoeffs.setDifferentiate(); }

	void process(int frames);
	void dumpCoeffs();
};

// a biquad filter with matrix coefficients.
class Biquad2DMatrix
{
public:
	Biquad2DMatrix(int w = 0, int h = 0);
	~Biquad2DMatrix();
	void setDims(int w, int h);
	
	MLBiquad mCoeffs;
	MLSignal mCoeffsMatrix;
	MLSignal mIn;
	MLSignal mX1;
	MLSignal mX2;
	MLSignal mY1;
	MLSignal mY2;
	MLSignal mA0;
	MLSignal mA1;
	MLSignal mA2;
	MLSignal mB1;
	MLSignal mB2;
	MLSignal mTemp1;
	MLSignal mAccum;
	const MLSignal* mpIn;
	MLSignal* mpOut;
	
	void setInputSignal(const MLSignal* pIn)
		{ mpIn = pIn; }
	void setOutputSignal(MLSignal* pOut)
		{ mpOut = pOut; }
	void clear() 
	{
		mIn.clear(); 
		mX1.clear(); 
		mX2.clear(); 
		mY1.clear(); 
		mY2.clear(); 
		mTemp1.clear(); 
		mAccum.clear(); 
	}
	void setSampleRate(float sr) 
		{ mCoeffs.setSampleRate(sr); }
	void setLopass(const MLSignal& fMatrix, float q);
	void setHipass(const MLSignal& fMatrix, float q);
	void setOnePole(const MLSignal& fMatrix);
	void setDifferentiate(void);
	void process(int frames);
};

// a one pole filter with matrix coefficients and different decays
// depending on whether the signal input is rising or falling
class AsymmetricOnepoleMatrix
{
public:
	AsymmetricOnepoleMatrix(int w = 0, int h = 0);
	~AsymmetricOnepoleMatrix();
	void setDims(int w, int h);

	MLSignal mY1;
	MLSignal mDx;
	MLSignal mSign;
	MLSignal mK;
	MLSignal mA;
	MLSignal mB;
	const MLSignal* mpIn;
	MLSignal* mpOut;
	float mSampleRate;
	
	void setInputSignal(const MLSignal* pIn)
		{ mpIn = pIn; }
	void setOutputSignal(MLSignal* pOut)
		{ mpOut = pOut; }
	void clear() 
	{
		mY1.clear(); 
	}
	void setSampleRate(float sr) { mSampleRate = sr; }
	void setCoeffs(const MLSignal& fa, const MLSignal& fb);
	void process(int frames);
};

// a one pole filter with matrix coefficients 
class OnepoleMatrix
{
public:
	OnepoleMatrix(int w = 0, int h = 0);
	~OnepoleMatrix();
	void setDims(int w, int h);

	MLSignal mY1;
	MLSignal mDx;
	MLSignal mA;
	const MLSignal* mpIn;
	MLSignal* mpOut;
	float mSampleRate;
	
	void setInputSignal(const MLSignal* pIn)
		{ mpIn = pIn; }
	void setOutputSignal(MLSignal* pOut)
		{ mpOut = pOut; }
	void clear() 
	{
		mY1.clear(); 
	}
	void setSampleRate(float sr) { mSampleRate = sr; }
	void setCoeffs(const MLSignal& fa);
	void process(int frames);
};

#endif // __FILTERS2D__
