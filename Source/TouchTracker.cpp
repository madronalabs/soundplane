
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
void appendSpanToRow(std::array<Vec4, ROW_LENGTH>& row, Vec4 b)
{
	// if full (last element is not null), return
	if(row[ROW_LENGTH - 1]) { debug() << "!"; return; }
	
	auto firstNull = std::find_if(row.begin(), row.end(), [](Vec4 a){ return !bool(a); });
	*firstNull = b;
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


/*
Touch::Touch() : 
	x(0), y(0), z(0), dz(0), zf(0), zf10(0.), dzf(0.), 
	key(-1), age(0), retrig(0), releaseCtr(0)
{
}

Touch::Touch(float px, float py, float pz, float pdz) : 
	x(px), y(py), z(pz), dz(pdz), zf(0.), zf10(0.), dzf(0.), 
	key(-1), age(0), retrig(0), releaseCtr(0)
{
}
*/

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
	mTemplateThresh(0.003),
	mOverrideThresh(0.01),
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
	mCalibrationProgressSignal.setDims(w, h);

	
	// clear previous spans
	for(auto& row : mSpansHoriz1.data)
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
	const float kHysteresis = 0.001f;
	mOffThreshold = f; 
	mOnThreshold = mOffThreshold + kHysteresis; 
	mOverrideThresh = mOnThreshold*5.f;
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
//	mFilteredInput.sigMax(0.f);
	
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
		float kc, kex, key, kk;			
//		kc = 16./32.; kex = 4./32.; key = 2./32.; kk=1./32.;	

		kc = 16./48.; kex = 8./48.; key = 4./48.; kk=2./48.;	
		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
//		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		
//		kc = 4./18.; kex = 3./18.; key = 2./18.; kk=1./18.;	
//		mFilteredInput.convolve3x3xy(kc, kex, key, kk);

		// MLTEST
		mCalibratedSignal = mFilteredInput;

		if(mMaxTouchesPerFrame > 0)
		{
			mSpansHoriz = findSpans<kSensorRows, kSensorCols, 0>(mFilteredInput);
			mSpansHoriz = findZ2Spans<kSensorRows, kSensorCols, 0>(mSpansHoriz, mFilteredInput);			
			mPingsHoriz = findPings<kSensorRows, kSensorCols, 0>(mSpansHoriz, mFilteredInput);
			
			mSpansVert = findSpans<kSensorCols, kSensorRows, 1>(mFilteredInput); 
			mSpansVert = findZ2Spans<kSensorCols, kSensorRows, 1>(mSpansVert, mFilteredInput);			
			mPingsVert = findPings<kSensorCols, kSensorRows, 1>(mSpansVert, mFilteredInput);
			
			mPingsHoriz = filterPings<kSensorRows, kSensorCols, 0>(mPingsHoriz, mPingsHorizY1);
			mPingsHorizY1 = mPingsHoriz;
			
			mPingsVert = filterPings<kSensorCols, kSensorRows, 1>(mPingsVert, mPingsVertY1);
			mPingsVertY1 = mPingsVert;
			
			mIntersections = findIntersections(mPingsHoriz, mPingsVert);
			

			findTouches();
			
			matchTouches();
			
			// copy filtered spans to output array
			{
				std::lock_guard<std::mutex> lock(mSpansHorizOutMutex);
				mSpansHorizOut = mSpansHoriz;
			}
			
			{
				std::lock_guard<std::mutex> lock(mPingsHorizOutMutex);
				mPingsHorizOut = mPingsHoriz;
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
				std::lock_guard<std::mutex> lock(mIntersectionsOutMutex);
				mIntersectionsOut = mIntersections;
			}
		}

		filterAndOutputTouches();
		
		{
			std::lock_guard<std::mutex> lock(mTouchesOutMutex);
			mTouchesOut = mTouches;
		}

	}
#if DEBUG   

	if (mCount++ > 1000) 
	{
		mCount = 0;
		//dumpTouches();
		
	
		debug() << "\n INTERSC: \n";
		int row = 0;
		for(auto rowArray : mIntersectionsOut.data)
		{
			debug() << "row " << row++ << ": ";
			for(Vec4 s : rowArray)
			{
				if(!s)
				{
					break;	
				}
				debug() << "[" << s.x() << "-" << s.y() << "] ";
			}
			debug() << "\n";
		}

		
		 
	}   
#endif  
}

// find horizontal or vertical spans over which z is higher than a small constant.
template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::findSpans(const MLSignal& in)
{
//	const float zThresh = 0.00005; // must be greater than 0.
	const float zThresh = 0.001; // must be greater than 0.
	
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
		//		if(spanEnd > spanStart)
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
					if(spanEnd > spanStart)
					{
						appendSpanToRow(y.data[j], Vec4(spanStart, spanEnd, 0.f, 0.f));
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

template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::findPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inSpans, const MLSignal& in)
{
	// this is the maximum width of a span containing only one ping. depends on smoothing kernel.
	const float k1 = XY ? 3.5f : 4.0f; 
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
				appendSpanToRow(y.data[j], Vec4(pingPos, pingZ, 0.f, 0.f));
			}
			else
			{
				float pingPos = a + fingerWidth;
				float pingZ = XY ? in.getInterpolatedLinear(j, pingPos) : in.getInterpolatedLinear(pingPos, j);
				appendSpanToRow(y.data[j], Vec4(pingPos, pingZ, 0.f, 0.f));
				
				pingPos = b - fingerWidth;
				pingZ = XY ? in.getInterpolatedLinear(j, pingPos) : in.getInterpolatedLinear(pingPos, j);
				appendSpanToRow(y.data[j], Vec4(pingPos, pingZ, 0.f, 0.f));							
			}
		}
		j++;
	}
	return y;
}

template<size_t ARRAYS, size_t ARRAY_LENGTH, bool XY>
VectorArray2D<ARRAYS, ARRAY_LENGTH> TouchTracker::filterPings(const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPings, const VectorArray2D<ARRAYS, ARRAY_LENGTH>& inPingsY1)
{
	constexpr float kMatchDistance = 16.0f;
	VectorArray2D<ARRAYS, ARRAY_LENGTH> y;
	int j = 0;
	for(auto array : inPings.data)
	{
		y.data[j].fill(Vec4::null());
		
		for(Vec4 ping : array)
		{
			if(!ping) break;
			
			// get same row in previous pings
			auto arrayY1 = inPingsY1.data[j];
			
			Vec4 closest = Vec4::null();
			float minDist = MAXFLOAT;
			for(Vec4 pingY1 : arrayY1)
			{
				if(!pingY1) break;
				float dist = (ping - pingY1).magnitude();
				if(dist < minDist)
				{
					minDist = dist;
					closest = pingY1;
				}
			}
			
			/*
			auto compareClosest = [&](Vec4& a, Vec4& b){ float ad = (a - ping).magnitude(); float bd = (b - ping).magnitude(); return (ad < bd) ? a : b; };
			
			auto closest = std::accumulate(arrayY1.begin(), arrayY1.end(), Vec4::null, compareClosest);
			*/
			
			
			if(minDist < kMatchDistance)
			{
			//	debug() << "*";
				
				constexpr float k = 0.01f;
				
				float pos = ping.x()*k + closest.x()*(1.0f - k);
				float z = ping.y()*k + closest.y()*(1.0f - k);
				
				appendSpanToRow(y.data[j], Vec4(pos, z, 0.f, 0.f));							
			}
			else
			{
				if(XY)
				debug() << "!";
				// no match, unfiltered
				appendSpanToRow(y.data[j], ping);							
			}
		}
		j++;
	}
	return y;
}

TouchTracker::VectorsH TouchTracker::findIntersections(const TouchTracker::VectorsH& pingsHoriz, const TouchTracker::VectorsV& pingsVert)
{
	TouchTracker::VectorsH y;
	
	int j = 0;
	for(auto row : pingsHoriz.data)
	{
		int rowIntersections = 0;
		y.data[j].fill(Vec4::null());
		
		for(auto horizPing : row)
		{
			if(!horizPing) break;
			
			float hx = horizPing.x();
			int hxi = hx;
			int hyi = j;
			
			if(within(hxi, 0, kSensorCols))
			{
				// look for vertical pings nearby
				auto col = pingsVert.data[hxi];
				
				auto isCloseToHoriz = [&](Vec4 p){ int vyi = p.x(); return (vyi == hyi); };
				
				auto vertIt = std::find_if(col.begin(), col.end(), isCloseToHoriz ); 
				
				if(0)
				if(vertIt != col.end())
				{
					Vec4 vertPing = *vertIt;
					y.data[j][rowIntersections++] = Vec4(horizPing.x(), vertPing.x(), horizPing.y(), 0);
					vertIt++;
				}
				
				auto colB = pingsVert.data[hxi + 1];
				
			//	auto isCloseToHoriz = [&](Vec4 p){ int vyi = p.x(); return (vyi == hyi); };
				
				auto vertItB = std::find_if(colB.begin(), colB.end(), isCloseToHoriz ); 
				
				if(0)
				if(vertItB != colB.end())
				{
					Vec4 vertPing = *vertItB;
					y.data[j][rowIntersections++] = Vec4(horizPing.x(), vertPing.x(), horizPing.y(), 0);
					vertItB++;
				}
				
				if((vertIt != col.end()) && (vertItB != colB.end()))
				{
					Vec4 vertPing = *vertIt;
					Vec4 vertPingB = *vertItB;
					float yAvg = (vertPing.x() + vertPingB.x())/2.f;
					float zAvg = (vertPing.y() + vertPingB.y())/2.f;
					y.data[j][rowIntersections++] = Vec4(horizPing.x(), yAvg, zAvg, 0);
					//vertItB++;
				}
				
				
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
			
	return y;
}


/*
void TouchTracker::findTouches()
{
	// cluster intersections to get touches. 
	// light intersections cluster more easily. 
	// diagonals of dig. pairs need work
	
	mTouches.fill(Vec4()); 
	
	int currentGroup = 1;
	int n = mIntersections.size();
	
	// sort by z
	std::sort(mIntersections.begin(), mIntersections.end(), [](Vec4 va, Vec4 vb){return va.z() > vb.z();});
	
	for(int i=0; i<n; ++i)
	{
		Vec4 a = mIntersections[i];
		
		// TODO this creates a discontinuity! 
		
		
		
		// PROBLEM
		
		// get radius we can move this touch to combine with others		
		const float xDist = 1.5f;
		const float yDist = 1.5f;
		
		
		// TODO increase radius for light touches?
		float distScale = 1.0f;
		
		Vec4 c;
		float maxZ = 0.f;
		bool found = false;
		int foundIdx = -1;
		
		// get blob of maximum z within radius maxDist
		for(int j=0; j<n; ++j)
		{
			if(j != i)
			{
				Vec4 b = mIntersections[j];
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
			int groupOfB = mIntersections[foundIdx].w();
			if(!groupOfB)
			{
				mIntersections[foundIdx].setW(currentGroup++);
			}
			mIntersections[i].setW(mIntersections[foundIdx].w());
		}
		else
		{
			mIntersections[i].setW(currentGroup++);
		}
	}
	
	// collect groups into new touches
	int touches = 0;
	for(int g=1; g<currentGroup; ++g)
	{
		Vec4 centroid;
		for(int i=0; i<n; ++i)
		{
			if(mIntersections[i].w() == g)
			{
				Vec3 a = mIntersections[i];
				a.setX(a.x()*a.z());
				a.setY(a.y()*a.z());
				centroid += a;
			}
		}
		float tz = centroid.z();
		centroid /= tz;
		
		float zFromInput = mFFT2.getInterpolatedLinear(Vec2(centroid.x(), centroid.y()));
		
		centroid.setZ(zFromInput);
		centroid.setW(1); // age
		
		if(zFromInput > mOnThreshold)
		{
			mTouches[touches++] = centroid;
			if(touches >= mMaxTouchesPerFrame) break;
		}
	}
	// zero-terminate
	if(touches < kMaxTouches)
	{
		mTouches[touches] = Vec4();
	}
}
*/

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
	// filter touches
	// filter x and y for output
	// filter touches and write touch data to one frame of output signal.
	//
	MLSignal& out = *mpOut;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Vec4 t = mTouches[i];
		
		out(xColumn, i) = t.x();
		out(yColumn, i) = t.y();
		out(zColumn, i) = t.z() - mOnThreshold;
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

