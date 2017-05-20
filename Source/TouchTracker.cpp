
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

TouchTracker::TouchTracker(int w, int h) :
	mWidth(w),
	mHeight(h),
	mpIn(0),
	mNumNewCentroids(0),
	mNumCurrentCentroids(0),
	mNumPreviousCentroids(0),
	mMatchDistance(2.0f),
	mNumPeaks(0),
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
	mOnThreshold = f*0.1f; 
	mOffThreshold = mOnThreshold * 0.5f; 
	mCalibrator.setThreshold(mOnThreshold);
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
				
		mFilteredInputX = mFilteredInput;
		mFilteredInputY = mFilteredInput;
		
		// convolve input with 3x3 smoothing kernel.
		float kc, kex, key, kk;			
//		kc = 16./32.; kex = 4./32.; key = 2./32.; kk=1./32.;	
//		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
//		mFilteredInput.convolve3x3xy(kc, kex, key, kk);

//		kc = 16./48.; kex = 8./48.; key = 4./48.; kk=2./48.;	
//		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
//		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		
		kc = 4./18.; kex = 3./18.; key = 2./18.; kk=1./18.;
		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		
		kc = 4./8.; kex = 2./8.; key = 0.; kk=0.;	
		mFilteredInputX.convolve3x3xy(kc, kex, key, kk);
		
		kc = 4./8.; kex = 0.; key = 2./8.; kk=0.;	
		mFilteredInputY.convolve3x3xy(kc, kex, key, kk);
		
		// MLTEST
		mCalibratedSignal = mFilteredInput;

		if(mMaxTouchesPerFrame > 0)
		{
			mSpansHoriz = findSpans<kSensorRows, kSensorCols, 0>(mFilteredInputX);
			mPingsHorizRaw = findZ2Pings<kSensorRows, kSensorCols, 0>(mSpansHoriz, mFilteredInputX);			
			
			mSpansVert = findSpans<kSensorCols, kSensorRows, 1>(mFilteredInputY); 
			mPingsVertRaw = findZ2Pings<kSensorCols, kSensorRows, 1>(mSpansVert, mFilteredInputY);
			
			
			
			/*
			
			// cluster pings
			mClustersHorizRaw = clusterPings<kSensorRows, kSensorCols, 0>(mPingsHorizRaw, mFilteredInput);
			mClustersVertRaw = clusterPings<kSensorCols, kSensorRows, 1>(mPingsVertRaw, mFilteredInput);

			bool doFilter = false;
			if(doFilter)
			{
				mClustersHoriz = filterClusters<kSensorRows, kSensorCols, 0>(mClustersHorizRaw, mClustersHorizY1);
				mClustersHorizY1 = mClustersHoriz;			
				mClustersVert = filterClusters<kSensorCols, kSensorRows, 1>(mClustersVertRaw, mClustersVertY1);
				mClustersVertY1 = mClustersVert;
			}
			else
			{
				mClustersHoriz = mClustersHorizRaw;
				mClustersVert = mClustersVertRaw;
			} 
			*/
			
			mKeyStates = pingsToKeyStates(mPingsHorizRaw, mPingsVertRaw);
			mKeyStates = filterKeyStates(mKeyStates, mKeyStates1);
			mKeyStates1 = mKeyStates;
			
			mTouchesRaw = findTouches(mClustersHoriz, mClustersVert, mFilteredInput);
//			mTouchesRaw = findTouches(mPingsHoriz, mPingsVert, mFilteredInput);
		
//			mTouches = filterTouchesSimple(mTouchesRaw, mTouches1, mFilteredInput);
			mTouches1 = mTouches;
			
	//		matchTouches();
			
			// copy filtered spans to output array
			{
				std::lock_guard<std::mutex> lock(mSpansHorizOutMutex);
				mSpansHorizOut = mSpansHoriz;
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
				std::lock_guard<std::mutex> lock(mSpansVertOutMutex);
				mSpansVertOut = mSpansVert;
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
				std::lock_guard<std::mutex> lock(mClustersVertRawOutMutex);
				mClustersVertRawOut = mClustersVertRaw;
			}
			
			{
				std::lock_guard<std::mutex> lock(mClustersVertOutMutex);
				mClustersVertOut = mClustersVert;
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

		filterAndOutputTouches();
		
		{
			std::lock_guard<std::mutex> lock(mTouchesOutMutex);
			mTouchesOut = mTouches;
		}

	}
#if DEBUG   

	
	if(0)
	if (mCount++ > 1000) 
	{
		mCount = 0;
		//dumpTouches();
		
	debug() << "key states v: \n";
		
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
#endif  
}

// find horizontal or vertical spans over which z is higher than a small constant.
template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::findSpans(const MLSignal& in)
{
	// zThresh must be greater than 0. We also want it significantly lower than mOnThreshold
	// because when more candidate spans are generated, discontinuities when they appear / disappear are smaller.
	const float zThresh = 0.0001 + mOnThreshold; 
	
	int dim1 = in.getWidth();
	int dim2 = in.getHeight();
	if(XY) std::swap(dim1, dim2);
	VectorArray2D<ARRAYS, ARRAY_LENGTH> out;
		
	for(int j=0; j<dim2; ++j)
	{
		out.data[j].fill(Vec4::null());	
		int spansInRow = 0;
		
		float spanStart, spanEnd;
		float z = 0.f;
		float zm1 = 0.f;
		spanStart = spanEnd = -1;

		constexpr int margin = XY ? 1 : 0;
		int intStart = 0 - margin;
		int intEnd = dim1 + margin;
		
		for(int i=intStart; i <= intEnd; ++i)
		{
			z = (within(i, 0, dim1)) ? (XY ? in(j, i) : in(i, j)) : 0.f;
			
			if((z >= zThresh) && (zm1 < zThresh))
			{
				// start span
				spanStart = i;
			}
			else if((spanStart >= 0) && ((z < zThresh) || (i == intEnd)) && (zm1 >= zThresh)) 
			{
				// end span when z goes under thresh or active at end of sensor
				spanEnd = clamp(i, 0, dim1 - 1);
				if(spanEnd > spanStart)
				{
					if(spansInRow < ARRAY_LENGTH)
					{
						out.data[j][spansInRow++] = Vec4(spanStart, spanEnd, 0.f, 0.f); 		
					}
				}
				spanStart = spanEnd = -1;
			}
			zm1 = z;
		}
	}
	return out;
}

// for each horiz or vert span, find one or more subspans over which the 2nd derivative of z is negative.
//
// may be more accurate than going right to findZ2Pings() when only one ping is in a span.
// 
template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::findZ2Spans(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& intSpans, const MLSignal& in)
{
	const float ddzThresh = -0.0000f;
	//	const float kMinSpanLength = 0.f;
	int dim1 = in.getWidth();
	int dim2 = in.getHeight();
	if(XY) std::swap(dim1, dim2);
	
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;
	int j = 0;
	for(auto array : intSpans.data)
	{
		y.data[j].fill(Vec4::null());
		
		for(auto intSpan : array)
		{
			if(!intSpan) break;
			
			float spanStart, spanEnd;
			float spanStartZ;
			float z = 0.f;
			float zm1 = 0.f;
			float dz = 0.f;
			float dzm1 = 0.f;
			float ddz = 0.f;
			float ddzm1 = 0.f;
			
			// need to iterate before and after to get derivatives flowing
			constexpr int margin = 2;
			
			int intStart = intSpan.x() - margin;
			int intEnd = intSpan.y() + margin;
			spanStart = spanEnd = -1;
			bool spanActive = false;
			
			for(int i = intStart; i <= intEnd; ++i)
			{
				z = (within(i, 0, dim1)) ? (XY ? in(j, i) : in(i, j)) : 0.f;
				dz = z - zm1;
				ddz = dz - dzm1;
				
				if((ddz < ddzThresh) && (ddzm1 >= ddzThresh))
				{ 
					// start span
					float m = ddz - ddzm1;
					float xa = (ddzThresh - ddzm1)/m;
					spanStart = i - 2.f + xa; 	
					spanStart = clamp(spanStart, intStart + 0.f, intEnd + 0.f);
					spanStartZ = z; 
					spanActive = true;
				}
				else if((spanActive) && ((ddz >= ddzThresh) || (i == intEnd)) && (ddzm1 < ddzThresh))
				{	
					// end span if ddz goes back above 0 or the int span ends		
					float m = ddz - ddzm1;				
					float xa = (ddzThresh - ddzm1)/m;
					spanEnd = i - 2.f + xa; 
					spanEnd = clamp(spanEnd, intStart + 0.f, intEnd + 0.f);
			//		if(spanEnd > spanStart)
					{
						appendVectorToRow(y.data[j], Vec4(spanStart, spanEnd, 0.f, 0.f));
					}
					spanStart = spanEnd = -1;
					spanActive = false;
				}
				
				zm1 = z;
				dzm1 = dz;
				ddzm1 = ddz;
			}						
		}
		j++;
	}
	return y;
}


// new ping finder using z'' minima and parabolic interpolation
template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::findZ2Pings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& intSpans, const MLSignal& in)
{
	constexpr float ddzThresh = 0.0002f;
	constexpr float kScale = XY ? 0.200f : 0.600f; // curvature per linear distance is different in x and y

	// TEST
	float maxK = 0.0f;
	float maxZ = 0.0f;
	
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;
	int j = 0;
	for(auto array : intSpans.data)
	{
		y.data[j].fill(Vec4::null());
		
		for(auto intSpan : array)
		{
			if(!intSpan) break;
			
			float spanStart, spanEnd;

			float z = 0.f;
			float zm1 = 0.f;
			float zm2 = 0.f;
			float dz = 0.f;
			float dzm1 = 0.f;
			float ddz = 0.f;
			float ddzm1 = 0.f;
			float ddzm2 = 0.f;
			
			// need to iterate before and after the span to get derivatives flowing
			constexpr int margin = 2;
			
			int intStart = intSpan.x() - margin;
			int intEnd = intSpan.y() + margin;
			spanStart = spanEnd = -1;
			
			for(int i = intStart; i <= intEnd; ++i)
			{
				z = (within(i, 0, static_cast<int>(ARRAY_LENGTH))) ? (XY ? in(j, i) : in(i, j)) : 0.f;
				dz = z - zm1;
				ddz = dz - dzm1;
				
				// find ddz minima
				if((ddzm1 < ddz) && (ddzm1 < ddzm2) && (ddzm1 < -ddzThresh))
				{ 
					// get peak by quadratic interpolation
					float a = ddzm2;
					float b = ddzm1;
					float c = ddz;
					float k = (a - 2.f*b + c)/2.f * kScale * 10.f; // curvature
					float p = ((a - c)/(a - 2.f*b + c))*0.5f;
					float x = i - 2.f + p;
					
					// TODO interpolate z?
					
					if(within(x, intSpan.x(), intSpan.y()))
					{
						// TEST
						if(k > maxK)maxK = k;
						if(z > maxZ)maxZ = z;
						
						appendVectorToRow(y.data[j], Vec4(x, zm2, 0.f, 0.f));
					}
				}
				
				zm2 = zm1;
				zm1 = z;
				dzm1 = dz;
				ddzm2 = ddzm1;
				ddzm1 = ddz;
			}						
		}
		j++;
	}
	
	
	
	
	
//	if(maxK > 0.f)
//	debug() << "max k: " << maxK  << "max z: " << maxZ << "\n";
	return y;
}


// older ping finder using span ends
template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::findPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inSpans, const MLSignal& in)
{
	// this is the maximum width of a span containing only one ping. depends on smoothing kernel.
	const float k1 = XY ? 3.4f : 4.7f;
	const float fingerWidth = k1 / 2.f;
	
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;
	int j = 0;
	
	for(auto array : inSpans.data)
	{
		y.data[j].fill(Vec4::null());
		
		for(auto s : array)
		{
			if(!s) break;
			
			float a = s.x(); // start 
			float b = s.y(); // end			
			float d = b - a;
			if(d < k1)
			{
				float pingPos = (a + b)/2.f;
				float pingZ = XY ? in.getInterpolatedLinear(j, pingPos) : in.getInterpolatedLinear(pingPos, j);
				appendVectorToRow(y.data[j], Vec4(pingPos, 0.f, pingZ, 0.f));
			}
			else
			{
				float pingPos = a + fingerWidth/2.;
				float pingZ = XY ? in.getInterpolatedLinear(j, pingPos) : in.getInterpolatedLinear(pingPos, j);
				appendVectorToRow(y.data[j], Vec4(pingPos, 0.f, pingZ, 0.f));
				
				pingPos = b - fingerWidth/2.;
				pingZ = XY ? in.getInterpolatedLinear(j, pingPos) : in.getInterpolatedLinear(pingPos, j);
				appendVectorToRow(y.data[j], Vec4(pingPos, 0.f, pingZ, 0.f));							
			}
		}
		j++;
	}
	return y;
}

template<size_t ARRAY_LENGTH>
Vec4 getLeftNeighbor( std::array<Vec4, ARRAY_LENGTH> array, float x, bool dd)
{
	if(dd)
	debug() << "NEINGHBR fdor " << x << ": ";
	
	if(!array[0]) return Vec4::null();
	
	Vec4 left = Vec4::null();
	for(Vec4 ping : array)
	{
		
		if(dd)  debug() << ping ;
		if(!ping) break;
		if(ping.x() <= x)
		{
			if(dd) debug() << " [L] ";
			left = ping;
		}
		else
		{
			if(dd) debug() << " [R] ";
			break;
		}
	}
	
	if(dd) debug() << " ret:  " << left << "\n";

	return left;
}

template<size_t ARRAY_LENGTH>
Vec4 getRightNeighbor( std::array<Vec4, ARRAY_LENGTH> array, float x)
{
	if(!array[0]) return Vec4::null();
	Vec4 right = Vec4::null();
	for(Vec4 ping : array)
	{
		if(!ping) break;
		if(ping.x() > x) 
		{
			right = ping;
			break;
		}
	}
	return right;
}


template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::filterPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPings, const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPingsY1)
{
	//	const float kR = 8.0f; // max filter radius
	const float k1 = XY ? 3.4f : 4.7f;
	
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;
	int j = 0;
	
	for(auto array : inPings.data)
	{
		// copy to output array
		y.data[j] = array;
		
		// get same row in previous pings
		auto arrayY1 = inPingsY1.data[j];
		
		bool doDebug = !XY && (j == 4);
		
		int inN = countPings(array);
		if(doDebug)
		{
			if(inN > 0)
			{
			debug() << "\nin: ";
			for(Vec4& ping : array)
			{
				if(!ping) break;
				debug() << ping;
			}
			debug() << "\n";
			debug() << "y1: ";
			for(Vec4& ping : arrayY1)
			{
				if(!ping) break;
				debug() << ping;
			}
			debug() << "\n";
			}
		}
		
		
		for(Vec4& ping : y.data[j])
		{
			if(!ping) break;
			
			float x = ping.x();
			float z = ping.z();
			float newx = x;

			// set filter effect radius, increase radius when pressure is light
			const float rMin = k1/2.f;
			const float rMax = k1* 2.f;
			const float zThresh = 0.003f;
			float zOffset = (zThresh - z)/zThresh;
			zOffset = clamp(zOffset, 0.f, 1.f);
			float r = rMin + (rMax - rMin)*zOffset;
			
			r = rMax; // TEMP
			
			/*
			// get weighted centroid of all previous pings in triangular window radius r
			int n = 0;
			float cxSum = 0.f;
			float czSum = 0.f;
			for(Vec4& prev : arrayY1)
			{
				if(!prev) break;
				float px = prev.x();
				float pz = prev.z();	
				if(within(px, x - r, x + r))
				{
					float windowWeight = 1.0f - fabs(px - x)/r;
					// ? float zWeight = pz/z;
					cxSum += px*pz*windowWeight;
					czSum += pz*windowWeight;
					n++;
				}
			}
			if(doDebug)
			{
				debug() << "n = " << n << "\n";
				debug() << "r = " << r << "\n";
			}
			*/
			
			// get closest previous ping within window radius r

			float px;
			float minPos;
			float minDist = MAXFLOAT;
			float windowWeight;
			bool found = false;
			for(Vec4& prev : arrayY1)
			{
				if(!prev) break;
				float cx = prev.x();
				float dist = fabs(cx - x);
				if(within(cx, x - r, x + r))
				{
					if(dist < minDist)
					{
						// windowWeight = 1.0f - dist/r;
						// ? float zWeight = pz/z;
						minPos = cx;
						minDist = dist;
						found = true;
					}
				}
			}
			
			
			if(found)
			{
				//float cx = cxSum/czSums
				
				// get filter coeff
				float dr = fabs(minPos - x);				
				float cr = (dr/r);
				
				//cr *= cr;
				cr = 1.0f - cr;
				
	//			float k = clamp(cr, 0.f, 1.f);	
	//			
				// filter position and get variance
				
				if(doDebug)
				{
					debug() << "    x = " << x << "    px = " << minPos << "\n";
				}
				
				float k = 0.99f; // TODO separate x/y
				newx = k*minPos + (1.0f - k)*x;

			}
			else
			{
				// reset variance and position
				if(doDebug)
				{
					debug() << " ### \n";
				}
				
				newx = x;
				
			}
			
			
			
			ping.setX(newx);
		}
		
		if(inN > 0)
		if(doDebug)
		{
			debug() << "\nout: ";
			for(Vec4 ping : y.data[j])
			{
				if(!ping) break;
				debug() << ping;
			}
			debug() << "\n";
		}
		
		j++;
	}
	return y;
}


template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::filterClusters(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPings, const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPingsY1)
{
	//	const float kR = 8.0f; // max filter radius
	const float k1 = XY ? 3.4f : 4.7f;
	const float kR = k1 / 4.f;
	
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;
	int j = 0;
	
	for(auto array : inPings.data)
	{
		// copy to output array
		y.data[j] = array;
		
		// get same row in previous pings
		auto arrayY1 = inPingsY1.data[j];
		
		bool doDebug = false;//!XY && (j == 4);
		
		if(doDebug)
		{
			debug() << "\nin: ";
			for(Vec4& ping : array)
			{
				if(!ping) break;
				debug() << ping;
			}
			debug() << "\n";
			debug() << "y1: ";
			for(Vec4& ping : arrayY1)
			{
				if(!ping) break;
				debug() << ping;
			}
			debug() << "\n";
		}
		
		
		for(Vec4& ping : y.data[j])
		{
			if(!ping) break;
			
			float x = ping.x();
			float z = ping.z();
			float newx = x;
			
			Vec4 leftNeighbor = getLeftNeighbor(arrayY1, x, false);
			Vec4 rightNeighbor = getRightNeighbor(arrayY1, x);
			
			if(doDebug)
			{
				debug() << "    left: " << leftNeighbor << " right: " << rightNeighbor << "\n";
			}
			
			float cl = 0.f;
			float cr = 0.f;
			float xl, xr;
			float dl, dr;
			//	if(leftNeighbor)
			{
				xl = leftNeighbor.x();
				dl = x - xl;
				cl = (dl/kR);
				
				cl *= cl;
				cl = 1.0f - cl;
				cl = clamp(cl, 0.f, 1.f);
			}
			//	if(rightNeighbor)
			{
				xr = rightNeighbor.x();
				dr = xr - x;				
				cr = (dr/kR);
				
				
				cr *= cr;
				cr = 1.0f - cr;
				
				cr = clamp(cr, 0.f, 1.f);
				
			}
			
			
			//		float kl = nkl*kM;
			//		float kr = nkr*kM;
			
			if(leftNeighbor && rightNeighbor)
			{
				float fxl = cl*xl + (1.0f - cl)*x;
				float fxr = cr*xr + (1.0f - cr)*x;
				newx = (fxl + fxr)/2.f;
				
				if(doDebug) 
				{
					debug() << "B";
					debug() << "    cl: " << cl << " , cr: " << cr << "\n";
				}
			}
			else if(leftNeighbor)
			{
				newx = cl*xl + (1.0f - cl)*x;
				
				if(doDebug) debug() << "L";
				if(doDebug)  debug() << "    dl: " << dl << " cl: " << cl << "\n";
				if(doDebug)  debug() << "    x: " << x << " newx: " << newx << " xl: " << xl << "\n";
				
			}
			else if(rightNeighbor)
			{
				newx = cr*xr + (1.0f - cr)*x;
				
				if(doDebug) debug() << "R";
				if(doDebug)  debug() << "    dr: " << dr << " cr: " << cr << "\n";
				if(doDebug)  debug() << "    x: " << x << " newx: " << newx << " xr: " << xr  << "\n";
				
			}
			else
			{
				if(doDebug) debug() << "X";
			}
			
			ping.setX(newx);
		}
		
		if(doDebug)
		{
			debug() << "\nout: ";
			for(Vec4 ping : y.data[j])
			{
				if(!ping) break;
				debug() << ping;
			}
			debug() << "\n";
		}
		
		j++;
	}
	return y;
}


// pings from findPingsZ2 are (x, k, z)

template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::clusterPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPings, const MLSignal& in)
{
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;
	int j = 0;
	
	// single-touch width constant
	const float k1 = XY ? 3.4f : 4.7f;
	
	for(auto pingsArray : inPings.data)
	{
		y.data[j].fill(Vec4::null());
		
		// copy 1d array of pings to local array and sort by z
		std::array<Vec4, ARRAY_LENGTH> sortedPingsArray = pingsArray;
		std::sort(sortedPingsArray.begin(), sortedPingsArray.end(), [](Vec4 va, Vec4 vb){return va.z() > vb.z();});
		
		// start with no clusters 
		std::array<Vec4, ARRAY_LENGTH> clusterArray;
		clusterArray.fill(Vec4::null());
		
		bool doDebug = false;//(!XY && (j == 4));
			
		for(Vec4 ping : sortedPingsArray)
		{
			if(!ping) break;
			
			// set cluster radius, increase radius when pressure is light
			const float rMin = k1/2.f;
			const float rMax = k1*2.f;
			const float zThresh = 0.003f;
			float zOffset = (zThresh - ping.z())/zThresh;
			zOffset = clamp(zOffset, 0.f, 1.f);
			float r = rMin + (rMax - rMin)*zOffset;
						
			// TODO if distant, fade out z for continuity?
			
			insertPingIntoArray(clusterArray, ping, r, false);
		}
		
		
		// clusters now have running z sums in them -- replace with z
		
		for(Vec4& ping : clusterArray)
		{
			if(!ping) break;
			const float w = ARRAY_LENGTH + 0.f;
			const float x = ping.x();
			if(!(within(x, 0.f, w)))
			{
				debug() << "!! ping: " << ping << ", w: " << w << " !! \n";
			}
			Vec2 pos = XY ? Vec2(j + 0.f, x) : Vec2(x, j + 0.f);
			float zFromInput = in.getInterpolatedLinear(pos);

//			if(doDebug)
//	debug() << "#" << zFromInput ;
			
//			ping.setZ(0.001f);//(zFromInput);
			ping.setZ(zFromInput);
		}
		
		y.data[j] = clusterArray;
		
		int pings = countPings(pingsArray);
		int clusters = countPings(clusterArray);

		if(doDebug)
		{
			if(pings != clusters)
			{
				
			debug() << "pings: " << pings <<  " clusters: " << clusters << "\n";
				
				debug() << "pings: [";
				for(int i=0; i<pings; ++i)
				{
					debug() << pingsArray[i];// ? "*" : "-";
				}
				debug() << "]\n";
				
				debug() << "clusters: [";
				for(int i=0; i<clusters; ++i)
				{
					debug() << clusterArray[i];// ? "*" : "-";
				}
				debug() << "]\n\n";
			}
		}
		
		j++;
	}
	return y;
}

float triWindow(float x, float r)
{
	float y = (x < 0) ? 1.0f + x/r : 1.0f - x/r;
	return(clamp(y, 0.f, 1.f));
}

TouchTracker::KeyStates TouchTracker::pingsToKeyStates(const TouchTracker::VectorsH& pingsHoriz, const TouchTracker::VectorsV& pingsVert)
{
	//mKeyRangeX = MLRange (mKeyRect.left(), mKeyRect.left() + mKeyRect.width(), margin, mViewWidth - margin);
	//mKeyRangeY = MLRange (mKeyRect.top(), mKeyRect.top() + mKeyRect.height(), margin, mViewHeight - margin);

	float rx = 1.0f;
	float ry = 1.0f;
	
	MLRange xRange(3.5f, 59.5f);
	xRange.convertTo(MLRange(1.f, 29.f));
	
	MLRange yRange(1.25, 5.75);  // Soundplane A as measured
	yRange.convertTo(MLRange(1.f, 4.f));
	
	TouchTracker::KeyStates centroids;

	int j = 0;
	for(auto pingsArray : pingsHoriz.data)
	{
		for(Vec4 ping : pingsArray)
		{
			if(!ping) break;
			
			float px = xRange(ping.x());
			float py = yRange(j);
			
			// add ping to all keys it overlaps
			//for(int ky=firstKeyY; ky <= lastKeyY; ++ky)
			int ky = py;
			{
				//				float yWindow = triWindow(ky - py - 0.5f, ry);
				int kx = px;
				int firstKeyX = kx - 1;
				int lastKeyX = kx + 1;
				kx = clamp(kx, 0, kKeyCols - 1);
				firstKeyX = clamp(firstKeyX, 0, kKeyCols - 1);
				lastKeyX = clamp(lastKeyX, 0, kKeyCols - 1);
				
				
				for(int destKeyX=firstKeyX; destKeyX <= lastKeyX; ++destKeyX)
				{
					//float xWindow = triWindow(px - kx - 0.5f, rx);
					
					float cx = px - destKeyX;
					float cz = 1.0f;//*xWindow;
					
					Vec4& key = (centroids.data[ky])[destKeyX];
					
					key.setX(key.x() + cx*cz); // TEST
					key.setZ(key.z() + cz);							
				}
			}
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
				
			float px = xRange(i);// clamp(xRange(i), 0.f, kKeyCols + 0.f);
			float py = yRange(ping.x());// clamp(yRange(ping.x()), 0.f, kKeyRows + 0.f);
			
			// add ping to all keys it overlaps
			
			// add to key and neighbors
			//		for(int kx=firstKeyX; kx <= lastKeyX; ++kx)
			int kx = px;
			kx = clamp(kx, 0, kKeyCols - 1);
			{
				
				int ky = py;
				int firstKeyY = ky - 1;
				int lastKeyY = ky + 1;
				ky = clamp(ky, 0, kKeyRows - 1);
				firstKeyY = clamp(firstKeyY, 0, kKeyRows - 1);
				lastKeyY = clamp(lastKeyY, 0, kKeyRows - 1);
				
				// add ping to key centroid and neighbors
				for(int destKeyY=firstKeyY; destKeyY <= lastKeyY; ++destKeyY)
				{
					float cy = py - destKeyY;
					float cz = 1.0f;//pz;//*yWindow;
					
					Vec4& key = (centroids.data[destKeyY])[kx];
					
					key.setY(key.y() + cy*cz); // TEST
					key.setW(key.w() + cz);							
				}
			}
		}
		
		if(doDebug && (n > 0))
		{
			debug() << "\n";
		}
		
		i++;
	}
	
	
	// reduce centroids
	for(auto& centroidsArray : centroids.data)
	{
		for(Vec4& centroid : centroidsArray)
		{
			
			float cx = centroid.x();
			float cz = centroid.z();
			if(cz > 0.f)
			{
				centroid.setX(cx/cz);
			}
			else
			{
				centroid.setX(0.5f);
			}
			
			
			min / max or ... ? 
			
			float cy = centroid.y();
			float cw = centroid.w();
			if(cw > 0.f)
			{
				centroid.setY(cy/cw);
			}
			else
			{
				centroid.setY(0.5f);
			}

			// filter will fill in variance here
			centroid.setZ(0.5f);
			centroid.setW(0.5f);

		}
	}
	

	
	return centroids;
}


TouchTracker::KeyStates TouchTracker::filterKeyStates(const TouchTracker::KeyStates& x, const TouchTracker::KeyStates& ym1)
{
	const float k = 0.05f;

	// TEST
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
			
			float xn = k*xKey.x() + (1.0f - k)*ym1Key.x();
			float yn = k*xKey.y() + (1.0f - k)*ym1Key.y();
			
			float dx = xKey.x() - xn;
			float dy = xKey.y() - yn;
			
			float xvn = k*fabs(dx*dx) + (1.0f - k)*ym1Key.z(); // z holds xvn
			float yvn = k*fabs(dy*dy) + (1.0f - k)*ym1Key.w(); // z holds xvn
			
			
			yKey = Vec4(xn, yn, xvn, yvn);
			
			if((i == 16) && (j == 2))
			{
				debug() << yKey << "\n";
			}

			
			i++;
		}
		j++;
	}
	return y;

}


// find touches by looking for adjacent x and y clusters. 
// TODO based on data, looking only at vertical pings could work, avoiding all the horiz computation. Revisit.  
std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::findTouches(const TouchTracker::VectorsH& pingsHoriz, const TouchTracker::VectorsV& pingsVert, const MLSignal& z)
{
	std::array<Vec4, kMaxTouches> y;
	y.fill(Vec4::null());
	
	int j = 0;
	int touches = 0;
	for(auto row : pingsHoriz.data)
	{
		for(auto horizPing : row)
		{
			if(!horizPing) break;
			
			float hx = horizPing.x();
			int hxi = hx;
			int hyi = j;
			
			if(within(hxi, 0, kSensorCols))
			{
				// look for vertical pings nearby
				auto colA = pingsVert.data[hxi];
				auto isCloseToHoriz = [&](Vec4 p){ int vyi = p.x(); return (vyi == hyi); };				
				auto vertItA = std::find_if(colA.begin(), colA.end(), isCloseToHoriz ); 	
				
				auto colB = pingsVert.data[hxi + 1];				
				auto vertItB = std::find_if(colB.begin(), colB.end(), isCloseToHoriz ); 
				
				// simple x near y
				if(vertItA != colA.end())
				{
					Vec4 vertPingA = *vertItA;

					
					// y curvature is more stable
					// float kxy = vertPing.y(); // TEST;// (horizPing.y() + vertPing.y())/2.f;
					
					Vec2 xyPosA (horizPing.x(), vertPingA.x());
					
		//			Vec2 xyPosB (horizPing.x(), vertPingB.x());
		//			Vec2 xyPosAvg = (xyPosA + xyPosB)/2.f;
					float zFromInput = z.getInterpolatedLinear(xyPosA);

					
					
					//			debug() << "k:" << kxy;
					y[touches++] = Vec4(xyPosA.x(), xyPosA.y(), zFromInput, 0);
				}
				
				
				// try: peak x near peak y
				
				
				/*
				 if((vertIt != col.end()) && (vertItB != colB.end()))
				 {
					Vec4 vertPing = *vertIt;
					Vec4 vertPingB = *vertItB;
					float yAvg = (vertPing.x() + vertPingB.x())/2.f;
					float zAvg = (vertPing.y() + vertPingB.y())/2.f;
					y.data[j][rowIntersections++] = Vec4(horizPing.x(), yAvg, zAvg, 0);
					//vertItB++;
				 }
				 */
				
				/*
				 while((vertIt != col.end()) && isCloseToHoriz(*vertIt) & (rowIntersections < kSensorCols))
				 {
					// TODO average surrounding x and y 
					Vec4 vertPing = *vertIt;
					y.data[j][rowIntersections++] = Vec4(horizPing.x(), vertPing.x(), horizPing.y(), 0);
					vertIt++;
				 }*/
			}
 		}
		j++;
	}
	
	if(0) // TEST
	{
	if(touches == 1) debug() << ".";
		else if(touches == 2) debug() << "=";
			else if (touches > 2) debug() << ">";
	}
	
//	debug() << "tocuhes: " << touches << "\n";
	return y;
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

std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::filterTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& x, const std::array<Vec4, TouchTracker::kMaxTouches>& x1, const MLSignal& z)
{
	// when a touch is removed from one frame to the next, a placeholder may need to be inserted to maintain the order
	const float kMaxConnectDist = 8.0f;
	
	std::array<Touch, kMaxTouches> prevTouches{};
	std::array<Touch, kMaxTouches> currTouches{};
	std::array<Touch, kMaxTouches> newTouches{};
	
	// count incoming touches
	int m, n;
	for(m = 0; (m < x1.size()) && x1[m]; m++) {}
	for(n = 0; (n < x.size()) && x[n]; n++) {}
	//	m = clamp(m, 0, mMaxTouchesPerFrame);
	//	n = clamp(n, 0, mMaxTouchesPerFrame);
	
	// convert to Touches
	for(int i=0; i < n; ++i)
	{
		currTouches[i] = vec4ToTouch(x[i]);
		currTouches[i].currIdx = i;
	}
	for(int j=0; j < m; ++j)
	{
		prevTouches[j] = vec4ToTouch(x1[j]);
		prevTouches[j].currIdx = j;
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
			if(prev.age > 0) // don't match to dead touches
			{
				Vec2 prevPos(prev.x, prev.y);
				Vec2 dab = currPos - prevPos;
				float dist = dab.magnitude(); // NOTE (aPos - bPos).magnitude FAILS! TODO	
				//	if(dist < kMaxConnectDist)
				{
					if(dist < currTouches[i].minDist)
					{
						currTouches[i].prevIdx = j;
						currTouches[i].minDist = dist;
					}
				}
			}
		}
	}
	
	// sort current touches by z
	std::sort(currTouches.begin(), currTouches.begin() + n, [](Touch a, Touch b){ return a.z > b.z; } );
	
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
			Touch& closestPrevTouch = prevTouches[connectedIdx];
			
			if (!closestPrevTouch.occupied)
			{
				// touch is continued
				// write new touch and occupy previous
				
				// TODO connected touch filter
				newTouches[connectedIdx] = currTouches[i];
				
				closestPrevTouch.occupied = true;
				
				// increment age
				newTouches[connectedIdx].age = (closestPrevTouch.age + 1);
				//	newTouches[connectedIdx].z = 0.005f;
				
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
				// touch is new, no filtering
				newTouches[freeIdx] = currTouches[i];
				
				// reset age
				newTouches[freeIdx].age = 1;
				maxOccupiedIdx = max(maxOccupiedIdx, freeIdx);
			}
		}
		newSlotsRemaining--;
		if(!newSlotsRemaining) break;
	}
	
	const int kTouchDecayFrames = 1000;
	
	// fill new touches with previous touches, if prev indices are not occupied
	if(m > 0)
	{
		if(newSlotsRemaining)
		{
			for(int j=0; j<m; ++j)
			{
				int oldAge = prevTouches[j].age;
				if(oldAge != 0)
				{
					// if new touch at same index is unoccupied
					if(newTouches[j].age == 0)
					{
						int newAge;
						if(oldAge > 0)
						{
							// start decay
							newAge = -kTouchDecayFrames;
						}
						else
						{
							// decay
							newAge = oldAge + 1;
						}
						
						// debug() << "D" << j << " " << oldAge << " -> " << newAge << "\n";
						
						// TODO decay filter
						newTouches[j] = prevTouches[j];
						
						newTouches[j].age = newAge;
						maxOccupiedIdx = max(maxOccupiedIdx, j);
					}
				}
			}
		}
	}
	
	// convert back to Vec4
	std::array<Vec4, kMaxTouches> y;	
	y.fill(Vec4::null());
	for(int i=0; i <= maxOccupiedIdx; ++i)
	{
		y[i] = touchToVec4(newTouches[i]);
	}
	
	// debug
	if(0)
		if(maxOccupiedIdx >= 0)
		{
			for(int i=0; i <= maxOccupiedIdx; ++i)
			{
				debug() << y[i] << " "; 
			}
		}
	
	return y;
}

// for testing, update ages in the simplest possible way to see results, rule out problems with complicated filter/matching 
std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::filterTouchesSimple(const std::array<Vec4, TouchTracker::kMaxTouches>& x, const std::array<Vec4, TouchTracker::kMaxTouches>& x1, const MLSignal& z)
{
	std::array<Vec4, TouchTracker::kMaxTouches> y = x;
	
	// count incoming touches
	int n;
	for(n = 0; (n < y.size()) && y[n]; n++) {}
	
	// sort by z
	std::sort(y.begin(), y.begin() + n, [](Vec4 a, Vec4 b){ return a.z() > b.z(); } );
	
	// increment ages if a touch is present
	for(int i=0; i<n; ++i)
	{
		if(x1[i])
		{
			int age = x1[i].w();
			y[i].setW( age + 1) ;
		}
	}
	
	return y;
}

std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::filterTouchesXX(const std::array<Vec4, TouchTracker::kMaxTouches>& in)
{
	// cluster intersections to get touches. 
	// light intersections cluster more easily. 
	
	std::array<Vec4, kMaxTouches> y;
	y.fill(Vec4()); 
	
	int currentGroup = 1;
	// int n = in.size();
	int n= 0;
	for(;n < in.size(); n++)
	{
		if(!in[n]) break;	
	}
	
//	if(n> 0)
//	debug() << "ints: " << n << "\n";
	
	// sort by z
	std::array<Vec4, TouchTracker::kMaxTouches> intersections = in;
	std::sort(intersections.begin(), intersections.end(), [](Vec4 va, Vec4 vb){return va.z() > vb.z();});
	
	for(int i=0; i<n; ++i)
	{
		Vec4 a = intersections[i];
		
		// TODO this creates a discontinuity! 
				
		// PROBLEM
		
		// get radius we can move this touch to combine with others		
		const float xDist = 2.0f;
		const float yDist = 1.5f;
		
		// TODO increase radius for light touches?
		float distScale = 1.0f;
		
		Vec4 c;
		float maxZ = 0.f;
		bool found = false;
		int foundIdx = -1;
		
		// get intersection of maximum z within radius maxDist
		for(int j=0; j<n; ++j)
		{
			if(j != i)
			{
				Vec4 b = intersections[j];
				Vec2 ab = a.xy() - b.xy();
				ab.setX(ab.x()/(xDist*distScale)); 
				ab.setY(ab.y()/(yDist*distScale)); 
				float dist = ab.magnitude(); // TODO magnitude(ab);
				
				if(dist < 1.f)
				{
					if(b.z() > maxZ)
					{
						found = true;
						foundIdx = j;
						maxZ = b.z();
					}
				}
			}
		}

		// set groups
		if((found) && (maxZ > a.z()))
		{
			int groupOfB = intersections[foundIdx].w();
			if(!groupOfB)
			{
				intersections[foundIdx].setW(currentGroup++);
			}
			intersections[i].setW(intersections[foundIdx].w());
		}
		else
		{
			intersections[i].setW(currentGroup++);
		}
	}
	
	
	// collect groups into new touches
	int touches = 0;
	for(int g=1; g<currentGroup; ++g)
	{
		Vec4 centroid;
		int centroidSize = 0;
		float maxZ = 0.f;
		for(int i=0; i<n; ++i)
		{
			if(intersections[i].w() == g)
			{
				Vec3 a = intersections[i];
				a.setX(a.x()*a.z());
				a.setY(a.y()*a.z());
				centroid += a;
				centroidSize++;
				if(a.z() > maxZ)
				{
					maxZ = a.z();
				}
			}
		}
		float tz = centroid.z(); // z sum
		centroid /= tz;
		
//		float zFromInput = z.getInterpolatedLinear(Vec2(centroid.x(), centroid.y()));
		
//		centroid.setZ(tz * 10.f);  // curvature
		//centroid.setZ(zFromInput);
		
//		float zMean = tz / (centroidSize + 0.f);
//		centroid.setZ(maxZ);

		centroid.setW(1); // age
		
		// TODO examine 
		
	//	if(zFromInput > mOnThreshold)
		{
			y[touches++] = centroid;
			if(touches >= mMaxTouchesPerFrame) break;
		}
	}
	// zero-terminate (not null!)
	if(touches < kMaxTouches)
	{
		y[touches] = Vec4();
	}
	return y;
}


void TouchTracker::matchTouches()
{		
	// sort by age so that newest touches will be matched last
	std::sort(mTouches.begin(), mTouches.end(), [](Vec4 a, Vec4 b){return a.w() > b.w();});

	// match mTouches with previous touches in mTouches1 and put results in workingTouches
	std::array<Vec4, kMaxTouches> workingTouches;	
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Vec4 u = mTouches[i];
		if(u != Vec4()) 
		{
			float minDist = MAXFLOAT;
			int minIdx = 0;
			
			for(int j=0; j<mMaxTouchesPerFrame; ++j)
			{
				Vec4 v = mTouches1[j];
				
				if(workingTouches[j] == Vec4())
				{
					Vec2 u2(u.x(), u.y());
					Vec2 v2(v.x(), v.y());					
					float uvDist = (u2 - v2).magnitude();
					
					if(uvDist < minDist)
					{
						minDist = uvDist;
						minIdx = j;
					}		
				}
			}
			workingTouches[minIdx] = u;			 
		}
	}
	
	// copy / merge all touches from workingTouches to mTouches, including empty ones
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		// increment ages
		Vec4 u = workingTouches[i];
		Vec4 v = mTouches[i];
		int age = u.w();
		int age1 = v.w();
		int newAge = 0;
		if(age)
		{
			newAge = age1 + 1;
		}
		else
		{
			newAge = 0;
		}
		mTouches[i] = Vec4(u.x(), u.y(), u.z(), newAge);
	}
	
	// store history
	mTouches1 = mTouches;
}

void TouchTracker::filterAndOutputTouches()
{
	MLSignal& out = *mpOut;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Vec4 t = mTouches[i];
		
		out(xColumn, i) = t.x();
		out(yColumn, i) = t.y();
		out(zColumn, i) = (t.z() - mOnThreshold) * mZScale;
		out(ageColumn, i) = t.w();
	}
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

