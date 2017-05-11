
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include "MLSignal.h"
#include "MLVector.h"
#include "MLDebug.h"
#include "pa_ringbuffer.h"

#include <list>
#include <thread>
#include <mutex>
#include <array>

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

/*
class Touch
{
public:
	Touch();
	Touch(float px, float py, float pz, float pdz);
	~Touch() {}	
	bool isActive() const {return (age > 0); } 
	int key;
	float x;
	float y;
	float z;
	float x1, y1, z1;
	float dz;
	float xf; // filtered x
	float yf; // filtered y
	float zf; // filtered z
	float zf10; // const 10Hz filtered z
	float dzf; // d(filtered z)
	int age;
	int retrig;
	float tDist; // distance from template touch
	int releaseCtr;
	float releaseSlope;
	int unsortedIdx;
};*/

//std::ostream& operator<< (std::ostream& out, const Touch & t);

constexpr int kSensorRows = 8;
constexpr int kSensorCols = 64;

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
	static constexpr int kMaxTouches = 32; // more than 10, to allow raw touches that may be filtered 
	
	class Listener
	{
	public:
		Listener() {}
		virtual ~Listener() {}
		virtual void hasNewCalibration(const MLSignal& cal, const MLSignal& norm, float avg) = 0;
	};
	
	class Calibrator
	{
	public:
		Calibrator(int width, int height);
		~Calibrator();

		const MLSignal& getTemplate(Vec2 pos) const;		
		void setThreshold(float t) { mOnThreshold = t; }
		
		// process and add an input sample to the current calibration. 
		// return 0 if OK, 1 if the calibration is done. 
		int addSample(const MLSignal& m);
		
		void begin();
		void cancel();
		bool isCalibrating();
		bool isDone();
		bool doneCollectingNormalizeMap();
		bool hasCalibration();
		Vec2 getBinPosition(Vec2 p) const;		
		void setCalibration(const MLSignal& v);
		void setDefaultNormalizeMap();
		void setNormalizeMap(const MLSignal& m);
		
		float getZAdjust(const Vec2 p);
		float differenceFromTemplateTouch(const MLSignal& in, Vec2 inPos);
		float differenceFromTemplateTouchWithMask(const MLSignal& in, Vec2 inPos, const MLSignal& mask);
		void normalizeInput(MLSignal& in);
		bool isWithinCalibrateArea(int i, int j);
		
		MLSignal mCalibrateSignal;
		MLSignal mVisSignal;
		MLSignal mNormalizeMap;
		bool mCollectingNormalizeMap;
		Vec2 mVisPeak;
		float mAvgDistance;
		
	private:	
		void makeDefaultTemplate();
		float makeNormalizeMap();
		
		void getAverageTemplateDistance();
		Vec2 centroidPeak(const MLSignal& in);	
					
		float mOnThreshold;
		bool mActive;
		bool mHasCalibration;	
		bool mHasNormalizeMap;	
		int mSrcWidth;
		int mSrcHeight;
		int mWidth;
		int mHeight;
		std::vector<MLSignal> mData;
		std::vector<MLSignal> mDataSum;
		std::vector<int> mSampleCount;
		std::vector<int> mPassesCount;
		MLSignal mIncomingSample;
		MLSignal mDefaultTemplate;
		MLSignal mNormalizeCount;
		MLSignal mFilteredInput;
		MLSignal mTemp;
		MLSignal mTemp2;
		int mCount;
		int mTotalSamples;
		int mWaitSamplesAfterNormalize;
		float mStartupSum;
		float mAutoThresh;
		Vec2 mPeak;
		int age;
	};
	
	TouchTracker(int w, int h);
	~TouchTracker();

	void setInputSignal(MLSignal* pIn);
	void setOutputSignal(MLSignal* pOut);
	void setMaxTouches(int t);
	void makeTemplate();
	
	
	int getKeyIndexAtPoint(const Vec2 p);
	Vec2 getKeyCenterAtPoint(const Vec2 p);
	Vec2 getKeyCenterByIndex(int i);
	int touchOccupyingKey(int k);
	bool keyIsOccupied(int k) { return (touchOccupyingKey(k) >= 0); }
	
	void clear();
	void setSampleRate(float sr) { mSampleRate = sr; }
	void setThresh(float f);
	void setTaxelsThresh(int t) { mTaxelsThresh = t; }
	void setQuantize(bool q) { mQuantizeToKey = q; }
	void setLopass(float k); 	
	void setForceCurve(float f) { mForceCurve = f; }
	void setZScale(float f) { mZScale = f; }
	
	// process input and get touches. creates one frame of touch data in buffer.
	void process(int);
	
	const MLSignal& getCalibratedSignal() { return mCalibratedSignal; } 
	const MLSignal& getCalibrateSignal() { return mCalibrator.mVisSignal; }		
	
	float getCalibrateAvgDistance() {return mCalibrator.mAvgDistance; }
	Vec3 getCalibratePeak() { return mCalibrator.mVisPeak; }		
	void beginCalibrate() { mCalibrator.begin(); }	
	void cancelCalibrate() { mCalibrator.cancel(); }	
	bool isCalibrating() { return(mCalibrator.isCalibrating()); }	
	bool isCollectingNormalizeMap() { return(mCalibrator.mCollectingNormalizeMap); }	
	void setCalibration(const MLSignal& v) { mCalibrator.setCalibration(v); }
	bool isWithinCalibrateArea(int i, int j) { return mCalibrator.isWithinCalibrateArea(i, j); }
	
	MLSignal& getNormalizeMap() { return mCalibrator.mNormalizeMap; }
	void setNormalizeMap(const MLSignal& v) { mCalibrator.setNormalizeMap(v); }
	void setListener(Listener* pL) { mpListener = pL; }
	void setDefaultNormalizeMap();
	void setRotate(bool b);
	void setUseTestSignal(bool b) { mUseTestSignal = b; }
	void doNormalize(bool b) { mDoNormalize = b; }
	
	void setSpanCorrect(float v) { mSpanCorrect = v; }
	
	// returning by value - MLTEST
	// these should use time-stamped ringbuffers to communicate with views
	
	typedef VectorArray2D<kSensorRows, kSensorCols> VectorsH;
	typedef VectorArray2D<kSensorCols, kSensorRows> VectorsV;

	VectorsH getSpansHoriz() { std::lock_guard<std::mutex> lock(mSpansHorizOutMutex); return mSpansHorizOut; }
	VectorsH getPingsHoriz() { std::lock_guard<std::mutex> lock(mPingsHorizOutMutex); return mPingsHorizOut; }
	VectorsH getClustersHoriz() { std::lock_guard<std::mutex> lock(mClustersHorizOutMutex); return mClustersHorizOut; }
	
	VectorsV getSpansVert() { std::lock_guard<std::mutex> lock(mSpansVertOutMutex); return mSpansVertOut; }
	VectorsV getPingsVert() { std::lock_guard<std::mutex> lock(mPingsVertOutMutex); return mPingsVertOut; }	
	VectorsV getClustersVert() { std::lock_guard<std::mutex> lock(mClustersVertOutMutex); return mClustersVertOut; }
	
	std::array<Vec4, kMaxTouches> getRawTouches() { std::lock_guard<std::mutex> lock(mTouchesRawOutMutex); return mTouchesRawOut; }
		
	std::array<Vec4, kMaxTouches> getTouches() { std::lock_guard<std::mutex> lock(mTouchesOutMutex); return mTouchesOut; }
	
private:	

	// Touch class for internal use.
	// Externally, only x y z and age are relevant so a Vec4 is used for brevity.
	class Touch
	{
	public:	
//		Touch(Vec4 v) : x(v.x()), y(v.y()), z(v.z()), age(v.w()) {}
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
	float mLopass;
	
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
	
	float mOnThreshold;
	float mOffThreshold;
		
	int mKeyboardType;

	MLSignal mFilteredInput;

	MLSignal mCalibratedSignal;
	MLSignal mCalibrationProgressSignal;

	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findSpans(const MLSignal& in);
	
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findZ2Spans(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& intSpans, const MLSignal& in);
		
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inSpans, const MLSignal& in);
	
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findZ2Pings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inSpans, const MLSignal& in);
	
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> filterPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPings, const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPingsY1);
	
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> clusterPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPings, const MLSignal& in);
	
	std::array<Vec4, kMaxTouches> findTouches(const VectorsH& pingsHoriz, const VectorsV& pingsVert, const MLSignal& z);
	
	std::array<Vec4, kMaxTouches> findClusters(const VectorsH& pingsHoriz, const VectorsV& pingsVert);

	int getFreeIndex(std::array<Touch, kMaxTouches> t);

	std::array<Vec4, kMaxTouches> filterTouches(const std::array<Vec4, kMaxTouches>& x, const std::array<Vec4, kMaxTouches>& x1, const MLSignal& z);

	std::array<Vec4, kMaxTouches> filterTouchesXX(const std::array<Vec4, kMaxTouches>& t);
	
	void matchTouches();
	
	void filterAndOutputTouches();
	
	// spans of touches in x and y
	VectorsH mSpansHoriz;
	VectorsH mSpansHoriz1;
	VectorsH mSpansHorizOut;
	std::mutex mSpansHorizOutMutex;	
	VectorsV mSpansVert;
	VectorsV mSpansVert1;
	VectorsV mSpansVertOut;
	std::mutex mSpansVertOutMutex;	
	
	// a ping is a guess at where a touch is over a particular row or column.
	VectorsH mPingsHoriz;
	VectorsH mPingsHorizY1;
	VectorsH mPingsHorizOut;
	std::mutex mPingsHorizOutMutex;	
	VectorsV mPingsVert;
	VectorsV mPingsVertY1;
	VectorsV mPingsVertOut;
	std::mutex mPingsVertOutMutex;	
	
	// clusters of pings
	VectorsH mClustersHoriz;
	VectorsH mClustersHorizOut;
	std::mutex mClustersHorizOutMutex;	
	VectorsV mClustersVert;
	VectorsV mClustersVertOut;
	std::mutex mClustersVertOutMutex;	
	
	// touches
	std::array<Vec4, kMaxTouches> mTouchesRaw;
	std::array<Vec4, kMaxTouches> mTouchesRawOut;
	std::mutex mTouchesRawOutMutex;	
	std::array<Vec4, kMaxTouches> mTouches;
	std::array<Vec4, kMaxTouches> mTouches1;
	std::array<Vec4, kMaxTouches> mTouchesOut;
	std::mutex mTouchesOutMutex;	
	
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

	Calibrator mCalibrator;

};


