
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

const int kTemplateRadius = 3;
const int kTemplateSize = kTemplateRadius*2 + 1;
const int kTouchHistorySize = 128;
const int kTouchTrackerMaxPeaks = 16;

const int kPassesToCalibrate = 2;
const float kNormalizeThresh = 0.125;
const int kNormMapSamples = 2048;

// Soundplane A
const int kCalibrateWidth = 64;
const int kCalibrateHeight = 8;
const int kTrackerMaxTouches = 16;

const int kPendingTouchFrames = 10;
const int kTouchReleaseFrames = 100;
const int kAttackFrames = 100;
const int kMaxPeaksPerFrame = 4;

// new constants
const float kSpanThreshold = 0.002f;
//const int kTemplateSpanWidth = 9;

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
	static constexpr int kMaxSpansPerRow = 32;
	static constexpr int kMaxSpansPerCol = 4;
	static constexpr int kMaxTouches = 10; 
	
	class Listener
	{
	public:
		Listener() {}
		virtual ~Listener() {}
		virtual void hasNewCalibration(const MLSignal& cal, const MLSignal& norm, float avg) = 0;
	};
	
	
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
	void setTaxelsThresh(int t) { mTaxelsThresh = t; }
	void setQuantize(bool q) { mQuantizeToKey = q; }
	void setLopassXY(float k); 	
	void setLopassZ(float k); 	
	void setForceCurve(float f) { mForceCurve = f; }
	void setZScale(float f) { mZScale = f; }
	
	// process input and get touches. creates one frame of touch data in buffer.
	void process(int);
	
	

	void setListener(Listener* pL) { mpListener = pL; }
	void setDefaultNormalizeMap();
	void setRotate(bool b);
	void setUseTestSignal(bool b) { mUseTestSignal = b; }
	void doNormalize(bool b) { mDoNormalize = b; }
	
	void setSpanCorrect(float v) { mSpanCorrect = v; }
	
	// returning by value - MLTEST
	// these should use time-stamped ringbuffers to communicate with views
	
	typedef std::bitset<kSensorRows*kSensorCols> SensorBitsArray;	
	typedef VectorArray2D<kSensorRows, kSensorCols> VectorsH;
	typedef VectorArray2D<kSensorCols, kSensorRows> VectorsV;
	typedef VectorArray2D<kKeyRows, kKeyCols> KeyStates;
	

	const MLSignal& getCalibratedSignal() { std::lock_guard<std::mutex> lock(mCalibratedSignalMutex);  return mCalibratedSignal; } 

	SensorBitsArray getThresholdBits() { std::lock_guard<std::mutex> lock(mThresholdBitsMutex); return mThresholdBitsOut; }

	VectorsH getPingsHorizRaw() { std::lock_guard<std::mutex> lock(mPingsHorizRawOutMutex); return mPingsHorizRawOut; }
	VectorsH getPingsHoriz() { std::lock_guard<std::mutex> lock(mPingsHorizOutMutex); return mPingsHorizOut; }
	VectorsH getClustersHoriz() { std::lock_guard<std::mutex> lock(mClustersHorizOutMutex); return mClustersHorizOut; }
	VectorsH getClustersHorizRaw() { std::lock_guard<std::mutex> lock(mClustersHorizRawOutMutex); return mClustersHorizRawOut; }
	
	VectorsV getPingsVertRaw() { std::lock_guard<std::mutex> lock(mPingsVertRawOutMutex); return mPingsVertRawOut; }	
	VectorsV getPingsVert() { std::lock_guard<std::mutex> lock(mPingsVertOutMutex); return mPingsVertOut; }	
	VectorsV getClustersVert() { std::lock_guard<std::mutex> lock(mClustersVertOutMutex); return mClustersVertOut; }
	VectorsV getClustersVertRaw() { std::lock_guard<std::mutex> lock(mClustersVertRawOutMutex); return mClustersVertRawOut; }

	KeyStates getKeyStates() { std::lock_guard<std::mutex> lock(mKeyStatesOutMutex); return mKeyStatesOut; }

	std::array<Vec4, kMaxTouches> getRawTouches() { std::lock_guard<std::mutex> lock(mTouchesRawOutMutex); return mTouchesRawOut; }
		
	std::array<Vec4, kMaxTouches> getTouches() { std::lock_guard<std::mutex> lock(mTouchesOutMutex); return mTouchesOut; }
	
private:	

	// Touch class for internal use.
	// Externally, only x y z and age are relevant so a Vec4 is used for brevity.
	class Touch
	{
	public:	
		Touch() : x(0.f), y(0.f), z(0.f), age(0), minDist(MAXFLOAT), currIdx(-1), prevIdx(-1), occupied(false) {}
		Touch(float px, float py, float pz, int pa) : x(px), y(py), z(pz), age(pa), minDist(MAXFLOAT), currIdx(-1), prevIdx(-1), occupied(false) {}
				
		float x;
		float y;
		float z;
		int age;
		float minDist;
		int currIdx;
		int prevIdx;
		bool occupied;
	};
	
	Touch vec4ToTouch(Vec4 v)
	{
		return Touch(v.x(), v.y(), v.z(), static_cast<int>(v.w()));
	}
	
	Vec4 touchToVec4(Touch t)
	{
		return Vec4(t.x, t.y, t.z, static_cast<float>(t.age));
	}
	
	Listener* mpListener;
	
	void dumpTouches();
	int countActiveTouches();
	
	int mWidth;
	int mHeight;
	MLSignal* mpIn;
	MLSignal* mpOut;
	
	float mSampleRate;
	float mLopassXY;
	float mLopassZ;
	
	bool mQuantizeToKey;
	
	int mNumPeaks;
	int mNumNewCentroids;
	int mNumCurrentCentroids;
	int mNumPreviousCentroids;
	
	float mTemplateSizeY;

	float mMatchDistance;
	float mZScale;
	int mTaxelsThresh;
	
	float mSmoothing;
	float mForceCurve;
	
	float mFilterThreshold;
	float mOnThreshold;
	float mOffThreshold;
	float mLoPressureThreshold;
	
	int mKeyboardType;

	MLSignal mFilteredInput;
	MLSignal mFilteredInputX;
	MLSignal mFilteredInputY;

	MLSignal mCalibratedSignal;
	MLSignal mCalibrationProgressSignal;
	std::mutex mCalibratedSignalMutex;
	
	MLSignal adjustForMax(const MLSignal& in);
	
	SensorBitsArray findThresholdBits(const MLSignal& in);
	
	
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findPings(const SensorBitsArray& inThresh, const MLSignal& inSignal);

	VectorsH reducePingsH(const VectorsH& pings);

	VectorsH correctPingsH(const VectorsH& pings);
	
	VectorsV correctPingsV(const VectorsV& pings);
	
	KeyStates pingsToKeyStates(const VectorsH& pingsHoriz, const VectorsV& pingsVert);
	KeyStates reduceKeyStates(const KeyStates& x);
	KeyStates combineKeyStates(const KeyStates& x);
	
	KeyStates filterKeyStates(const KeyStates& x, const KeyStates& ym1);
	
	std::array<Vec4, kMaxTouches> findTouches(const KeyStates& keyStates);
	
	std::array<Vec4, kMaxTouches> combineCloseTouches(const std::array<Vec4, kMaxTouches>& t);

	std::array<Vec4, kMaxTouches> removeCrowdedTouches(const std::array<Vec4, kMaxTouches>& t);

	std::array<Vec4, kMaxTouches> sortTouchesWithHysteresis(const std::array<Vec4, kMaxTouches>& t, std::array<int, TouchTracker::kMaxTouches>& currentSortedOrder);
	std::array<Vec4, kMaxTouches> limitNumberOfTouches(const std::array<Vec4, kMaxTouches>& t);

	std::array<Vec4, kMaxTouches> matchTouches(const std::array<Vec4, kMaxTouches>& x, const std::array<Vec4, kMaxTouches>& x1);
	
	std::array<Vec4, kMaxTouches> filterTouchesXYAdaptive(const std::array<Vec4, kMaxTouches>& x, const std::array<Vec4, kMaxTouches>& x1);
	std::array<Vec4, kMaxTouches> filterTouchesXY(const std::array<Vec4, kMaxTouches>& x, const std::array<Vec4, kMaxTouches>& x1, float freq);
	std::array<Vec4, kMaxTouches> filterTouchesZ(const std::array<Vec4, kMaxTouches>& x, const std::array<Vec4, kMaxTouches>& x1, float upFreq, float downFreq);

	std::array<Vec4, kMaxTouches> exileOldTouches(const std::array<Vec4, kMaxTouches>& x);
	std::array<Vec4, kMaxTouches> clampTouches(const std::array<Vec4, kMaxTouches>& x);
	
	void outputTouches(std::array<Vec4, TouchTracker::kMaxTouches> touches);
	
	
	SensorBitsArray mThresholdBits;
	SensorBitsArray mThresholdBitsOut;
	std::mutex mThresholdBitsMutex;	
	
	
	// a ping is a guess at where a touch is over a particular row or column.
	VectorsH mPingsHorizRaw;
	VectorsH mPingsHoriz;
	VectorsH mPingsHorizY1;
	VectorsH mPingsHorizOut;
	VectorsH mPingsHorizRawOut;
	std::mutex mPingsHorizRawOutMutex;	
	std::mutex mPingsHorizOutMutex;	
	VectorsV mPingsVertRaw;
	VectorsV mPingsVertRawOut;
	VectorsV mPingsVert;
	VectorsV mPingsVertY1;
	VectorsV mPingsVertOut;
	std::mutex mPingsVertRawOutMutex;	
	std::mutex mPingsVertOutMutex;	
	
	// clusters of pings
	VectorsH mClustersHoriz;
	VectorsH mClustersHorizRaw;
	VectorsH mClustersHorizY1;
	VectorsH mClustersHorizOut;
	std::mutex mClustersHorizOutMutex;	
	VectorsH mClustersHorizRawOut;
	std::mutex mClustersHorizRawOutMutex;	
	VectorsV mClustersVert;
	VectorsV mClustersVertRaw;
	VectorsV mClustersVertY1;
	VectorsV mClustersVertRawOut;
	std::mutex mClustersVertRawOutMutex;	
	VectorsV mClustersVertOut;
	std::mutex mClustersVertOutMutex;	
	
	// key states
	KeyStates mKeyStates;
	KeyStates mKeyStates1;
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
	std::array<Vec4, kMaxTouches> mTouchesXY1;
	std::array<Vec4, kMaxTouches> mTouches1; 
	std::array<Vec4, kMaxTouches> mTouches2; 
	
	std::array<Vec4, kMaxTouches> mTouchesOut;
	std::mutex mTouchesOutMutex;	
	
	std::array<Vec2, kMaxTouches> mLatestPositions;
	
	MLSignal mBackground;
	
		
	int mRetrigClock;
	
	int mMaxTouchesPerFrame;
	int mPrevTouchForRotate;
	bool mRotate;
	bool mDoNormalize;
	
	float mSpanCorrect;
	
	int mNumKeys;

	int mCount;
	
	bool mNeedsClear;
	bool mUseTestSignal;

};


