
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __TOUCH_TRACKER__
#define __TOUCH_TRACKER__

#include "Filters2D.h"
#include "MLVector.h"
#include "MLDebug.h"
#include "pa_ringbuffer.h"

#include "MLFFT.h" // not production code

#include <list>
#include <thread>
#include <mutex>

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
	dzColumn = 3,
	ageColumn = 4,
	dtColumn = 5,
	noteColumn = 6,
	reservedColumn = 7
} TouchSignalColumns;

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
};

std::ostream& operator<< (std::ostream& out, const Touch & t);

class TouchTracker
{
public:
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
	
	int addTouch(const Touch& t);
	int getTouchIndexAtKey(const int k);
	void removeTouchAtIndex(int touchIdx);

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
	const MLSignal& getFitTestSignal() { return mFitTestSignal; } 
	const MLSignal& getTestSignal2() { return mTestSignal2; } 
	const MLSignal& getCalibratedSignal() { return mCalibratedSignal; } 
	const MLSignal& getRegionSignal() { return mRegions; } 
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
	
	// new
	
	// returning by value - MLTEST
	std::vector<Vec3> getSpansHoriz() { std::lock_guard<std::mutex> lock(mSpansHorizMutex); return mSpansHoriz; }
	std::vector<Vec3> getSpansVert() { std::lock_guard<std::mutex> lock(mSpansVertMutex); return mSpansVert; }
	
	std::vector<Vec3> getPingsHoriz() { std::lock_guard<std::mutex> lock(mPingsHorizMutex); return mPingsHoriz; }
	
	std::vector<Vec3> getPingsVert() { std::lock_guard<std::mutex> lock(mPingsVertMutex); return mPingsVert; }
	
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

	// new
	void findSpansHoriz();
	void findSpansVert();
	void correctSpansHoriz();
	void correctSpansVert();
	
	void findPingsHoriz();
	void findPingsVert();
	void findTouches();

	void fitCurves();
	void filterAndOutputTouches();
	
	// new
	MLSignal mRegions; 
	MLSignal mRowPeaks; 

	
	std::vector<Vec3> mSpansHoriz;
	std::mutex mSpansHorizMutex;

	std::vector<Vec3> mSpansVert;
	std::mutex mSpansVertMutex;

	// a ping is a guess at where a touch is over a particular row.
	std::vector<Vec3> mPingsHoriz;
	std::mutex mPingsHorizMutex;
	
	std::vector<Vec3> mPingsVert;
	std::mutex mPingsVertMutex;
	

	AsymmetricOnepoleMatrix mBackgroundFilter;
	MLSignal mBackgroundFilterFrequency;
	MLSignal mBackgroundFilterFrequency2;
	MLSignal mBackground;
	
	// new
	MLSignal mFFT1;
	MLSignal mFFT1i;
	MLSignal mFFT2;
	MLSignal mTouchFrequencyMask;
	MLSignal mTouchKernel;
	MLSignal mTouchKerneli;
		
	int mRetrigClock;
	
	int mMaxTouchesPerFrame;
	int mPrevTouchForRotate;
	bool mRotate;
	bool mDoNormalize;
	
	float mSpanCorrect;

	std::vector<Touch> mTouches;
	std::vector<Touch> mTouchesToSort;
	
	int mNumKeys;

	int mCount;
	
	bool mNeedsClear;
	bool mUseTestSignal;

	Calibrator mCalibrator;

};


#endif // __TOUCH_TRACKER__

