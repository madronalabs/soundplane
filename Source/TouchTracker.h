
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include "Filters2D.h"
#include "MLVector.h"
#include "MLDebug.h"
#include "pa_ringbuffer.h"

#include "MLFFT.h" // not production code

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

typedef enum
{
	rectangularA = 0,
	hexagonalA // etc. 
} KeyboardTypes;

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

struct LineSegment
{
	LineSegment(Vec2 a, Vec2 b) : start(a), end(b) {}
	Vec2 start;
	Vec2 end;
};

struct Mat22
{
	Mat22(float a, float b, float c, float d) : a00(a), a10(b), a01(c), a11(d) {}
	float a00, a10, a01, a11;
};

inline Vec2 multiply(Mat22 m, Vec2 a)
{
	return Vec2(m.a00*a.x() + m.a10*a.y(), m.a01*a.x() + m.a11*a.y());
}

inline LineSegment multiply(Mat22 m, LineSegment a)
{
	return LineSegment(multiply(m, a.start), multiply(m, a.end));
}

inline LineSegment translate(LineSegment a, Vec2 p)
{
	return LineSegment(a.start + p, a.end + p);
}

inline bool lengthIsZero(LineSegment a)
{
	return((a.start.x() == a.end.x())&&(a.start.y() == a.end.y()));
}

inline float length(LineSegment a)
{
	return (a.end - a.start).magnitude();
}

Vec2 intersect(const LineSegment& a, const LineSegment& b); // TODO move to utils



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
	static constexpr int kMaxTouches = 10;
	
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
	
/*	int addTouch(const Touch& t);
	int getTouchIndexAtKey(const int k);
	void removeTouchAtIndex(int touchIdx);
*/
	void clear();
	void setSampleRate(float sr) { mSampleRate = sr; }
	void setThresh(float f);
	void setTemplateThresh(float f) { mTemplateThresh = f; }
	void setTaxelsThresh(int t) { mTaxelsThresh = t; }
	void setQuantize(bool q) { mQuantizeToKey = q; }
	void setBackgroundFilter(float v) { mBackgroundFilterFreq = v; }
	void setLopass(float k); 	
	void setForceCurve(float f) { mForceCurve = f; }
	void setZScale(float f) { mZScale = f; }
	
	// process input and get touches. creates one frame of touch data in buffer.
	void process(int);
	
	const MLSignal& getTestSignal() { return mTestSignal; } 
	const MLSignal& getGradientSignalX() { return mGradientSignalX; } 
	const MLSignal& getGradientSignalY() { return mGradientSignalY; } 
	const MLSignal& getFitTestSignal() { return mFitTestSignal; } 
	const MLSignal& getTestSignal2() { return mTestSignal2; } 
	const MLSignal& getCalibratedSignal() { return mCalibratedSignal; } 
	const MLSignal& getCookedSignal() { return mCookedSignal; } 
	const MLSignal& getCalibrationProgressSignal() { return mCalibrationProgressSignal; } 
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
	VectorArray2D<kSensorRows, kSensorCols> getSpansHoriz() { std::lock_guard<std::mutex> lock(mSpansHorizOutMutex); return mSpansHorizOut; }
	VectorArray2D<kSensorRows, kSensorCols> getPingsHoriz() { std::lock_guard<std::mutex> lock(mPingsHorizOutMutex); return mPingsHorizOut; }
	
	VectorArray2D<kSensorCols, kSensorRows> getSpansVert() { std::lock_guard<std::mutex> lock(mSpansVertOutMutex); return mSpansVertOut; }
	VectorArray2D<kSensorCols, kSensorRows> getPingsVert() { std::lock_guard<std::mutex> lock(mPingsVertOutMutex); return mPingsVertOut; }
	
	VectorArray2D<kSensorRows, kSensorCols> getIntersections() { std::lock_guard<std::mutex> lock(mIntersectionsOutMutex); return mIntersectionsOut; }
		
	std::array<Vec4, kMaxTouches> getTouches() { std::lock_guard<std::mutex> lock(mTouchesOutMutex); return mTouchesOut; }
	
private:	


	Listener* mpListener;

	Vec3 closestTouch(Vec2 pos);
	float getInhibitThreshold(Vec2 a);
	float getInhibitThresholdVerbose(Vec2 a);

	Vec2 adjustPeak(const MLSignal& in, int x, int y);
	Vec2 adjustPeakToTemplate(const MLSignal& in, int x, int y);
	Vec2 fractionalPeakTaylor(const MLSignal& in, const int x, const int y);
	
	void dumpTouches();
	int countActiveTouches();
	void makeFrequencyMask();
	
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
	float mOverrideThresh;
	float mBackgroundFilterFreq;
	float mTemplateThresh;
		
	int mKeyboardType;

	MLSignal mFilteredInput;
	MLSignal mResidual;
	MLSignal mFilteredResidual;
	MLSignal mSumOfTouches;
	MLSignal mInhibitMask;
	MLSignal mTemp;
	MLSignal mTempWithBorder;
	MLSignal mTestSignal;
	MLSignal mGradientSignalX;
	MLSignal mGradientSignalY;
	MLSignal mFitTestSignal;
	MLSignal mTestSignal2;
	MLSignal mCalibratedSignal;
	MLSignal mCookedSignal;
	MLSignal mXYSignal;
	MLSignal mInputMinusBackground;
	MLSignal mInputMinusTouches;
	MLSignal mCalibrationProgressSignal;
	MLSignal mTemplate;
	MLSignal mTemplateScaled;
	MLSignal mNullSig;
	MLSignal mTemplateMask;	
	MLSignal mDzSignal;	
	MLSignal mRetrigTimer;
	
	typedef VectorArray2D<kSensorRows, kSensorCols> HSpans;

	// new
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findSpans(const MLSignal& in);
	
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findZ2Spans(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& intSpans, const MLSignal& in);
		
	template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
	VectorArray2D<ARRAYS, ARRAY_LENGTH> findPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inSpans, const MLSignal& in);
	
	
	
	VectorArray2D<kSensorRows, kSensorCols> combineSpansHoriz(const VectorArray2D<kSensorRows, kSensorCols>& inSpans);
	VectorArray2D<kSensorRows, kSensorCols> correctSpansHoriz(const VectorArray2D<kSensorRows, kSensorCols>& inSpans);
	VectorArray2D<kSensorRows, kSensorCols> filterSpansHoriz(const VectorArray2D<kSensorRows, kSensorCols>& inSpans, const VectorArray2D<kSensorRows, kSensorCols>& inSpans1);
	
	VectorArray2D<kSensorRows, kSensorCols> findPingsHorizUnused(const VectorArray2D<kSensorRows, kSensorCols>& inSpans, const MLSignal& in);
	
	VectorArray2D<kSensorRows, kSensorCols> findIntersections(const VectorArray2D& pingsHoriz, const VectorArray2D& pingsVert);

	
	void findSpansVert();
	
	void combineSpansVert();
	
	void filterSpansVert();
	
	void correctSpansVert();
	
	void findLineSegmentsHoriz();
	void findLineSegmentsVert();
	
	void findTouches();
	
	void matchTouches();
	
	void filterAndOutputTouches();
	
	// spans of touches in x and y
	VectorArray2D<kSensorRows, kSensorCols> mSpansHoriz;
	VectorArray2D<kSensorRows, kSensorCols> mSpansHoriz1;
	VectorArray2D<kSensorRows, kSensorCols> mSpansHorizOut;
	std::mutex mSpansHorizOutMutex;	
	VectorArray2D<kSensorCols, kSensorRows> mSpansVert;
	VectorArray2D<kSensorCols, kSensorRows> mSpansVert1;
	VectorArray2D<kSensorCols, kSensorRows> mSpansVertOut;
	std::mutex mSpansVertOutMutex;	
	
	// a ping is a guess at where a touch is over a particular row or column.
	VectorArray2D<kSensorRows, kSensorCols> mPingsHoriz;
	VectorArray2D<kSensorRows, kSensorCols> mPingsHorizOut;
	std::mutex mPingsHorizOutMutex;	
	VectorArray2D<kSensorCols, kSensorRows> mPingsVert;
	VectorArray2D<kSensorCols, kSensorRows> mPingsVertOut;
	std::mutex mPingsVertOutMutex;	
	
	// intersections of pings
	VectorArray2D<kSensorRows, kSensorCols> mIntersections;
	VectorArray2D<kSensorRows, kSensorCols> mIntersectionsOut;
	std::mutex mIntersectionsOutMutex;	
	
	// touches
	std::array<Vec4, kMaxTouches> mTouches;
	std::array<Vec4, kMaxTouches> mTouches1;
	std::array<Vec4, kMaxTouches> mTouchesOut;
	std::mutex mTouchesOutMutex;	
	
	
	AsymmetricOnepoleMatrix mBackgroundFilter;
	MLSignal mBackgroundFilterFrequency;
	MLSignal mBackgroundFilterFrequency2;
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


