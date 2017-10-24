
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include "MLSignal.h"

//#include "MLDebug.h"
#define debug()	std::cout

#include <list>
#include <thread>
#include <mutex>
#include <array>
#include <bitset>

typedef enum
{
	xColumn = 0,
	yColumn = 1,
	zColumn = 2,
	dzColumn = 3,
	ageColumn = 4
} TouchSignalColumns;

constexpr int kSensorRows = 8;
constexpr int kSensorCols = 64;
constexpr int kKeyRows = 5;
constexpr int kKeyCols = 30;

MLSignal smoothPressure(const MLSignal& inSignal); 

class TouchTracker
{
public:
	
	static constexpr int kMaxTouches = 16; 
	
	class Touch
	{
	public:
		Touch() : x(0.f), y(0.f), z(0.f), dz(0.f), age(0){};
		Touch(float px, float py, float pz, float pdz, int page) : x(px), y(py), z(pz), dz(pdz), age(page){};
		float x;
		float y;
		float z;
		float dz;
		int age;
	};

	typedef std::array<Touch, kMaxTouches> TouchArray;
	
	class SensorFrame
	{
	public:
		float data[kSensorRows*kSensorCols];
		
	};
	
	TouchTracker(int w, int h);
	~TouchTracker();
	
	void setMaxTouches(int t);
	
	void clear();
	void setRotate(bool b);	
	void setSampleRate(float sr) { mSampleRate = sr; }
	void setThresh(float f);
	void setLopassZ(float k); 	
	
	// process input and get touches. creates one frame of touch data in buffer.
	void process(MLSignal* pIn, int maxTouches, TouchArray* pOut);
	
private:
	
	int mWidth;
	int mHeight;
	
	//
	int mMaxTouchesPerFrame;
	float mSampleRate;
	float mLopassZ;	
	
	float mFilterThreshold;
	float mOnThreshold;
	float mOffThreshold;
	
	MLSignal mFilteredInput;
	MLSignal mInput1;
	
	// sorted order of touches for hysteresis
	std::array<int, kMaxTouches> mTouchSortOrder;
	
	// touches

	TouchArray mTouches;
	
	// filter histories
	TouchArray mTouchesMatch1;
	TouchArray mTouches2; 

	
	std::array<int, kMaxTouches> mRotateShuffleOrder;
	bool mRotate;
	bool mPairs;
	
	int mCount; // debug frame counter
	
	TouchArray findTouches(const MLSignal& in);
	TouchArray rotateTouches(const TouchArray& t);
	TouchArray matchTouches(const TouchArray& x, const TouchArray& x1);
	TouchArray filterTouchesXYAdaptive(const TouchArray& x, const TouchArray& x1);
	TouchArray filterTouchesZ(const TouchArray& x, const TouchArray& x1, float upFreq, float downFreq);
	TouchArray exileUnusedTouches(const TouchArray& x1, const TouchArray& x2);
	TouchArray clampTouches(const TouchArray& x);
	void outputTouches(TouchArray touches);
	
	// unused
	TouchArray sortTouchesWithHysteresis(const TouchArray& t, std::array<int, kMaxTouches>& currentSortedOrder);
	TouchArray limitNumberOfTouches(const TouchArray& t);
	
	MLSignal getCurvatureX(const MLSignal& in);
	MLSignal getCurvatureY(const MLSignal& in);
	MLSignal getCurvatureXY(const MLSignal& in);

};

