
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include "SensorFrame.h"
#include "Touch.h"

using namespace std::chrono;

class TouchTracker
{
public:
	
	TouchTracker();
	~TouchTracker();
	
	void clear();
	void setRotate(bool b);
	void setThresh(float f);
	void setLopassZ(float k);
	
	// preprocess input to get curvature
	SensorFrame preprocess(const SensorFrame& in);
	
	// process input and get touches. returns one frame of touch data. changes history of many filters.
	TouchArray process(const SensorFrame& in, int maxTouches);
	
	TouchArray getTestTouches(time_point<system_clock> t, int maxTouches);
	
private:
	
	float mSampleRate;
	int mMaxTouchesPerFrame;
	bool mClearNextFrame{false};
	float mLopassZ;
	bool mRotate;
	
	float mFilterThreshold;
	float mOnThreshold;
	float mOffThreshold;
	
	SensorFrame mInput{};
	SensorFrame mInputZ1{};
	
	TouchArray mTouches{};
	TouchArray mTouchesMatch1{};
	TouchArray mTouches2{};
	
	std::array<int, kMaxTouches> mRotateShuffleOrder;
	
	void clearAndSendNextFrameIfNeeded();
	void setMaxTouches(int t);
	TouchArray findTouches(const SensorFrame& in);
	TouchArray rotateTouches(const TouchArray& t);
	TouchArray matchTouches(const TouchArray& x, const TouchArray& x1);
	TouchArray filterTouchesXYAdaptive(const TouchArray& x, const TouchArray& x1);
	TouchArray filterTouchesZ(const TouchArray& x, const TouchArray& x1, float upFreq, float downFreq);
	TouchArray exileUnusedTouches(const TouchArray& x1, const TouchArray& x2);
	TouchArray clampAndScaleTouches(const TouchArray& x);
	void outputTouches(TouchArray touches);
};

