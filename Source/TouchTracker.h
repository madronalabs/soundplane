
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include "MLSignal.h"

//#include "MLDebug.h"
#define debug()	std::cout

//#include "pa_ringbuffer.h"

#include <list>
#include <thread>
#include <mutex>
#include <array>
#include <bitset>

const int kTouchWidth = 8; // 8 columns in touch data: [x, y, z, dz, age, dt, note, ?] for each touch.
typedef enum
{
	xColumn = 0,
	yColumn = 1,
	zColumn = 2,
	ageColumn = 3
} TouchSignalColumns;

//std::ostream& operator<< (std::ostream& out, const Touch & t);

constexpr int kSensorRows = 8;
constexpr int kSensorCols = 64;

constexpr int kKeyRows = 5;
constexpr int kKeyCols = 30;

template <size_t ARRAYS, size_t ARRAY_LENGTH >
struct VectorArray2D
{ 
	std::array< std::array<Vec4, ARRAY_LENGTH>, ARRAYS > data;
};

class TouchTracker
{
public:
	static constexpr int kMaxTouches = 10; 
 
	TouchTracker(int w, int h);
	~TouchTracker();
	
	void setInputSignal(MLSignal* pIn);
	void setOutputSignal(MLSignal* pOut);
	void setMaxTouches(int t);
	void makeTemplate();
	
	void clear();
	void setSampleRate(float sr) { mSampleRate = sr; }
	void setThresh(float f);
	void setLoThresh(float f);
	void setLopassXY(float k); 	
	void setLopassZ(float k); 	
	
	// process input and get touches. creates one frame of touch data in buffer.
	void process(int);
	
	void setRotate(bool b);
	void setPairs(bool b);
	
	typedef std::bitset<kSensorRows*kSensorCols> SensorBitsArray;	
	typedef VectorArray2D<kSensorRows, kSensorCols> VectorsH;
	typedef VectorArray2D<kSensorCols, kSensorRows> VectorsV;
	typedef VectorArray2D<kKeyRows, kKeyCols> KeyStates;
	
	// returning lots of things by value - 
	// TODO these should use time-stamped ringbuffers to communicate with views
	
	const MLSignal& getCalibratedSignal() { std::lock_guard<std::mutex> lock(mCalibratedSignalMutex);  return mCalibratedSignal; } 
	const MLSignal& getSmoothedSignal() { std::lock_guard<std::mutex> lock(mSmoothedSignalMutex);  return mSmoothedSignal; } 

	SensorBitsArray getThresholdBits() { std::lock_guard<std::mutex> lock(mThresholdBitsMutex); return mThresholdBitsOut; }

	VectorsH getPingsHorizRaw() { std::lock_guard<std::mutex> lock(mPingsHorizRawOutMutex); return mPingsHorizRawOut; }
	VectorsH getPingsHoriz() { std::lock_guard<std::mutex> lock(mPingsHorizOutMutex); return mPingsHorizOut; }
	
	VectorsV getPingsVertRaw() { std::lock_guard<std::mutex> lock(mPingsVertRawOutMutex); return mPingsVertRawOut; }	
	VectorsV getPingsVert() { std::lock_guard<std::mutex> lock(mPingsVertOutMutex); return mPingsVertOut; }	

	KeyStates getKeyStates() { std::lock_guard<std::mutex> lock(mKeyStatesOutMutex); return mKeyStatesOut; }

	std::array<Vec4, kMaxTouches> getRawTouches() { std::lock_guard<std::mutex> lock(mTouchesRawOutMutex); return mTouchesRawOut; }
		
	std::array<Vec4, kMaxTouches> getTouches() { std::lock_guard<std::mutex> lock(mTouchesOutMutex); return mTouchesOut; }
	
private:
	
	int mWidth;
	int mHeight;
	
	MLSignal* mpIn;
	MLSignal* mpOut;
	
	int mMaxTouchesPerFrame;
	float mSampleRate;
	float mLopassXY;
	float mLopassZ;	
	
	float mFilterThreshold;
	float mOnThreshold;
	float mOffThreshold;
	float mLoPressureThreshold;
	
	MLSignal mFilteredInput;
	
	MLSignal mCalibratedSignal;
	std::mutex mCalibratedSignalMutex;
	
	MLSignal mSmoothedSignal;
	std::mutex mSmoothedSignalMutex;
	
	SensorBitsArray mThresholdBits;
	SensorBitsArray mThresholdBitsOut;
	std::mutex mThresholdBitsMutex;	
	
	// a ping is a guess at where a touch is over a particular row or column.
	VectorsH mPingsHoriz;
	VectorsH mPingsHorizOut;
	std::mutex mPingsHorizOutMutex;	
	VectorsH mPingsHorizRaw;
	VectorsH mPingsHorizRawOut;
	std::mutex mPingsHorizRawOutMutex;	
	VectorsV mPingsVertRaw;
	VectorsV mPingsVertRawOut;
	std::mutex mPingsVertRawOutMutex;	
	VectorsV mPingsVert;
	VectorsV mPingsVertOut;
	std::mutex mPingsVertOutMutex;	
	
	// key states
	KeyStates mKeyStates;
	KeyStates mKeyStatesOut;
	std::mutex mKeyStatesOutMutex;	
	
	// sorted order of touches for hysteresis
	std::array<int, TouchTracker::kMaxTouches> mTouchSortOrder;
	
	// touches
	std::array<Vec4, kMaxTouches> mTouchesRaw;
	std::array<Vec4, kMaxTouches> mTouchesRawOut;
	std::mutex mTouchesRawOutMutex;	
	std::array<Vec4, kMaxTouches> mTouches;
	
	// filter histories
	std::array<Vec4, kMaxTouches> mTouchesMatch1;
	std::array<Vec4, kMaxTouches> mTouches2; 
	
	std::array<Vec4, kMaxTouches> mTouchesOut;
	std::mutex mTouchesOutMutex;	
	
	std::array<int, kMaxTouches> mRotateShuffleOrder;
	bool mRotate;
	bool mPairs;
	
	int mCount; // debug frame counter
	
	SensorBitsArray findThresholdBits(const MLSignal& in);
	
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findPings(const SensorBitsArray& inThresh, const MLSignal& inSignal);

	VectorsH correctPingsH(const VectorsH& pings);	
	VectorsV correctPingsV(const VectorsV& pings);
	
	KeyStates pingsToKeyStates(const VectorsH& pingsHoriz, const VectorsV& pingsVert);
	
	std::array<Vec4, kMaxTouches> findTouches(const KeyStates& keyStates);

	std::array<Vec4, kMaxTouches> reduceCrowdedTouches(const std::array<Vec4, kMaxTouches>& t);

	std::array<Vec4, kMaxTouches> rotateTouches(const std::array<Vec4, kMaxTouches>& t);
	std::array<Vec4, kMaxTouches> createPairsV(const std::array<Vec4, kMaxTouches>& t);
	std::array<Vec4, kMaxTouches> createPairsH(const std::array<Vec4, kMaxTouches>& t);

	std::array<Vec4, kMaxTouches> matchTouches(const std::array<Vec4, kMaxTouches>& x, const std::array<Vec4, kMaxTouches>& x1);
	
	std::array<Vec4, kMaxTouches> filterTouchesXYAdaptive(const std::array<Vec4, kMaxTouches>& x, const std::array<Vec4, kMaxTouches>& x1);
	std::array<Vec4, kMaxTouches> filterTouchesZ(const std::array<Vec4, kMaxTouches>& x, const std::array<Vec4, kMaxTouches>& x1, float upFreq, float downFreq);

	std::array<Vec4, kMaxTouches> exileUnusedTouches(const std::array<Vec4, kMaxTouches>& x1, const std::array<Vec4, kMaxTouches>& x2);

	std::array<Vec4, kMaxTouches> clampTouches(const std::array<Vec4, kMaxTouches>& x);
	
	void outputTouches(std::array<Vec4, TouchTracker::kMaxTouches> touches);
	
	// unused
	std::array<Vec4, kMaxTouches> sortTouchesWithHysteresis(const std::array<Vec4, kMaxTouches>& t, std::array<int, TouchTracker::kMaxTouches>& currentSortedOrder);
	std::array<Vec4, kMaxTouches> limitNumberOfTouches(const std::array<Vec4, kMaxTouches>& t);
	

};


