
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "TouchTracker.h"

#include <algorithm>
#include <numeric>

bool spansOverlap(Vec4 a, Vec4 b)
{
	return (within(b.x(), a.x(), a.y() ) || within(b.y(), a.x(), a.y()) ||
			((b.x() < a.x()) && (b.y() > a.y()))
			);
}

template<size_t ROW_LENGTH>
void replaceLastSpanInRow(std::array<Vec4, ROW_LENGTH>& row, Vec4 b)
{
	 auto lastNonNull = std::find_if(row.rbegin(), row.rend(), [](Vec4 a){ return bool(a); });
	 if(lastNonNull != row.rend())
	 {
		*lastNonNull = b;
	 }
	 else
	 {
		 *(row.begin()) = b;
	 }
}

template<size_t ROW_LENGTH>
void appendVectorToRow(std::array<Vec4, ROW_LENGTH>& row, Vec4 b)
{
	// if full (last element is not null), return
	if(row[ROW_LENGTH - 1]) { debug() << "!"; return; }
	
	auto firstNull = std::find_if(row.begin(), row.end(), [](Vec4 a){ return !bool(a); });
	*firstNull = b;
}


template<size_t ARRAY_LENGTH>
int countPings(std::array<Vec4, ARRAY_LENGTH>& array)
{
	int n = 0;
	while(array[n])
	{
		n++;
	}
	return n;
}

template<size_t ROW_LENGTH>
void insertSpanIntoRow(std::array<Vec4, ROW_LENGTH>& row, Vec4 b)
{
	// if full (last element is not null), return
	if(row[ROW_LENGTH - 1]) { debug() << "!"; return; }
	
	for(int i=0; i<ROW_LENGTH; i++)
	{
		Vec4 a = row[i];
		if(!a)
		{
			// empty, overwrite
			row[i] = b;
			return;
		}
		else if(within(b.x(), a.x(), a.y() ) || within(b.y(), a.x(), a.y() ) ||
				( (b.x() < a.x()) && (b.y() > a.y()) )
				)
		{
			// overlapping, combine
			// row[i] = Vec4(min(a.x(), b.x()), max(a.y(), b.y()), b.z(), b.w() );
			// overwrite
			row[i] = b;
			return;
		}
		else if(b.x() > a.y())
		{
			// past existing span, insert after
			for(int j = ROW_LENGTH - 1; j > i; j--)
			{
				row[j] = row[j - 1];
			}
			row[i] = b;
			return;
		}
	}	
}

template<int ROW_LENGTH>
void removeSpanFromRow(std::array<Vec4, ROW_LENGTH>& row, int pos)
{
	int spans = row.size();
	// restore null at end in case needed 
	row[ROW_LENGTH - 1] = Vec4::null();
	
	for(int j = pos; j < spans - 1; ++j)
	{
		Vec4 next = row[j + 1];
		row[j] = next;
		if(!next) break;
	}
}

// insert before element i, making room
template<typename T, size_t ARRAY_LENGTH>
void insert(std::array<T, ARRAY_LENGTH>& k, T b, int i)
{
	if(i >= ARRAY_LENGTH) return;
	for(int j = ARRAY_LENGTH - 1; j > i; j--)
	{
		k[j] = k[j - 1];
	}
	k[i] = b;
	return;
}


// combine existing cluster a with new ping b. z of the cluster keeps a running sum of all ping z values
// so that a running centroid of the position can be calculated.
Vec4 combinePings(Vec4 a, Vec4 b)
{
	float sxz = (a.x() * a.z())+(b.x() * b.z());
	float sz = a.z() + b.z();
	float sx = sxz / sz;
	
	Vec4 c(sx, 0., sz, 0.);
	
	if(sx < 0.f) { debug() << "**" << a << " + " << b << " = " << c << "**\n"; }
	
	return c;
	
}

template<size_t ARRAY_LENGTH>
void insertPingIntoArray(std::array<Vec4, ARRAY_LENGTH>& k, Vec4 b, float r, bool dd)
{
	// if full (last element is not null), return
	if(k[ARRAY_LENGTH - 1]) { debug() << "!"; return; }
	
	if(dd) debug() << " (" << countPings(k) ;
	

	// get insert index i
	int i = 0;
	while(k[i] && (k[i].x() < b.x())) 
	{ 
		i++; 
	}
	
	if(dd) debug() << "(" << i << ")";
		
	/*
	if(!k[i])
	{
		// rightmost (incl. first)
		k[i] = b;
		if(dd) debug() << "N) ";
		return;
	}
	*/
	
	bool overlapRight = within(b.x(), k[i].x() - r, k[i].x() + r);
	if(i == 0)
	{
		// leftmost
		if (overlapRight)
		{
			k[i] = combinePings(k[i], b);

			if(dd) debug() << "L) ";
		}
		else
		{
			insert(k, b, i);
			
			if(dd) debug() << "R) ";
		}
		return;
	}

	bool overlapLeft = within(b.x(), k[i - 1].x() - r, k[i - 1].x() + r);
	
	if((!overlapLeft) && (!overlapRight))
	{
		insert(k, b, i);		
		if(dd) debug() << "A)";
	}
	else if((overlapLeft) && (!overlapRight))
	{
		k[i - 1] = combinePings(k[i - 1], b);
		if(dd) debug() << "B)";
	}
	else if((!overlapLeft) && (overlapRight))
	{
		k[i] = combinePings(k[i], b);
		if(dd) debug() << "C)";
	}
	else // overlapLeft && overlapRight
	{
		// deinterpolate b -> a and c
		float dab = b.x() - k[i - 1].x();
		float dbc = k[i].x() - b.x();
		float pa = dbc / (dab + dbc);
		float pc = dab / (dab + dbc);
		Vec4 bToA(b.x(), b.y(), b.z()*pa, 0.f);
		Vec4 bToC(b.x(), b.y(), b.z()*pc, 0.f);
		k[i - 1] = combinePings(k[i - 1], bToA);
		k[i] = combinePings(k[i], bToC);

		if(dd) debug() << "D)";
	}	
}


TouchTracker::SensorBitsArray shiftLeft(const TouchTracker::SensorBitsArray& in)
{
	TouchTracker::SensorBitsArray y;
	int w = kSensorCols;
	int h = kSensorRows;
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			y[j*w + i] = (i < w - 1) ? in[j*w + i + 1] : 1;
		}
	}	return y;
}

TouchTracker::SensorBitsArray shiftRight(const TouchTracker::SensorBitsArray& in)
{
	TouchTracker::SensorBitsArray y;
	int w = kSensorCols;
	int h = kSensorRows;
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			y[j*w + i] = (i > 0) ? in[j*w + i - 1] : 1;
		}
	}	return y;
}

TouchTracker::SensorBitsArray shiftUp(const TouchTracker::SensorBitsArray& in)
{
	TouchTracker::SensorBitsArray y;
	int w = kSensorCols;
	int h = kSensorRows;
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			y[j*w + i] = (j < h - 1) ? in[(j + 1)*w + i] : 1;
		}
	}	return y;
}

TouchTracker::SensorBitsArray shiftDown(const TouchTracker::SensorBitsArray& in)
{
	TouchTracker::SensorBitsArray y;
	int w = kSensorCols;
	int h = kSensorRows;
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			y[j*w + i] = (j > 0) ? in[(j - 1)*w + i] : 1;
		}
	}	return y;
}

TouchTracker::SensorBitsArray erode(const TouchTracker::SensorBitsArray& in)
{
	TouchTracker::SensorBitsArray y(in);
	y &= shiftLeft(in);
	y &= shiftRight(in);
	y &= shiftUp(in);
	y &= shiftDown(in);
	
	return y;
}

// TODO piecewise map MLRange type
float sensorToKeyY(float sy)
{
	float ky = 0.f;

	// Soundplane A as measured
	constexpr int mapSize = 6;
	constexpr std::array<float, mapSize> sensorMap{{0.15, 1.1, 2.9, 4.1, 5.9, 6.85}};
	constexpr std::array<float, mapSize> keyMap{{0., 1., 2., 3., 4., 5.}};
	
	if(sy < sensorMap[0])
	{
		ky = keyMap[0];
	}
	else if(sy > sensorMap[mapSize - 1])
	{
		ky = keyMap[mapSize - 1];
	}
	else
	{
		for(int i = 1; i<mapSize; ++i)
		{
			if(sy <= sensorMap[i])
			{
				// piecewise linear
				float m = (sy - sensorMap[i - 1])/(sensorMap[i] - sensorMap[i - 1]);
				ky = lerp(keyMap[i - 1], keyMap[i], m);
				break;
			}
		}
	}
	
	return ky;
	
}


TouchTracker::TouchTracker(int w, int h) :
	mWidth(w),
	mHeight(h),
	mpIn(0),
	mNumNewCentroids(0),
	mNumCurrentCentroids(0),
	mNumPreviousCentroids(0),
	mMatchDistance(2.0f),
	mNumPeaks(0),
	mFilterThreshold(0.01f),
	mOnThreshold(0.03f),
	mOffThreshold(0.02f),
	mTaxelsThresh(9),
	mQuantizeToKey(false),
	mCount(0),
	mMaxTouchesPerFrame(0),
	mNeedsClear(true),
	mCalibrator(w, h),
	mSampleRate(1000.f),
	mPrevTouchForRotate(0),
	mRotate(false),
	mDoNormalize(true),
	mUseTestSignal(false)
{
//	mTouches.resize(kTrackerMaxTouches);	
//	mTouchesToSort.resize(kTrackerMaxTouches);	


	mBackground.setDims(w, h);
	mFilteredInput.setDims(w, h);
	mFilteredInputX.setDims(w, h);
	mFilteredInputY.setDims(w, h);
	mCalibrationProgressSignal.setDims(w, h);

	
	// clear previous pings
	for(auto& row : mPingsHorizY1.data)
	{
		row.fill(Vec4::null());	
	} 
	for(auto& row : mPingsVertY1.data)
	{
		row.fill(Vec4::null());	
	} 
}
		
TouchTracker::~TouchTracker()
{
}

void TouchTracker::setInputSignal(MLSignal* pIn)
{ 
	mpIn = pIn; 
}

void TouchTracker::setOutputSignal(MLSignal* pOut)
{ 
	mpOut = pOut; 
	int w = pOut->getWidth();
	int h = pOut->getHeight();
	
	if (w < 5)
	{
		debug() << "TouchTracker: output signal too narrow!\n";
		return;
	}
	if (h < mMaxTouchesPerFrame)
	{
		debug() << "error: TouchTracker: output signal too short to contain touches!\n";
		return;
	}
}

void TouchTracker::setMaxTouches(int t)
{
	int newT = clamp(t, 0, kTrackerMaxTouches);
	if(newT != mMaxTouchesPerFrame)
	{
		mMaxTouchesPerFrame = newT;
	}
}

// return the index of the key over the point p.
//
int TouchTracker::getKeyIndexAtPoint(const Vec2 p) 
{
	int k = -1;
	float x = p.x();
	float y = p.y();
	int ix, iy;
	switch (mKeyboardType)
	{
		default:
			MLRange xRange(3.5f, 59.5f);
			xRange.convertTo(MLRange(1.f, 29.f));
			float kx = xRange(x);
			kx = clamp(kx, 0.f, 29.f);
			ix = kx;
			
			MLRange yRange(1.25, 5.75);  // Soundplane A as measured
			yRange.convertTo(MLRange(1.f, 4.f));
			float ky = yRange(y);
			ky = clamp(ky, 0.f, 4.f);
			iy = ky;
			
			k = iy*30 + ix;
			break;
	}
	return k;
}


// return the center of the key over the point p.
//
Vec2 TouchTracker::getKeyCenterAtPoint(const Vec2 p) 
{
	float x = p.x();
	float y = p.y();
	int ix, iy;
	float fx, fy;
	switch (mKeyboardType)
	{
		default:
			MLRange xRange(3.5f, 59.5f);
			xRange.convertTo(MLRange(1.f, 29.f));
			float kx = xRange(x);
			kx = clamp(kx, 0.f, 30.f);
			ix = kx;
			
			MLRange yRange(1.25, 5.75);  // Soundplane A as measured
			yRange.convertTo(MLRange(1.f, 4.f));
			float ky = yRange(y);
			ky = clamp(ky, 0.f, 5.f);
			iy = ky;
			
			MLRange xRangeInv(1.f, 29.f);
			xRangeInv.convertTo(MLRange(3.5f, 59.5f));
			fx = xRangeInv(ix + 0.5f);
			
			MLRange yRangeInv(1.f, 4.f);
			yRangeInv.convertTo(MLRange(1.25, 5.75));
			fy = yRangeInv(iy + 0.5f);

			break;
	}
	return Vec2(fx, fy);
}

Vec2 TouchTracker::getKeyCenterByIndex(int idx) 
{
	// for Soundplane A only
	int iy = idx/30;
	int ix = idx - iy*30;
	
	MLRange xRangeInv(1.f, 29.f);
	xRangeInv.convertTo(MLRange(3.5f, 59.5f));
	float fx = xRangeInv(ix + 0.5f);

	MLRange yRangeInv(1.f, 4.f);
	yRangeInv.convertTo(MLRange(1.25, 5.75));
	float fy = yRangeInv(iy + 0.5f);

	return Vec2(fx, fy);
}

/*
int TouchTracker::touchOccupyingKey(int k)
{
	int r = -1;
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{
			if(t.key == k)
			{
				r = i;
				break;
			}
		}
	}
	return r;
}
*/

void TouchTracker::setRotate(bool b)
{ 
	mRotate = b; 
	if(!b)
	{
		mPrevTouchForRotate = 0;
	}
}

// add new touch at first free slot.  TODO touch allocation modes including rotate.
// return index of new touch.
//
/*
int TouchTracker::addTouch(const Touch& t)
{
	int newIdx = -1;
	int minIdx = 0;
	float minZ = 1.f;
	int offset = 0;
	
	if(mRotate)
	{
		offset = mPrevTouchForRotate;
		mPrevTouchForRotate++;
		if(mPrevTouchForRotate >= mMaxTouchesPerFrame)
		{
			mPrevTouchForRotate = 0;
		}
	}
	
	for(int jr=offset; jr<mMaxTouchesPerFrame + offset; ++jr)
	{
		int j = jr%mMaxTouchesPerFrame;

		Touch& r = mTouches[j];
		if (!r.isActive())
		{		
			r = t;
			r.age = 1;			
			r.releaseCtr = 0;			
			newIdx = j;
			return newIdx;
		}
		else
		{
			mPrevTouchForRotate++;
			float rz = r.z;
			if (r.z < minZ)
			{
				minIdx = j;
				minZ = rz; 
			}
		}
	}
	
	// if we got here, all touches were active.
	// replace the touch with lowest z if new touch is greater.
	//
	if (t.z > minZ)
	{
		int n = mTouches.size();		
		if (n > 0)
		{
			Touch& r = mTouches[minIdx];
			r = t;
			r.age = 1;	
			r.releaseCtr = 0;		
			newIdx = minIdx;
		}		
	}
	return newIdx;
}
*/

/*
// return index of touch at key k, if any.
//
int TouchTracker::getTouchIndexAtKey(const int k)
{
	int r = -1;
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{		
			if(t.key == k)
			{
				r = i;
				break;
			}
		}
	}
	return r;
}

// remove the touch at index by setting the age to 0.  leave the
// position intact for downstream filters etc.  
//
void TouchTracker::removeTouchAtIndex(int touchIdx)
{
	Touch& t = mTouches[touchIdx];
	t.age = 0;
	t.key = -1;
}
*/

void TouchTracker::clear()
{
	for (int i=0; i<kMaxTouches; i++)	
	{
		mTouches[i] = Vec4();	
		mTouches1[i] = Vec4();	
	}
	mNeedsClear = true;
}


void TouchTracker::setThresh(float f) 
{ 
	mOnThreshold = clamp(f, 0.0005f, 1.f); 
	mFilterThreshold = mOnThreshold * 0.5f; 
	mOffThreshold = mOnThreshold * 0.75f; 
}

void TouchTracker::setLopass(float k)
{ 
	mLopass = k; 
}
			
// --------------------------------------------------------------------------------

#pragma mark process
	
void TouchTracker::process(int)
{	
	if (!mpIn) return;
	const MLSignal& in(*mpIn);
	
	mFilteredInput.copy(in);
	
	// clear edges (should do earlier! TODO)
	int w = in.getWidth();
	int h = in.getHeight();
	for(int j=0; j<h; ++j)
	{
		mFilteredInput(0, j) = 0;
		mFilteredInput(w - 1, j) = 0;
	}
	
	if (mNeedsClear)
	{
		mBackground.copy(mFilteredInput);
		mNeedsClear = false;
		return;
	}
		
	// filter out any negative values. negative values can shows up from capacitive coupling near edges,
	// from motion or bending of the whole instrument, 
	// from the elastic layer deforming and pushing up on the sensors near a touch. 
	mFilteredInput.sigMax(0.f);
	
	if (mCalibrator.isCalibrating())
	{		
		int done = mCalibrator.addSample(mFilteredInput);
		
		if(done == 1)
		{
			// Tell the listener we have a new calibration. We still do the calibration here in the Tracker, 
			// but the Model will be responsible for saving and restoring the calibration maps.
			if(mpListener)
			{
				mpListener->hasNewCalibration(mCalibrator.mCalibrateSignal, mCalibrator.mNormalizeMap, mCalibrator.mAvgDistance);
			}
		}
	}
	else
	{
		// TODO separate calibrator from Tracker and own in Model so test input can bypass calibration more cleanly
		bool doNormalize = false;
		if(doNormalize)
		{
			mCalibrator.normalizeInput(mFilteredInput);
		}
				
		// convolve input with 3x3 smoothing kernel.
		// a lot of filtering is needed here to get good position accuracy for Soundplane A.
		float kc, kex, key, kk;	
		kc = 4./16.; kex = 2./16.; key = 2./16.; kk=1./16.;
		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		
		// MLTEST
		mCalibratedSignal = mFilteredInput;

		if(mMaxTouchesPerFrame > 0)
		{
			mThresholdBits = findThresholdBits(mFilteredInput);
//			mThresholdBits = erode(mThresholdBits);
			
			mPingsHorizRaw = findPings<kSensorRows, kSensorCols, 0>(mThresholdBits, mFilteredInput);
			mPingsVertRaw = findPings<kSensorCols, kSensorRows, 1>(mThresholdBits, mFilteredInput);
			
//			mPingsHorizRaw = correctPings<kSensorRows, kSensorCols, 0>(mPingsHorizRaw);
//			mPingsVertRaw = correctPings<kSensorCols, kSensorRows, 1>(mPingsVertRaw);
			
			mKeyStates = pingsToKeyStates(mPingsHorizRaw, mPingsVertRaw, mKeyStates1);
			
			mKeyStates = reduceKeyStates(mKeyStates);

			mKeyStates = filterKeyStates(mKeyStates, mKeyStates1);
			mKeyStates1 = mKeyStates;

			mKeyStates = combineKeyStates(mKeyStates);
			
							
			// get touches, in key coordinates
			mTouchesRaw = findTouches(mKeyStates);
			
//			mTouches = combineTouches(mTouchesRaw);
			mTouches = (mTouchesRaw);
			
			mTouches = matchTouches(mTouches, mTouchesMatch1);
			mTouchesMatch1 = mTouches;
			
//			mTouches = filterTouches(mTouches, mTouches1);
			mTouches1 = mTouches;
			
			mTouches = clampTouches(mTouches);
			
			
	// TODO filer after match		
			
			// copy filtered spans to output array
			{
				std::lock_guard<std::mutex> lock(mThresholdBitsMutex);
				mThresholdBitsOut = mThresholdBits;
			}
			
			{
				std::lock_guard<std::mutex> lock(mPingsHorizRawOutMutex);
				mPingsHorizRawOut = mPingsHorizRaw;
			}
			
			{
				std::lock_guard<std::mutex> lock(mPingsHorizOutMutex);
				mPingsHorizOut = mPingsHoriz;
			}
			
			{
				std::lock_guard<std::mutex> lock(mClustersHorizRawOutMutex);
				mClustersHorizRawOut = mClustersHorizRaw;
			}
			
			{
				std::lock_guard<std::mutex> lock(mClustersHorizOutMutex);
				mClustersHorizOut = mClustersHoriz;
			}
			
			{
				std::lock_guard<std::mutex> lock(mPingsVertOutMutex);
				mPingsVertOut = mPingsVert;
			}
			
			{
				std::lock_guard<std::mutex> lock(mPingsVertRawOutMutex);
				mPingsVertRawOut = mPingsVertRaw;
			}
			
			{
				std::lock_guard<std::mutex> lock(mKeyStatesOutMutex);
				mKeyStatesOut = mKeyStates;
			}
			
			{
				std::lock_guard<std::mutex> lock(mTouchesRawOutMutex);
				mTouchesRawOut = mTouchesRaw;
			}
		}

		outputTouches(mTouches);
		
		{
			std::lock_guard<std::mutex> lock(mTouchesOutMutex);
			mTouchesOut = mTouches;
		}

	}
#if DEBUG   

	
	if (mCount++ > 1000) 
	{
		mCount = 0;
		if(1)
		{
			
			
		debug() << "key states : \n";
			
			for(auto row : mKeyStatesOut.data)
			{
				for(auto key : row)
				{
					debug() << "[" << key <<        "]";
				}
				debug() << " \n";		
			}
		debug() << " \n";	
		}
			 
	}   
#endif  
}


TouchTracker::SensorBitsArray TouchTracker::findThresholdBits(const MLSignal& in)
{
	const float kMinPressureThresh = 0.0004f;
	SensorBitsArray y;
	
	int w = in.getWidth();
	int h = in.getHeight();
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			y[j*w + i] = (in(i, j) > kMinPressureThresh);
		}
	}
	
	if(mCount == 0)
	{
		debug() << "thresh bits: \n";
		for(int j=0; j<h; ++j)
		{
			for(int i=0; i<w; ++i)
			{
				debug() << y[j*w + i];
			}
			debug() << "\n";
		}
	}
	
	return y;
}


// new ping finder using z'' minima and parabolic interpolation
template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::findPings(const SensorBitsArray& inThresh, const MLSignal& inSignal)
{
	constexpr float kScale = XY ? 0.100f : 0.400f; // curvature per linear distance is different in x and y
	constexpr float kThresh = 0.0001f;
	
	
	// TEST
	float maxZ, yAtMaxZ;
	maxZ = 0;
	float maxK = 0.f;
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;

	for(int j=0; j<ARRAYS; ++j)
	{
		// get row or column of input bits
		std::bitset<ARRAY_LENGTH> inThreshArray;
		if(!XY)
		{
			for(int k=0; k<ARRAY_LENGTH; ++k)
			{
				inThreshArray[k] = inThresh[j*kSensorCols + k];
			}
		}
		else
		{
			for(int k=0; k<ARRAY_LENGTH; ++k)
			{
				inThreshArray[k] = inThresh[k*kSensorCols + j];
			}
		}
		
		y.data[j].fill(Vec4::null());
		
		// find a span
		int intSpanStart = 0;
		int intSpanEnd = 0;		
		bool spanActive = false;
		bool spanComplete = false;
		
		for(int i=0; i<=ARRAY_LENGTH; ++i)
		{
			bool t = (i < ARRAY_LENGTH) ? inThreshArray[i] : 0;
			if(t)
			{
				if(!spanActive)
				{
					intSpanStart = i;
					spanActive = true;
				}
			}
			else
			{
				if(spanActive)
				{
					intSpanEnd = i;
					spanComplete = true;
					spanActive = false;
				}
			}
						
			if(spanComplete)
			{				
			//	if(intSpanEnd - intSpanStart + 1 >= kMinSpanSize)
				{
					// span acquired, look for pings
					float z = 0.f;
					float zm1 = 0.f;
					float zm2 = 0.f;
					float zm3 = 0.f;
					float dz = 0.f;
					float dzm1 = 0.f;
					float ddz = 0.f;
					float ddzm1 = 0.f;
					float ddzm2 = 0.f;
					
					// need to iterate before and after the span to get derivatives flowing
					constexpr int margin = 1;
					
					for(int i = intSpanStart - margin; i <= intSpanEnd + margin; ++i)
					{
						z = (within(i, 0, static_cast<int>(ARRAY_LENGTH))) ? (XY ? inSignal(j, i) : inSignal(i, j)) : 0.f;
						dz = z - zm1;
						ddz = dz - dzm1;
						
						// find ddz minima: peaks of curvature
						if((ddzm1 < ddz) && (ddzm1 < ddzm2) && (ddzm1 < -kThresh))
						{ 
							// get peak by quadratic interpolation
							float a = ddzm2;
							float b = ddzm1;
							float c = ddz;
							float k = (a - 2.f*b + c)/2.f * kScale * 10.f; // curvature
							float p = ((a - c)/(a - 2.f*b + c))*0.5f;
							float x = i - 2.f + p;
							
							float za = zm3;
							float zb = zm2;
							float zc = zm1;

							float pz = zb - 0.25f*(za - zc)*p;

							// TEST interesting, going right to z here
							
							if(within(x, intSpanStart + 0.f, intSpanEnd - 0.f))
							{
								appendVectorToRow(y.data[j], Vec4(x, pz, 0.f, 0.f));
								
								if(k > maxK)
								{
									maxK = k;
								}
								if(pz > maxZ)
								{
									maxZ = pz;
									yAtMaxZ = x;
								}
							}
						}
						
						zm3 = zm2;
						zm2 = zm1;
						zm1 = z;
						dzm1 = dz;
						ddzm2 = ddzm1;
						ddzm1 = ddz;
					}	
				}
				spanComplete = false;
				intSpanStart = 0;
				intSpanEnd = 0;
			}
		}
	}
	
	// display coverage
	{
		if(mCount == 0)
		{
			debug() << "\n# pings " << (XY ? "vert" : "horiz") << ":\n";
			
			for(auto array : y.data)
			{
				int c = 0;
				for(Vec4 ping : array)
				{
					if(!ping) break;
					c++;
				}
				debug() << c << " ";
			}
			debug() << "\n";
			
			debug() << "max z: " << maxZ << " pos: " << yAtMaxZ << " max k: " << maxK << "\n"; 
		}
	}
	
	//	if(maxK > 0.f)
	//	debug() << "max k: " << maxK  << "max z: " << maxZ << "\n";
	return y;
}

/*
// correct ping curvatures for keyboard mechanical properties
template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::correctPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPings)
{
	MLRange sensorToKeyX(3.5f, 59.5f, 1.f, 29.f);
	MLRange sensorToKeyY(0., 7., 0.25, 4.75); // as measured, revisit
	
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;

	int j = 0;
	for(auto pingsArray : inPings.data)
	{
		int i = 0;
		y.data[j].fill(Vec4::null());

		for(Vec4 ping : pingsArray)
		{
			if(!ping) break;
			float keyPosition, sensorPosition;
			sensorPosition = ping.x();
			if(!XY)
			{
				keyPosition = sensorToKeyX(sensorPosition);
			}
			else
			{
				keyPosition = sensorToKeyY(sensorPosition);
			}
			float k = ping.y(); 
				
			// correct curvature for key position
			float keyFraction = keyPosition - floorf(keyPosition);
			float correction = 4.f - 12.f*(keyFraction - 0.5f)*(keyFraction - 0.5f);
			k *= correction;			
			y.data[j][i] = Vec4(sensorPosition, k, 0.f, 0.f);
			
			i++;
		}
		j++;
	}

	return y;
}
*/

TouchTracker::KeyStates TouchTracker::pingsToKeyStates(const TouchTracker::VectorsH& pingsHoriz, const TouchTracker::VectorsV& pingsVert, const TouchTracker::KeyStates& ym1)
{
	MLRange sensorToKeyX(3.5f, 59.5f, 1.f, 29.f);
//	MLRange sensorToKeyY(0., 7., 0.25, 4.75); // as measured, revisit
		
	TouchTracker::KeyStates keyStates;
	
	VectorArray2D<kKeyRows, kKeyCols> zValues; // additional storage for z counts

	int j = 0;
	for(auto pingsArray : pingsHoriz.data)
	{
		for(Vec4 ping : pingsArray)
		{
			if(!ping) break;
			
			float px = sensorToKeyX(ping.x());
			float py = sensorToKeyY(j);
			float pz = ping.y(); 
			
			int kxa = clamp(static_cast<int>(floorf(px)), 0, kKeyCols - 1);
			int kya = clamp(static_cast<int>(floorf(py)), 0, kKeyRows - 1);
			Vec4& xaya = (keyStates.data[kya])[kxa];
			
			if(0)
			if(pz > xaya.z())
			{
				xaya.setX(px);
				xaya.setZ(pz);
			}
			
				xaya.setX(xaya.x() + pz*px);
				xaya.setZ(xaya.z() + pz);
			
			Vec4& zxaya = (zValues.data[kya])[kxa];
			zxaya.setZ(zxaya.z() + 1.f);	
			
		}
		j++;
	}

	int i = 0;
	for(auto pingsArray : pingsVert.data)
	{		
		bool doDebug = false;//(i == 16);
		int n = 0;
		
		for(Vec4 ping : pingsArray)
		{
			if(!ping) break;
			n++;
				
			float px = sensorToKeyX(i);
			float py = sensorToKeyY(ping.x());
			float pz = ping.y();
			
			int kxa = clamp(static_cast<int>(floorf(px)), 0, kKeyCols - 1);
			int kya = clamp(static_cast<int>(floorf(py)), 0, kKeyRows - 1);
			Vec4& xaya = (keyStates.data[kya])[kxa];		
			
			if(0)
			if(pz > xaya.w())
			{
				xaya.setY(py);
				xaya.setW(pz);
			}

				xaya.setY(xaya.y() + pz*py);
				xaya.setW(xaya.w() + pz);	
			
			Vec4& zxaya = (zValues.data[kya])[kxa];
			zxaya.setW(zxaya.w() + 1.f);	
		}
		
		i++;
	}
	
	// display coverage
	if(0)
	if(mCount == 0)
	{
		debug() << "\n counts:\n";
		
		for(auto& zValues : zValues.data)
		{
			for(Vec4& key : zValues)
			{
				int k = key.z() ;
				debug() << k;
			}

			debug() << " ";
			
			for(Vec4& key : zValues)
			{		
				int k = key.w() ;
				debug() << k;
			}
			debug() << "\n";
		}
		
	}
	
	float maxZ = 0.f;
	
	// get x and y centroids 
	{
		int j = 0;
		for(auto& keyStatesArray : keyStates.data)
		{			
			int i = 0;
			for(Vec4& key : keyStatesArray)
			{			
				float cx = key.x();
				float cy = key.y();
				float cz = key.z();
				float cw = key.w();
				
				Vec4 zVec = (zValues.data[j])[i];

				if((cz > 0.f) && (cw > 0.f))
				{
					// divide sum of position by sum of pressure to get position centroids
					key.setX(cx/cz - i);
					key.setY(cy/cw - j);
					
					float z;
					
					if(0)
					{
					// TEST
					key.setX(cx - i);
					key.setY(cy - j);
					z = sqrtf((cz)*(cw)) * 8.f;			
					}
					
					// multiplying x by y pings means both must be present
					float zn = zVec.z();
					float wn = zVec.w();
					z = sqrtf((cz/zn)*(cw/wn)) * 8.f;			
					
					maxZ = max(z, maxZ); // TEST
					
					// reject below a low threshold here to reduce # of key states we have to process
					const float kMinKeyZ = 0.002f;
					if(z < kMinKeyZ) z = 0.f;
					
					key.setZ(z);
				}
				else
				{
					// use last valid position during decay
					
					// TODO can this be dropped and history only in touch filters?
					
				//	Vec4 prevKey = ym1.data[j][i];
					//	key = Vec4(prevKey.x(), prevKey.y(), 0.f, 0.f);
					
					// TEST
					key = Vec4(0.5f, 0.5f, 0.f, 0.f);
					
				//	key = Vec4(0.5f, 0.5f, 0.f, 0.f);
				}

				i++;
			}
			j++;
		}
	}
	
//	debug() << " max z: " << maxZ << "\n";

	
	return keyStates;
}


// before filtering key states, ensure that there is no more than one centroid in any corner of 4 keys.
// this will cause all sums to add up to the original value after filtering.
//
TouchTracker::KeyStates TouchTracker::reduceKeyStates(const TouchTracker::KeyStates& in)
{
	TouchTracker::KeyStates out;
	
	for(int j=0; j<kKeyRows - 1; ++j)
	{
		for(int i=0; i<kKeyCols - 1; ++i)
		{

			Vec4& aOut = out.data[j][i];
			Vec4& bOut = out.data[j][i + 1];
			Vec4& cOut = out.data[j + 1][i];
			Vec4& dOut = out.data[j + 1][i + 1];
			Vec4 a = in.data[j][i];
			Vec4 b = in.data[j][i + 1];
			Vec4 c = in.data[j + 1][i];
			Vec4 d = in.data[j + 1][i + 1];
			
			float ax = a.x();
			float ay = a.y();
			float az = a.z();
			float bx = b.x() + 1.f;
			float by = b.y();
			float bz = b.z();
			float cx = c.x();
			float cy = c.y() + 1.f;
			float cz = c.z();
			float dx = d.x() + 1.f;
			float dy = d.y() + 1.f;
			float dz = d.z();
			
			int pa = (a.z() > 0.f) && (a.x() > 0.5f) && (a.y() > 0.5f);
			int pb = (b.z() > 0.f) && (b.x() < 1.5f) && (b.y() > 0.5f);
			int pc = (c.z() > 0.f) && (c.x() > 0.5f) && (c.y() < 1.5f);
			int pd = (d.z() > 0.f) && (d.x() < 1.5f) && (d.y() < 1.5f);
			
			int pBits = (pd << 3) | (pc << 2) | (pb << 1) | pa;
			float kx, ky, kz;
			float sxz, syz, sz;
			
			bool doWrite = true;
			
			switch(pBits)
			{
				case 0: 
					doWrite = false;
					break;
				case 1: 
					kx = ax;
					ky = ay;
					kz = az;
					break;
				case 2:
					kx = bx;
					ky = by;
					kz = bz;
					break;
				case 3: // a, b
					sxz = ax*az + bx*bz;
					syz = ay*az + by*bz;
					sz = az + bz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(az, bz);
					break;
				case 4:
					kx = cx;
					ky = cy;
					kz = cz;
					break;
				case 5: // a, c
					sxz = ax*az + cx*cz;
					syz = ay*az + cy*cz;
					sz = az + cz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(az, cz);
					break;
				case 6: // b, c
					sxz = bx*bz + cx*cz;
					syz = by*bz + cy*cz;
					sz = bz + cz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(bz, cz);
					break;
				case 7: // a, b, c
					sxz = ax*az + bx*bz + cx*cz;
					syz = ay*az + by*bz + cy*cz;
					sz = az + bz + cz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(az, max(bz, cz));
					break;
				case 8:
					kx = dx;
					ky = dy;
					kz = dz;
					break;
				case 9: // a, d
					sxz = ax*az + dx*dz;
					syz = ay*az + dy*dz;
					sz = az + dz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(az, dz);
					break;
				case 10: // b, d
					sxz = bx*bz + dx*dz;
					syz = by*bz + dy*dz;
					sz = bz + dz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(bz, dz);
					break;
				case 11: // a, b, d
					sxz = ax*az + bx*bz + dx*dz;
					syz = ay*az + by*bz + dy*dz;
					sz = az + bz + dz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(az, max(bz, dz));
					break;
				case 12: // c, d
					sxz = cx*cz + dx*dz;
					syz = cy*cz + dy*dz;
					sz = cz + dz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(cz, dz);
					break;
				case 13: // a, c, d
					sxz = ax*az + cx*cz + dx*dz;
					syz = ay*az + cy*cz + dy*dz;
					sz = az + cz + dz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(az, max(cz, dz));
					break;
				case 14: // b, c, d
					sxz = bx*bz + cx*cz + dx*dz;
					syz = by*bz + cy*cz + dy*dz;
					sz = bz + cz + dz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(bz, max(cz, dz));
					break;
				case 15: // a, b, c, d
					sxz = ax*az + bx*bz + cx*cz + dx*dz;
					syz = ay*az + by*bz + cy*cz + dy*dz;
					sz = az + bz + cz + dz;
					kx = sxz/sz;
					ky = syz/sz;
					kz = max(az, max(bz, max(cz, dz)));
					break;
					
			}


			if(doWrite)
			{
				Vec4* outputs[4] {&aOut, &bOut, &cOut, &dOut}; // OK


				// get position centroid
				
				// write centroid back to the proper state for the corner it's in
				int right = kx > 1.f;
				int top = ky > 1.f;
				int qBits = (top << 1) | right;
					
				if(right) kx -= 1.f;
				if(top) ky -= 1.f;
				
				*outputs[qBits] = Vec4(kx, ky, kz, 0.f);


			}
		}
	}
	
		
	return out;
}

// filter location and curvature in each key state
TouchTracker::KeyStates TouchTracker::filterKeyStates(const TouchTracker::KeyStates& x, const TouchTracker::KeyStates& ym1)
{

	// get z coeffs from user setting
	const float sr = 1000.f;
	const float kZFreq = 50.f;
	
	float omegaUp = kZFreq*kMLTwoPi/sr;
	float kUp = expf(-omegaUp);
	float a0Up = 1.f - kUp;
	float b1Up = kUp;
	
	
	TouchTracker::KeyStates y;
	int j = 0;
	for(auto& yRow : y.data) 
	{
		auto xRow = x.data[j];
		auto ym1Row = ym1.data[j];
		int i = 0;
		for(Vec4& yKey : yRow)
		{
			Vec4 xKey = xRow[i];
			Vec4 ym1Key = ym1Row[i];
			
			float newZ = 0.f;
			
			float x = xKey.x();
			float y = xKey.y();
			float z = xKey.z();
			float z1 = ym1Key.z();

			newZ = (z*a0Up) + (z1*b1Up);
		
			yKey = Vec4(x, y, newZ, 0.f);
			i++;
		}
		j++;
	}
	
	// display within-ness

	if(mCount == 0)
	{
		debug() << "\n within:\n";
		
		for(auto& keyStatesArray : y.data)
		{
			for(Vec4& key : keyStatesArray)
			{
				bool w = ((within(key.x(), 0.f, 1.f)) && (within(key.y(), 0.f, 1.f)));
				debug() << w;
			}
			
			debug() << "\n";
		}
		
	}
	
	return y;
}


TouchTracker::KeyStates TouchTracker::combineKeyStates(const TouchTracker::KeyStates& pIn)
{
	TouchTracker::KeyStates in = pIn;
	TouchTracker::KeyStates out;
	
	for(int j=0; j<kKeyRows - 1; ++j)
	{
		for(int i=0; i<kKeyCols - 1; ++i)
		{
			Vec4& aOut = out.data[j][i];
			Vec4& bOut = out.data[j][i + 1];
			Vec4& cOut = out.data[j + 1][i];
			Vec4& dOut = out.data[j + 1][i + 1];
			Vec4& a = in.data[j][i];
			Vec4& b = in.data[j][i + 1];
			Vec4& c = in.data[j + 1][i];
			Vec4& d = in.data[j + 1][i + 1];
			
			float ax = a.x();
			float ay = a.y();
			float az = a.z();
			float bx = b.x() + 1.f;
			float by = b.y();
			float bz = b.z();
			float cx = c.x();
			float cy = c.y() + 1.f;
			float cz = c.z();
			float dx = d.x() + 1.f;
			float dy = d.y() + 1.f;
			float dz = d.z();
			
			int pa = (a.z() > 0.f) && (a.x() > 0.5f) && (a.y() > 0.5f);
			int pb = (b.z() > 0.f) && (b.x() <= 1.5f) && (b.y() > 0.5f);
			int pc = (c.z() > 0.f) && (c.x() > 0.5f) && (c.y() <= 1.5f);
			int pd = (d.z() > 0.f) && (d.x() <= 1.5f) && (d.y() <= 1.5f);
			
			int pBits = (pd << 3) | (pc << 2) | (pb << 1) | pa;
			float kx, ky, kz;
			float sxz = 0;
			float syz = 0;
			float sz = 0;

			// add z and make position centroid for any keys at this corner
			if(pa)
			{
				a.setZ(0.f);
				sxz += ax*az;
				syz += ay*az;
				sz += az;
			}
			if(pb)
			{
				b.setZ(0.f);
				sxz += bx*bz;
				syz += by*bz;
				sz += bz;
			}
			if(pc)
			{
				c.setZ(0.f);
				sxz += cx*cz;
				syz += cy*cz;
				sz += cz;
			}
			if(pd)
			{
				d.setZ(0.f);
				sxz += dx*dz;
				syz += dy*dz;
				sz += dz;
			}
			if(sz > mFilterThreshold)
			{
				kx = sxz/sz;
				ky = syz/sz;
				kz = sz;
				
				Vec4* inputs[4] {&a, &b, &c, &d}; // OK
				Vec4* outputs[4] {&aOut, &bOut, &cOut, &dOut}; // OK
				
				
 				// get position centroid
				
				// write corner centroid back to the proper key
				int right = kx > 1.f;
				int top = ky > 1.f;
				int qBits = (top << 1) | right;
				
				if(right) kx -= 1.f;
				if(top) ky -= 1.f;

				
			//	debug() << "[" << sxz << "/" << syz << "/" << sz << " -> " << kx << "/" << ky << "]";

			debug() << qBits << " "; // qbits is WRONG
				
				
				Vec4 prev = *outputs[qBits];
				float prevX = prev.x();
				float prevY = prev.y();
				float prevZ = prev.z();
				
				float esxz = prevX*prevZ + kx*kz;
				float esyz = prevY*prevZ + ky*kz;
				float esz = prevZ + kz;
				float ekx = esxz/esz;
				float eky = esyz/esz;
				
				*outputs[qBits] = Vec4(ekx, eky, esz, 0.f);
				
	//			debug() << qBits << " ";
				
			}
		}		
	}
	
	
	
	
	if(mCount == 0)
	{
		debug() << "\n combine in:\n";
		
		debug() << std::setprecision(2);
		
		for(auto& keyStatesArray : pIn.data)
		{
			for(auto& k : keyStatesArray)
			{
				debug() << k << " ";
			}
			
			debug() << "\n";
		}
		debug() << "\n combine out:\n";
		
		for(auto& keyStatesArray : out.data)
		{
			for(auto& k : keyStatesArray)
			{
				debug() << k << " ";
			}
			
			debug() << "\n";
		}
		
	}

	
	return out;
}



// look at key states to find touches.
std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::findTouches(const TouchTracker::KeyStates& keyStates)
{
	std::array<Vec4, kMaxTouches> touches;
	touches.fill(Vec4()); // zero value, not null
	
	int nTouches = 0;
	int j = 0;
	for(auto& row : keyStates.data)
	{
		int i = 0;
		for(Vec4 key : row)
		{
			float x = key.x();
			float y = key.y();
			float z = key.z();

			if(z > mOffThreshold) 
			{
				float sensorX = (i + x);
				float sensorY = (j + y);
				
				if(nTouches < kMaxTouches) // could remove if array big enough
				{
					touches[nTouches++] = Vec4(sensorX, sensorY, z, 0);
				}
			}
			
			i++;
		}
		j++;
	}

	std::sort(touches.begin(), touches.begin() + nTouches, [](Vec4 a, Vec4 b){ return a.z() > b.z(); } );

	if(mCount == 0)
	{
		debug() << "\n raw touches: " << nTouches << "\n";
		debug() << "    ";
		for(int i=0; i<nTouches; ++i)
		{
			debug() << touches[i];
		}
		debug() << "\n";
	}
	
	return touches;
}


std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::combineTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& in)
{
	const float kMaxDistX = 2.0f;//6.0f;//4.5f;
	const float kMaxDistY = 2.0f;
	const float kMaxZ = 0.02f; 
	MLRange zToXRange(0., kMaxZ, kMaxDistX, 1.0f);
	MLRange zToYRange(0., kMaxZ, kMaxDistY, 1.0f);
	
	
	std::array<Vec4, TouchTracker::kMaxTouches> out;
	out.fill(Vec4());
	std::array<Vec4, TouchTracker::kMaxTouches> touches(in);
	
	int n;
	for(n = 0; (n < in.size()) && (in[n].z() > 0.f); n++) {}

	// no data in w yet, using w as a scratch space to mark combined touches
	for(int i=0; i<n; ++i)
	{
		touches[i].setW(0);
	}
	
	int nOut = 0;
	
	for(int i=0; i<n; ++i)
	{
		Vec4& ti = touches[i];
		float tix = ti.x();
		float tiy = ti.y();
		float tiz = ti.z();
		float tiw = ti.w();
		
		if(tiz == 0.f)
		{
			break;
		}
		   
		   
		if(tiw == 0)
		{
			float maxDistX = zToXRange.convertAndClip(tiz);
			float maxDistY = zToYRange.convertAndClip(tiz);

	//		debug() << "mx: " << maxDistX << " my: " << maxDistY << "\n";
			
			ti.setW(1);
			Vec2 pi(tix, tiy);
			for(int j=i; j<n; ++j)
			{
				Vec4& tj = touches[j];
				float tjx = tj.x();
				float tjy = tj.y();
				float tjz = tj.z();
				
				float tjw = tj.w();
				if(tjw == 0)
				{
					
					
					Vec2 pj(touches[j].x(), touches[j].y());
					Vec2 vij = pj - pi;
					
					float dx = (pi.x() - pj.x())/maxDistX;
					float dx2 = dx*dx;
					
					float dy = (pi.y() - pj.y())/maxDistY;
					float dy2 = dy*dy;
					
					float d2 = dx2 + dy2;
					
					if(d2 < 2.f) // sqrt(d) < sqrt(2)
					{
						
						// get centroid of position wrt. pressure
						// TODO centroid abstraction
						float sumXZ = tix*tiz + tjx*tjz;
						float sumYZ = tiy*tiz + tjy*tjz;
						float sumZ = tiz + tjz;
						float cx = sumXZ/sumZ;
						float cy = sumYZ/sumZ;						
						float cz = tiz;
						
						if(d2 < 1.f)
						{
							// remove lesser touch location and replace greater with centroid
							
							ti.setX(cx);
							ti.setY(cy);
							
							// keeping the original z seems best
							tj.setZ(0); // TODO we still don't like this syntax
							
							tj.setW(1); // mark as removed // TODO we still don't like this syntax
							
						
						}
						else
						{
							// fade between two touches and their centroid
							// keeping original z values
							float df = d2 - 1.0f;
							float fade = clamp(2.f - d2, 0.f, 1.f);
							
							ti.setX(lerp(tix, cx, fade));
							ti.setY(lerp(tiy, cy, fade));
							
							tj.setX(lerp(tjx, cx, fade));
							tj.setY(lerp(tjy, cy, fade));
							
						}
						
	
						//debug() << " yes.\n";
					}
					else
					{
						//debug() << " no.\n";
					}
				}				
			}
			
			out[nOut] = ti;
			nOut++;
		}
	}
	
	if(n > 0)
	debug() << n << " -> " << nOut << "\n";
	
	if(mCount == 0)
	{
		debug() << "\n combined touches: " << nOut << "\n";
		debug() << "    ";
		for(int i=0; i<nOut; ++i)
		{
			debug() << out[i];
		}
		debug() << "\n";
	}
	
	// out = in;
	return out;
}

int TouchTracker::getFreeIndex(std::array<TouchTracker::Touch, TouchTracker::kMaxTouches> t)
{
	// write new
	// find a free spot (TODO rotate) 
	int freeIdx = -1;
	for(int j=0; j<mMaxTouchesPerFrame; ++j)
	{
	   int k = j % mMaxTouchesPerFrame;
	   if(t[k].z == 0.f)
	   {
		   freeIdx = k;
		   break;
	   }
	}

	return freeIdx;
}			   

std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::matchTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& x, const std::array<Vec4, TouchTracker::kMaxTouches>& x1)
{
	const float kMaxConnectDist = 2.0f;
	
	std::array<Touch, kMaxTouches> prevTouches{};
	std::array<Touch, kMaxTouches> currTouches{};
	std::array<Touch, kMaxTouches> newTouches{};
	
	// count incoming touches
	int m = 0, n = 0;
	for(int i = 0; i < x1.size(); i++)
	{
		m += (x1[i].z() > 0.);
	}
	
	for(int i = 0; i < x.size(); i++)
	{
		n += (x[i].z() > 0.);
	}
		
	//	m = clamp(m, 0, mMaxTouchesPerFrame);
	//	n = clamp(n, 0, mMaxTouchesPerFrame);
	
	// convert to Touches
	for(int j=0; j < m; ++j)
	{
		prevTouches[j] = vec4ToTouch(x1[j]);
		prevTouches[j].currIdx = j;
	}
	for(int i=0; i < n; ++i)
	{
		currTouches[i] = vec4ToTouch(x[i]);
		currTouches[i].currIdx = i;
	}
	
	// categorize touches into one of continued, new, or removed.
	
	// for each current touch, find closest distance to a previous touch
	for(int i=0; i<n; ++i)
	{
		Touch curr = currTouches[i];
		Vec2 currPos(curr.x, curr.y);
		for(int j=0; j < m; ++j)
		{
			Touch prev = prevTouches[j];
			
			if(prev.z > 0.f)
			{
				Vec2 prevPos(prev.x, prev.y);
				Vec2 dab = currPos - prevPos;
				float dist = dab.magnitude(); // NOTE (aPos - bPos).magnitude FAILS! TODO	
				if(dist < kMaxConnectDist)
				{
					if(dist < currTouches[i].minDist)
					{
						currTouches[i].prevIdx = j;
						currTouches[i].minDist = dist;
					}
					
					//debug() << "-";
				}

			}
		}
	}
	
	// sort current touches by z
	//std::sort(currTouches.begin(), currTouches.begin() + n, [](Touch a, Touch b){ return a.z > b.z; } );
	
	// start filling new touches.
	int newSlotsRemaining = mMaxTouchesPerFrame;
	
	// fill new touches with current touches
	int maxOccupiedIdx = -1;
	for(int i=0; i<n; ++i)
	{
		int connectedIdx = currTouches[i].prevIdx;
		bool written = false;
		
		if(connectedIdx >= 0)
		{
			Touch& connectedPrevTouch = prevTouches[connectedIdx];
			
			if (!connectedPrevTouch.occupied)
			{
				// touch is continued
				// write new touch and occupy previous
				newTouches[connectedIdx] = currTouches[i];
				connectedPrevTouch.occupied = true;
				
				// increment age
				newTouches[connectedIdx].age = (connectedPrevTouch.age + 1);
				
				maxOccupiedIdx = max(maxOccupiedIdx, connectedIdx);
				written = true;
			}
		}
		
		if(!written) // because either occupied or not matched
		{
			// write new
			// find a free spot (TODO rotate) 
			int freeIdx = getFreeIndex(newTouches);
						
			if(freeIdx >= 0)
			{
				newTouches[freeIdx] = currTouches[i];
				newTouches[freeIdx].age = 1;
				maxOccupiedIdx = max(maxOccupiedIdx, freeIdx);
			}
		}
		newSlotsRemaining--;
		if(!newSlotsRemaining) break;
	}
	
	
	// convert back to Vec4
	std::array<Vec4, kMaxTouches> y;	
	y.fill(Vec4());
	for(int i=0; i <= maxOccupiedIdx; ++i)
	{
		y[i] = touchToVec4(newTouches[i]);
	}

	/*
	// debug

	int finalCount = 0;
			for(int i=0; i <= maxOccupiedIdx; ++i)
			{
				if(y[i].z() > 0.f)
				{
					finalCount++;
				}
			}
	if(finalCount > 0)
	{
	char c;
	switch(finalCount)
	{
		case 0: break;
		case 1: c = '.'; break;
		case 2: c = ':'; break;
		case 3: c = '='; break;
		case 4: c = '*'; break;
			
	}
	debug() << c ; 
	}
	*/
	
	if(0)
	{
	// count new touches
	int newN = 0;
	for(int i = 0; i < mMaxTouchesPerFrame; i++)
	{
		newN += (y[i].z() > 0.);
	}

	if(newN > 0)
	{
	
		debug() << "\n matched:";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			//newN += (y[i].z() > 0.);
			debug() << y[i] << " ";
		}
		debug() << "\n";
	
	}
	}
	
	return y;
}


std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::filterTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& in, const std::array<Vec4, TouchTracker::kMaxTouches>& inz1)
{
	float sr = 1000.f; // Soundplane A
	float kApparentMult = 0.5f; // 3dB point maybe doesn't jive with apparent gesture speed
	float kXYFreq = 20.f; // position moves more slowly than z

	// get z coeffs from user setting
	float omegaUp = mLopass*kMLTwoPi/sr*kApparentMult;
	float kUp = expf(-omegaUp);
	float a0Up = 1.f - kUp;
	float b1Up = kUp;
	float omegaDown = omegaUp*0.1f;
	float kDown = expf(-omegaDown);
	float a0Down = 1.f - kDown;
	float b1Down = kDown;
	
	MLRange zToXYFreq(0., 0.1, 1., 20.); 
		
	const float kMaxConnectDist = 2.0f;
	
	// count incoming touches, noting there may be holes due to matching
	int maxIdx = 0;
	int n = 0;
	for(int i = 0; i < mMaxTouchesPerFrame; i++) 
	{
		if(in[i].z() > 0.f)
		{
			n++;
			maxIdx = i;
		}
	}

	std::array<Vec4, TouchTracker::kMaxTouches> out;

	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		float x = in[i].x();
		float y = in[i].y();
		float z = in[i].z();
		float w = in[i].w(); 
		
		float x1 = inz1[i].x();
		float y1 = inz1[i].y();
		float z1 = inz1[i].z();
		float w1 = inz1[i].w();
				
		float dx = (x1 - x);
		float dy = (y1 - y);
		float dist = sqrtf(dx*dx + dy*dy);
		
		bool connected = (dist < kMaxConnectDist);
		
		// filter, or not
		float newX, newY, newZ;
		float newW = 0;

		//debug() << w << " ";
		//if(z > mFilterThreshold)
		
		// filter z
		float dz = z - z1;
		if(dz > 0.f)
		{
			newZ = (z*a0Up) + (z1*b1Up);
		}
		else
		{
			newZ = (z*a0Down) + (z1*b1Down);				
		}	

		// filter position
		if(z < mOnThreshold)
		{			
			// decay, hold position
			newX = x1;
			newY = y1;
		}
		else if(!connected) 
		{
			// new touch, set new position
			newX = x;
			newY = y;
		}
		else 
		{			
			// get xy coeffs, adaptive based on z
			float freq = zToXYFreq.convertAndClip(z);
			float omegaXY = freq*kMLTwoPi/sr;
			float kXY = expf(-omegaXY);
			float a0XY = 1.f - kXY;
			float b1XY = kXY;
			
			// simple filter			
			newX = (x*a0XY) + (x1*b1XY);
			newY = (y*a0XY) + (y1*b1XY);
		}
		
		// gate with hysteresis
		bool gate = (w1 > 0);
		if(newZ > mOnThreshold)
		{
			gate = true;
		}
		else if (newZ < mOffThreshold)
		{
			gate = false;
		}
		
		// increment age
		if(!gate)
		{
			newW = 0;
		}
		else if(!connected)
		{
			newW = 1;
		}
		else
		{
			newW = w1 + 1;
		}
		
		if(0) 
		if(n > 0)
		{
			debug() << "filterTouches: " << n << " touches, max Idx = " << maxIdx << "\n";
			if((z > mOnThreshold)&&(z1 <= mOnThreshold))
			{
		//		debug() << "ON:" << 
			}
		}
		out[i] = Vec4(newX, newY, newZ, newW);
	}
	
	return out;
}


// clamp touches and remove hysteresis threshold.
std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::clampTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& in)
{
	std::array<Vec4, TouchTracker::kMaxTouches> out;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Vec4 t = in[i];
		out[i] = t;			
		float newZ = (clamp(t.z() - mOnThreshold, 0.f, 1.f));
		if(t.w() == 0.f)
		{
			newZ = 0.f;
		}
		out[i].setZ(newZ);
	}
	return out;
}


void TouchTracker::outputTouches(std::array<Vec4, TouchTracker::kMaxTouches> touches)
{
	MLSignal& out = *mpOut;
	if(!mCount)
	{
		debug() << "\ntouches: \n";
	}
	
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Vec4 t = touches[i];
		out(xColumn, i) = t.x();
		out(yColumn, i) = t.y();
		out(zColumn, i) = t.z();
		out(ageColumn, i) = t.w();
		
		if(!mCount)
		{
			debug() << "    " << t ;
		}
	}
	
	if(!mCount)
		debug() << "\n";
}

void TouchTracker::setDefaultNormalizeMap()
{
	mCalibrator.setDefaultNormalizeMap();
//	mpListener->hasNewCalibration(mNullSig, mNullSig, -1.f);
}

// --------------------------------------------------------------------------------
#pragma mark calibration


TouchTracker::Calibrator::Calibrator(int w, int h) :
	mActive(false),
	mHasCalibration(false),
	mHasNormalizeMap(false),
	mCollectingNormalizeMap(false),
	mSrcWidth(w),
	mSrcHeight(h),
	mWidth(w),
	mHeight(h),
	mAutoThresh(0.05f)
{
	// resize sums vector and signals in it
	mData.resize(mWidth*mHeight);//*kCalibrateResolution*kCalibrateResolution);
	mDataSum.resize(mWidth*mHeight);//*kCalibrateResolution*kCalibrateResolution);
	mSampleCount.resize(mWidth*mHeight);
	mPassesCount.resize(mWidth*mHeight);
	for(int i=0; i<mWidth*mHeight; ++i)
	{
		mData[i].setDims(kTemplateSize, kTemplateSize);
		mData[i].clear();
		mDataSum[i].setDims(kTemplateSize, kTemplateSize);
		mDataSum[i].clear();
		mSampleCount[i] = 0;
		mPassesCount[i] = 0;
	}
	mIncomingSample.setDims(kTemplateSize, kTemplateSize);
	mVisSignal.setDims(mWidth, mHeight);
	mNormalizeMap.setDims(mSrcWidth, mSrcHeight);
	
	mNormalizeCount.setDims(mSrcWidth, mSrcHeight);
	mFilteredInput.setDims(mSrcWidth, mSrcHeight);
	mTemp.setDims(mSrcWidth, mSrcHeight);
	mTemp2.setDims(mSrcWidth, mSrcHeight);

	makeDefaultTemplate();
}

TouchTracker::Calibrator::~Calibrator()
{
}

void TouchTracker::Calibrator::begin()
{
	MLConsole() << "\n****************************************************************\n\n";
	MLConsole() << "Hello and welcome to tracker calibration. \n";
	MLConsole() << "Collecting silence, please don't touch.";
	
	mFilteredInput.clear();
	mSampleCount.clear();
	mPassesCount.clear();
	mVisSignal.clear();
	mNormalizeMap.clear();
	mNormalizeCount.clear();
	mTotalSamples = 0;
	mStartupSum = 0.;
	for(int i=0; i<mWidth*mHeight; ++i)
	{
		float maxSample = 1.f;
		mData[i].fill(maxSample);
		mDataSum[i].clear(); 
		mSampleCount[i] = 0;
		mPassesCount[i] = 0;
	}
	mPeak = Vec2();
	age = 0;
	mActive = true;
	mHasCalibration = false;
	mHasNormalizeMap = false;
	mCollectingNormalizeMap = false;
}

void TouchTracker::Calibrator::cancel()
{
	if(isCalibrating())
	{
		mActive = false;
		MLConsole() << "\nCalibration cancelled.\n";
	}
}

void TouchTracker::Calibrator::setDefaultNormalizeMap()
{
	mActive = false;
	mHasCalibration = false;
	mHasNormalizeMap = false;
}

void TouchTracker::Calibrator::makeDefaultTemplate()
{
	mDefaultTemplate.setDims (kTemplateSize, kTemplateSize);
		
	int w = mDefaultTemplate.getWidth();
	int h = mDefaultTemplate.getHeight();
	Vec2 vcenter(h/2.f, w/2.f);
	
	// default scale -- not important because we want to calibrate.
	Vec2 vscale(3.5, 3.);	 
	
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			Vec2 vdistance = Vec2(i + 0.5f, j + 0.5f) - vcenter;
			vdistance /= vscale;
			float d = clamp(vdistance.magnitude(), 0.f, 1.f);
			mDefaultTemplate(i, j) = 1.0*(1.0f - d);
		}
	}
}

// get the template touch at the point p by bilinear interpolation
// from the four surrounding templates.
const MLSignal& TouchTracker::Calibrator::getTemplate(Vec2 p) const
{
	static MLSignal temp1(kTemplateSize, kTemplateSize);
	static MLSignal temp2(kTemplateSize, kTemplateSize);
	static MLSignal d00(kTemplateSize, kTemplateSize);
	static MLSignal d10(kTemplateSize, kTemplateSize);
	static MLSignal d01(kTemplateSize, kTemplateSize);
	static MLSignal d11(kTemplateSize, kTemplateSize);
	if(mHasCalibration)
	{
		Vec2 pos = getBinPosition(p);
		Vec2 iPos, fPos;
		pos.getIntAndFracParts(iPos, fPos);	
		int idx00 = iPos.y()*mWidth + iPos.x();


		d00.copy(mCalibrateSignal.getFrame(idx00));
        if(iPos.x() < mWidth - 3)
        {
            d10.copy(mCalibrateSignal.getFrame(idx00 + 1));
        }
        else
        {
            d10.copy(mDefaultTemplate);
        }
        
        if(iPos.y() < mHeight - 1)
        {
            d01.copy(mCalibrateSignal.getFrame(idx00 + mWidth));
        }
        else
        {
            d01.copy(mDefaultTemplate);
        }
        
        if ((iPos.x() < mWidth - 3) && (iPos.y() < mHeight - 1))
        {
            d11.copy(mCalibrateSignal.getFrame(idx00 + mWidth + 1));
        }
        else
        {
            d11.copy(mDefaultTemplate);
        }
		temp1.copy(d00);
        
        temp1.sigLerp(d10, fPos.x());
		temp2.copy(d01);
		temp2.sigLerp(d11, fPos.x());
		temp1.sigLerp(temp2, fPos.y());

		return temp1;
	}
	else
	{
		return mDefaultTemplate;
	}
}

Vec2 TouchTracker::Calibrator::getBinPosition(Vec2 pIn) const
{
	// Soundplane A
	static MLRange binRangeX(2.0, 61.0, 0., mWidth);
	static MLRange binRangeY(0.5, 6.5, 0., mHeight);
	Vec2 minPos(2.5, 0.5);
	Vec2 maxPos(mWidth - 2.5f, mHeight - 0.5f);
	Vec2 pos(binRangeX(pIn.x()), binRangeY(pIn.y()));
	return vclamp(pos, minPos, maxPos);
}

void TouchTracker::Calibrator::normalizeInput(MLSignal& in)
{
	if(mHasNormalizeMap)
	{
		in.multiply(mNormalizeMap);
	}
}

// return true if the current cell (i, j) is used by the current stage of calibration
bool TouchTracker::Calibrator::isWithinCalibrateArea(int i, int j)
{
	if(mCollectingNormalizeMap)
	{
		return(within(i, 1, mWidth - 1) && within(j, 0, mHeight));
	}
	else
	{
		return(within(i, 2, mWidth - 2) && within(j, 0, mHeight));
	}
}

float TouchTracker::Calibrator::makeNormalizeMap()
{			
	int samples = 0;
	float sum = 0.f;
	for(int j=0; j<mSrcHeight; ++j)
	{
		for(int i=0; i<mSrcWidth; ++i)
		{			
			if(isWithinCalibrateArea(i, j))
			{
				float sampleSum = mNormalizeMap(i, j);
				float sampleCount = mNormalizeCount(i, j);
				float sampleAvg = sampleSum / sampleCount;
				mNormalizeMap(i, j) = 1.f / sampleAvg;
				sum += sampleAvg;
				samples++;
			}
			else
			{
				mNormalizeMap(i, j) = 0.f;
			}
		}
	}
    
	float mean = sum/(float)samples;
	mNormalizeMap.scale(mean);	
	
	// TODO analyze normal map! edges look BAD
	// MLTEST
	// constrain output values
	mNormalizeMap.sigMin(3.f);
	mNormalizeMap.sigMax(0.125f);	
	
	// return maximum
	Vec3 vmax = mNormalizeMap.findPeak();
	float rmax = vmax.z();
	
	mHasNormalizeMap = true;
	return rmax;
}

void TouchTracker::Calibrator::getAverageTemplateDistance()
{
	MLSignal temp(mWidth, mHeight);
	MLSignal tempSample(kTemplateSize, kTemplateSize);
	float sum = 0.f;
	int samples = 0;
	for(int j=0; j<mHeight; ++j)
	{
		for(int i=0; i<mWidth; ++i)
		{						
			int idx = j*mWidth + i;
			
			// put mean of input samples into temp signal at i, j
			temp.clear();
			tempSample.copy(mDataSum[idx]);
			tempSample.scale(1.f / (float)mSampleCount[idx]);
			temp.add2D(tempSample, i - kTemplateRadius, j - kTemplateRadius);
			
			float diff = differenceFromTemplateTouch(temp, Vec2(i, j));
			sum += diff;
			samples++;
			// debug() << "template diff [" << i << ", " << j << "] : " << diff << "\n";	
		}
	}
	mAvgDistance = sum / (float)samples;
}

// input: the pressure data, after static calibration (tare) but otherwise raw.
// input feeds a state machine that first collects a normalization map, then
// collects a touch shape, or kernel, at each point. 
int TouchTracker::Calibrator::addSample(const MLSignal& m)
{
	int r = 0;
	static Vec2 intPeak1;
	
	static MLSignal f2(mSrcWidth, mSrcHeight);
	static MLSignal input(mSrcWidth, mSrcHeight);
	static MLSignal tare(mSrcWidth, mSrcHeight);
	static MLSignal normTemp(mSrcWidth, mSrcHeight);
    
    // decreasing this will collect a wider area during normalization,
    // smoothing the results.
    const float kNormalizeThreshold = 0.125f;
	
	float kc, ke, kk;
	kc = 4./16.; ke = 2./16.; kk=1./16.;
	
	// simple lopass time filter for calibration
	f2 = m;
	f2.subtract(mFilteredInput);
	f2.scale(0.1f);
	mFilteredInput.add(f2);
	input = mFilteredInput;
	input.sigMax(0.);		
		
	// get peak of sample data
	Vec3 testPeak = input.findPeak();	
	float peakZ = testPeak.z();
	
	const int startupSamples = 1000;
	const int waitAfterNormalize = 2000;
	if (mTotalSamples < startupSamples)
	{
		age = 0;
		mStartupSum += peakZ;
		if(mTotalSamples % 100 == 0)
		{
			MLConsole() << ".";
		}
	}
	else if (mTotalSamples == startupSamples)
	{
		age = 0;
		//mAutoThresh = kNormalizeThresh;
		mAutoThresh = mStartupSum / (float)startupSamples * 10.f;	
		MLConsole() << "\n****************************************************************\n\n";
		MLConsole() << "OK, done collecting silence (auto threshold: " << mAutoThresh << "). \n";
		MLConsole() << "Now please slide your palm across the surface,  \n";
		MLConsole() << "applying a firm and even pressure, until all the rectangles \n";
		MLConsole() << "at left turn blue.  \n\n";
		
		mNormalizeMap.clear();
		mNormalizeCount.clear();
		mCollectingNormalizeMap = true;
	}		
	else if (mCollectingNormalizeMap)
	{
		// smooth temp signal, duplicating values at border
		normTemp.copy(input);		
		normTemp.convolve3x3rb(kc, ke, kk);
		normTemp.convolve3x3rb(kc, ke, kk);
		normTemp.convolve3x3rb(kc, ke, kk);
	
		if(peakZ > mAutoThresh)
		{
			// collect additions in temp signals
			mTemp.clear(); // adds to map
			mTemp2.clear(); // adds to sample count
			
			// where input > thresh and input is near max current input, add data. 		
			for(int j=0; j<mHeight; ++j)
			{
				for(int i=0; i<mWidth; ++i)
				{
					// test threshold with smoothed data
					float zSmooth = normTemp(i, j);
					// but add actual samples from unsmoothed input
					float z = input(i, j);
					if(zSmooth > peakZ * kNormalizeThreshold)
					{
						// map must = count * z/peakZ
						mTemp(i, j) = z / peakZ;
						mTemp2(i, j) = 1.0f;	
					}
				}
			}
			
			// add temp signals to data						
			mNormalizeMap.add(mTemp);
			mNormalizeCount.add(mTemp2);
			mVisSignal.copy(mNormalizeCount);
			mVisSignal.scale(1.f / (float)kNormMapSamples);
		}
		
		if(doneCollectingNormalizeMap())
		{				
			float mapMaximum = makeNormalizeMap();	
						
			MLConsole() << "\n****************************************************************\n\n";
			MLConsole() << "\n\nOK, done collecting normalize map. (max = " << mapMaximum << ").\n";
			MLConsole() << "Please lift your hands.";
			mCollectingNormalizeMap = false;
			mWaitSamplesAfterNormalize = 0;
			mVisSignal.clear();
			mStartupSum = 0;
			
			// MLTEST bail after normalize
			mHasCalibration = true;
			mActive = false;	
			r = 1;	
			
			MLConsole() << "\n****************************************************************\n\n";
			MLConsole() << "TEST: Normalization is now complete and will be auto-saved in the file \n";
			MLConsole() << "SoundplaneAppState.txt. \n";
			MLConsole() << "\n****************************************************************\n\n";
			
			
		}
	}
	else
	{
		if(mWaitSamplesAfterNormalize < waitAfterNormalize)
		{
			mStartupSum += peakZ;
			mWaitSamplesAfterNormalize++;
			if(mTotalSamples % 100 == 0)
			{
				MLConsole() << ".";
			}
		}
		else if(mWaitSamplesAfterNormalize == waitAfterNormalize)
		{
			mWaitSamplesAfterNormalize++;
			mAutoThresh *= 1.5f;
			MLConsole() << "\nOK, done collecting silence again (auto threshold: " << mAutoThresh << "). \n";

			MLConsole() << "\n****************************************************************\n\n";
			MLConsole() << "Now please slide a single finger over the  \n";
			MLConsole() << "Soundplane surface, visiting each area twice \n";
			MLConsole() << "until all the areas are colored green at left.  \n";
			MLConsole() << "Sliding over a key the first time will turn it gray.  \n";
			MLConsole() << "Sliding over a key the second time will turn it green.\n";
			MLConsole() << "\n";
		}
		else if(peakZ > mAutoThresh)
		{
			// normalize input
			mTemp.copy(input);
			mTemp.multiply(mNormalizeMap);
		
			// smooth input	
			mTemp.convolve3x3r(kc, ke, kk);
			mTemp.convolve3x3r(kc, ke, kk);
			mTemp.convolve3x3r(kc, ke, kk);
			
			// get corrected peak
			mPeak = mTemp.findPeak();
			mPeak = mTemp.correctPeak(mPeak.x(), mPeak.y(), 1.0f);							
			Vec2 minPos(2.0, 0.);
			Vec2 maxPos(mWidth - 2., mHeight - 1.);
			mPeak = vclamp(mPeak, minPos, maxPos);
		
			age++; // continue touch
			// get sample from input around peak and normalize
			mIncomingSample.clear();
			mIncomingSample.add2D(m, -mPeak + Vec2(kTemplateRadius, kTemplateRadius)); 
			mIncomingSample.sigMax(0.f);
			mIncomingSample.scale(1.f / mIncomingSample(kTemplateRadius, kTemplateRadius));

			// get integer bin	
			Vec2 binPeak = getBinPosition(mPeak);
			mVisPeak = binPeak - Vec2(0.5, 0.5);		
			int bix = binPeak.x();			
			int biy = binPeak.y();
			// clamp to calibratable area
			bix = clamp(bix, 2, mWidth - 2);
			biy = clamp(biy, 0, mHeight - 1);
			Vec2 bIntPeak(bix, biy);

			// count sum and minimum of all kernel samples for the bin
			int dataIdx = biy*mWidth + bix;
			mDataSum[dataIdx].add(mIncomingSample); 
			mData[dataIdx].sigMin(mIncomingSample); 
			mSampleCount[dataIdx]++;
            
			if(bIntPeak != intPeak1)
			{
				// entering new bin.
				intPeak1 = bIntPeak;
				if(mPassesCount[dataIdx] < kPassesToCalibrate)
				{
					mPassesCount[dataIdx]++;
					mVisSignal(bix, biy) = (float)mPassesCount[dataIdx] / (float)kPassesToCalibrate;
				}
			}			
			
			// check for done
			if(isDone())
			{
				mCalibrateSignal.setDims(kTemplateSize, kTemplateSize, mWidth*mHeight);
				
				// get result for each junction
				for(int j=0; j<mHeight; ++j)
				{
					for(int i=0; i<mWidth; ++i)
					{						
						int idx = j*mWidth + i;
						// copy to 3d signal
						mCalibrateSignal.setFrame(idx, mData[idx]);
					}
				}
				
				getAverageTemplateDistance();
				mHasCalibration = true;
				mActive = false;	
				r = 1;	
							
				MLConsole() << "\n****************************************************************\n\n";
				MLConsole() << "Calibration is now complete and will be auto-saved in the file \n";
				MLConsole() << "SoundplaneAppState.txt. \n";
				MLConsole() << "\n****************************************************************\n\n";
			}	
		}
		else
		{
			age = 0;
			intPeak1 = Vec2(-1, -1);
			mVisPeak = Vec2(-1, -1);
		}
	}
	mTotalSamples++;
	return r;
}

bool TouchTracker::Calibrator::isCalibrating()
{
	return mActive;
}

bool TouchTracker::Calibrator::hasCalibration()
{
	return mHasCalibration;
}

bool TouchTracker::Calibrator::isDone()
{
	// If we have enough samples at each location, we are done.
	bool calDone = true;
	for(int j=0; j<mHeight; ++j)
	{
		for(int i=0; i<mWidth; ++i)
		{
			if(isWithinCalibrateArea(i, j))
			{
				int dataIdx = j*mWidth + i;
				if (mPassesCount[dataIdx] < kPassesToCalibrate)
				{
					calDone = false;
					goto done;
				}
			}
		}
	}
done:	
	return calDone;
}

bool TouchTracker::Calibrator::doneCollectingNormalizeMap()
{
	// If we have enough samples at each location, we are done.
	bool calDone = true;
	for(int j=0; j<mHeight; ++j)
	{
		for(int i=0; i<mWidth; ++i)
		{
			if(isWithinCalibrateArea(i, j))
			{
				if (mNormalizeCount(i, j) < kNormMapSamples)
				{
					calDone = false;
					goto done;
				}
			}
		}
	}
done:	
	return calDone;
}
	
void TouchTracker::Calibrator::setCalibration(const MLSignal& v)
{
	if((v.getHeight() == kTemplateSize) && (v.getWidth() == kTemplateSize))
	{
        mCalibrateSignal = v;
        mHasCalibration = true;
    }
    else
    {
		MLConsole() << "TouchTracker::Calibrator::setCalibration: bad size, restoring default.\n";
        mHasCalibration = false;
    }
}

void TouchTracker::Calibrator::setNormalizeMap(const MLSignal& v)
{
	if((v.getHeight() == mSrcHeight) && (v.getWidth() == mSrcWidth))
	{
		mNormalizeMap = v;
		mHasNormalizeMap = true;
	}
	else
	{
		MLConsole() << "TouchTracker::Calibrator::setNormalizeMap: restoring default.\n";
		mNormalizeMap.fill(1.f);
		mHasNormalizeMap = false;
	}
}


float TouchTracker::Calibrator::getZAdjust(const Vec2 p)
{
	// first adjust z for interpolation based on xy position within unit square
	//
	Vec2 vInt, vFrac;
	p.getIntAndFracParts(vInt, vFrac);
	
	Vec2 vd = (vFrac - Vec2(0.5f, 0.5f));
	return 1.414f - vd.magnitude()*0.5f;
}

float TouchTracker::Calibrator::differenceFromTemplateTouch(const MLSignal& in, Vec2 pos)
{
	static MLSignal a2(kTemplateSize, kTemplateSize);
	static MLSignal b(kTemplateSize, kTemplateSize);
	static MLSignal b2(kTemplateSize, kTemplateSize);

	float r = 1.0;
	int height = in.getHeight();
	int width = in.getWidth();
	MLRect boundsRect(0, 0, width, height);
	
	// use linear interpolated z value from input
	float linearZ = in.getInterpolatedLinear(pos)*getZAdjust(pos);
	linearZ = clamp(linearZ, 0.00001f, 1.f);
	float z1 = 1./linearZ;	
	const MLSignal& a = getTemplate(pos);
	
	// get normalized input values surrounding touch
	int tr = kTemplateRadius;
	b.clear();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			Vec2 vInPos = pos + Vec2((float)i - tr,(float)j - tr);			
			if (boundsRect.contains(vInPos))
			{
				float inVal = in.getInterpolatedLinear(vInPos);
				inVal *= z1;
				b(i, j) = inVal;
			}
		}
	}
	
	int tests = 0;
	float sum = 0.;

	// add differences in z from template
	a2.copy(a);
	b2.copy(b);
    
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			if(b(i, j) > 0.)
			{
				float d = a2(i, j) - b2(i, j);
				sum += d*d;
				tests++;
			}
		}
	}

	// get RMS difference
	if(tests > 0)
	{
		r = sqrtf(sum / tests);
	}	
	return r;
} 

float TouchTracker::Calibrator::differenceFromTemplateTouchWithMask(const MLSignal& in, Vec2 pos, const MLSignal& mask)
{
	static float maskThresh = 0.001f;
	static MLSignal a2(kTemplateSize, kTemplateSize);
	static MLSignal b(kTemplateSize, kTemplateSize);
	static MLSignal b2(kTemplateSize, kTemplateSize);

	float r = 0.f;
	int height = in.getHeight();
	int width = in.getWidth();
	MLRect boundsRect(0, 0, width, height);
	
	// use linear interpolated z value from input
	float linearZ = in.getInterpolatedLinear(pos)*getZAdjust(pos);
	linearZ = clamp(linearZ, 0.00001f, 1.f);
	float z1 = 1./linearZ;	
	const MLSignal& a = getTemplate(pos);
	
	// get normalized input values surrounding touch
	int tr = kTemplateRadius;
	b.clear();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			Vec2 vInPos = pos + Vec2((float)i - tr,(float)j - tr);			
			if (boundsRect.contains(vInPos) && (mask.getInterpolatedLinear(vInPos) < maskThresh))
			{
				float inVal = in.getInterpolatedLinear(vInPos);
				inVal *= z1;
				b(i, j) = inVal;
			}
		}
	}
	
	int tests = 0;
	float sum = 0.;

	// add differences in z from template
	a2.copy(a);
	b2.copy(b);
	a2.partialDiffX();
	b2.partialDiffX();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			if(b(i, j) > 0.)
			{
				float d = a2(i, j) - b2(i, j);
				sum += d*d;
				tests++;
			}
		}
	}

	// get RMS difference
	if(tests > 0)
	{
		r = sqrtf(sum / tests);
	}	
	return r;
}



// --------------------------------------------------------------------------------
#pragma mark utilities

/*
void TouchTracker::dumpTouches()
{
	int c = 0;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		const Touch& t = mTouches[i];
		if(t.isActive())
		{
			debug() << "t" << i << ":" << t << ", key#" << t.key << ", td " << t.tDist << ", dz " << t.dz << "\n";		
			c++;
		}
	}
	if(c > 0)
	{
		debug() << "\n";
	}
}
*/

/*
int TouchTracker::countActiveTouches()
{
	int c = 0;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		const Touch& t = mTouches[i];
		if( t.isActive())
		{
			c++;
		}
	}
	return c;
}
*/

