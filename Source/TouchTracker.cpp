
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "TouchTracker.h"

std::ostream& operator<< (std::ostream& out, const Touch & t)
{
	out << std::setprecision(4);
	out << "[" << t.x << ", " << t.y << ", " << t.zf << " (" << t.age << ")]";
	return out;
}

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

TouchTracker::TouchTracker(int w, int h) :
	mWidth(w),
	mHeight(h),
	mpIn(0),
	mpInputMap(0),
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
	mBackgroundFilter(1, 1),
	mNeedsClear(true),
	mKeyboardType(rectangularA),
	mCalibrator(w, h),
	mSampleRate(1000.f),
	mBackgroundFilterFreq(0.125f),
	mPrevTouchForRotate(0),
	mRotate(false),
	mDoNormalize(true),
	mUseTestSignal(false),
	mTemplateSamples(0)
{
	mTouches.resize(kTrackerMaxTouches);	
	mTouchesToSort.resize(kTrackerMaxTouches);	

	// TODO checks
	if(mpInputMap) 
	{
		delete[] mpInputMap;
		mpInputMap = 0;
	}
	mpInputMap = new e_pixdata[w*h];
	
	mTestSignal.setDims(w, h);
	mTestSignal2.setDims(w, h);
	mCalibratedSignal.setDims(w, h);
	mCookedSignal.setDims(w, h);
	mBackground.setDims(w, h);
	mBackgroundFilterFrequency.setDims(w, h);
	mBackgroundFilterFrequency2.setDims(w, h);
	mFilteredInput.setDims(w, h);
	mSumOfTouches.setDims(w, h);
	mInhibitMask.setDims(w, h);
	mTemplateMask.setDims(w, h);
	mInputMinusBackground.setDims(w, h);
	mCalibrationProgressSignal.setDims(w, h);
	mResidual.setDims(w, h);
	mFilteredResidual.setDims(w, h);
	mTemp.setDims(w, h);
	mTempWithBorder.setDims(w + 2, h + 2);
	mRetrigTimer.setDims(w, h);
	mDzSignal.setDims(w, h);
	mBackgroundFilter.setDims(w, h);
	mBackgroundFilter.setSampleRate(mSampleRate);
	mTemplateScaled.setDims (kTemplateSize, kTemplateSize);
	
	// new
	mRegions.setDims(w, h); 
	mRowPeaks.setDims(w, h); 

	//mTemplateSpanSum.setDims(kTemplateSpanWidth);
	
//	mTemplateSpan = MLSignal {0.17, 0.52, 0.86, 1.00, 0.86, 0.52, 0.17};// 12
	mTemplateSpan = MLSignal {0.04, 0.12, 0.69, 1.00, 0.69, 0.12, 0.04}; // 16 frequencies
	
	mTemplateSpan.dump(std::cout, true);
	
	// allocate max number of possible peaks
	//mSpans.resize(w*h);
	
	// NEW
	int wb = bitsToContain(w);
	int hb = bitsToContain(h);
	
	// TODO complex signals as 2 z frames?
	mFFT1.setDims(1 << wb, 1 << hb);
	mFFT1i.setDims(1 << wb, 1 << hb);
	
	mFFT2.setDims(1 << wb, 1 << hb);
	mTouchFrequencyMask.setDims(1 << wb, 1 << hb);
	
	mTouchKernel.setDims(w, h);
	mTouchKerneli.setDims(w, h);
	
	makeFrequencyMask();
	
	// set sum dims to match template and clear
	mTemplateSpanSum = mTemplateSpan;
	mTemplateSpanSum.clear();

}
		
TouchTracker::~TouchTracker()
{
	if(mpInputMap) delete[] mpInputMap;
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
		case(rectangularA):
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
		case(rectangularA):
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

void TouchTracker::clear()
{
	for (int i=0; i<mMaxTouchesPerFrame; i++)	
	{
		mTouches[i] = Touch();	
	}
	mBackgroundFilter.clear();
	mNeedsClear = true;
}
	
void TouchTracker::setThresh(float f) 
{ 
	const float kHysteresis = 0.002f;
	mOffThreshold = f; 
	mOnThreshold = f + kHysteresis; 
	mOverrideThresh = mOnThreshold*5.f;
	mCalibrator.setThreshold(mOnThreshold);
}

void TouchTracker::setLopass(float k)
{ 
	mLopass = k; 
}
			
// --------------------------------------------------------------------------------
#pragma mark main processing

Vec2 correctTouchPosition(const MLSignal& in, int ix, int iy)
{
	int width = in.getWidth();
	int height = in.getHeight();
	
	int x = clamp(ix, 1, width - 2);
	int y = clamp(iy, 1, height - 2);
	
	// over most of surface, return correctPeak()
	if(within(y, 2, height - 2))
	{
		return in.correctPeak(ix, iy, 0.75);
	}
	
	// else use tweaked correctPeak() for top, bottom rows (Soundplane A)
	Vec2 pos(x, y);
	float dx = (in(x + 1, y) - in(x - 1, y)) / 2.f;
	float dy = (in(x, y + 1) - in(x, y - 1)) / 2.f;
	float dxx = (in(x + 1, y) + in(x - 1, y) - 2*in(x, y));
	float dyy = (in(x, y + 1) + in(x, y - 1) - 2*in(x, y));
	float dxy = (in(x + 1, y + 1) + in(x - 1, y - 1) - in(x + 1, y - 1) - in(x - 1, y + 1)) / 4.f;
	if((dxx != 0.f)&&(dxx != 0.f)&&(dxx != 0.f))
	{
		float oneOverDiscriminant = 1.f/(dxx*dyy - dxy*dxy);
		float fx = (dyy*dx - dxy*dy) * oneOverDiscriminant;
		float fy = (dxx*dy - dxy*dx) * oneOverDiscriminant;
		
		fx = clamp(fx, -0.5f, 0.5f);
		fy = clamp(fy, -0.5f, 0.5f);
		
		if(y == 1)
		{
			if(fy > 0.)
				fy *= 2.f;
		}
		else if(y == height - 2)
		{
			if(fy < 0.)
				fy *= 2.f;
		}
		
		pos -= Vec2(fx, fy);
	}
	return pos;
}

Vec3 TouchTracker::closestTouch(Vec2 pos)
{
	float minDist = MAXFLOAT;
	int minIdx = 0;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{
			Vec2 p(t.x, t.y);
			p -= pos;
			float d = p.magnitude();
			if(d < minDist)
			{
				minDist = d;
				minIdx = i;
			}
		}
	}
	Touch& t = mTouches[minIdx];
	return(Vec3(t.x, t.y, t.z));
}

const float kInhibitRange = 8;
// currently, touches inhibit other new touches near them, below
// a conical slope away from the touch position. 
// return the greatest inhibit threshold of any other touch at the given position.		
float TouchTracker::getInhibitThreshold(Vec2 a)
{
	float maxInhibit = 0.f;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		const Touch& u = mTouches[i];
		if (u.isActive())
		{
			Vec2 b(u.x1, u.y1);		// use previous position		
			float dab = (a - b).magnitude();			
			// don't check against touch at input position (same touch)
			if(dab > 0.1f)
			{
				if(dab < kInhibitRange)
				{					
					float dru = 1.f - (dab / kInhibitRange);	
					dru = clamp(dru, 0.f, 1.f);
					float inhibitZ = u.z1*0.9*dru;
					maxInhibit = max(maxInhibit, inhibitZ);
				}
			}
		}
	}
	return maxInhibit;
}

void TouchTracker::makeFrequencyMask()
{
	int h = mTouchFrequencyMask.getHeight();
	int w = mTouchFrequencyMask.getWidth();
	float c = 16;
	float sum = 0.f;
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			// distance from 0 with wrap
			float px = min((float)i, (float)(w - i));
			float f;			
			if(px == 0)
			{
				// get rid of DC, which sharpens edges
				f = 0.;
			}
			else if(px < c)
			{
				// keep other frequencies below cutoff
				f = 1.f;
			}
			else
			{
				// get rid of high frequencies
				f = 0.;
			}
			
			mTouchFrequencyMask(i, j) = f;
			sum += f;
		}
	}
	
	// normalize
	sum /= h;
	mTouchFrequencyMask.scale((float)w/sum);
	mTouchFrequencyMask.dump(std::cout, true);
}

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
		mBackgroundFilter.clear();
		mNeedsClear = false;
		return;
	}
		
	// filter out any negative values. negative values can shows up from capacitive coupling near edges,
	// from motion or bending of the whole instrument, 
	// from the elastic layer deforming and pushing up on the sensors near a touch. 
	mFilteredInput.sigMax(0.);		
	
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
		if(mDoNormalize)
		{
			mCalibrator.normalizeInput(mFilteredInput);
		}
			
		
		// forward FFT -> FFT1
		mFFT1 = mFilteredInput;	
		mFFT1i.clear();
		FFTEachRow(mFFT1, mFFT1i);
		
		// make touch kernel duplicated on each row
		mTouchKernel.clear();
		mTouchKerneli.clear();
		for(int j=0; j<h; ++j)
		{
			// put template span in kernel row centered at 0 = DC
			int kw = mTemplateSpan.getWidth();
			int xOffset = kw/2;
			for(int i = 0; i < kw; ++i)
			{
				mTouchKernel((i - xOffset)%w, j) = mTemplateSpan[i];
			}		
		}
		float kernelRowSum = mTouchKernel.getSum() / h;
		mTouchKernel.scale(2.f * w / kernelRowSum);
		// OK
		
		// convert touch kernel to freq. domain
		FFTEachRow(mTouchKernel, mTouchKerneli);
		// zero complex components
		mTouchKerneli.clear();
		// TODO store
		
		// deconvolve, dividing by touch kernel in the freq. domain
		// question: no imaginary component in freq domain at this point?
		
		// remove high freqs. and DC in the freq. domain
		mFFT1.multiply(mTouchFrequencyMask);
		mFFT1i.multiply(mTouchFrequencyMask);

		// sharpen
		// temp? baaaad syntax
		// could be MLSignal::complexDivide();
		// divide in frequency domain by touch kernel and put results in (mFFT1, mFFT1i)
		FFTEachRowDivide(mFFT1, mFFT1i, mTouchKernel, mTouchKerneli);
		
		// back to spatial domain
		FFTEachRowInverse(mFFT1, mFFT1i);

		mFFT2 = mFFT1;
				
		mTestSignal = mFilteredInput;
		mTestSignal.scale(50.);
		
		mTestSignal2 = mFFT2;
		mTestSignal2.scale(50.);

		// MLTEST
		mFilteredInput = mFFT2;
		mCalibratedSignal = mFFT2;
		
		if(mMaxTouchesPerFrame > 0)
		{
			findSpans();
			findPings();		
		}

		filterAndOutputTouches();
	}
	
#if DEBUG	
	// TEMP	
	if (mCount++ > 1000) 
	{
		mCount = 0;
		//dumpTouches();
		
		/*
		debug() << "\n SPANS: \n";
		for(auto it = mSpans.begin(); it != mSpans.end(); ++it)
		{
			Vec3 s = *it;
			debug() << s.y() - s.x() << " ";
		}
		 */
	}
#endif	
}

inline Vec2 positionOfGreaterValue(const MLSignal& f, Vec2 p, Vec2 q)
{
	if(f(q) > f(p)) return(q);
	return p;
}

Vec2 maxPositionIn4Hood(const MLSignal& f, Vec2 p)
{
	Vec2 ux(1, 0);
	Vec2 uy(0, 1);
	Vec2 maxPos;
	maxPos = positionOfGreaterValue(f, p, p + ux);
	maxPos = positionOfGreaterValue(f, maxPos, p - ux);
	maxPos = positionOfGreaterValue(f, maxPos, p + uy);
	maxPos = positionOfGreaterValue(f, maxPos, p - uy);
	return maxPos;
}

// find connected regions in data and assign each an index.
void TouchTracker::getRegions()
{
	float zMin = mOnThreshold;
	const MLSignal& in = mFFT2;
	int w = in.getWidth();
	int h = in.getHeight();
	
	// copy input to temp signal with zero border
	MLSignal f(w + 2, h + 2);
	f.clear();
	f.add2D(in, 1, 1);
	
	// make temp regions signal with border
	MLSignal r(w + 2, h + 2);
	r.clear();
	
	// find peaks and sort them by z. This step may be optional for touch tracker.
	// it makes the visualization easier to look at. Without it the
	// indices will be more or less random each frame.	
	std::vector<Vec3> peaks;
	for(int j=1; j<h + 1; ++j)
	{
		for(int i=1; i < w + 1; ++i)
		{
			Vec2 p(i, j);
			if(f(p) > zMin)
			{			
				if(p == maxPositionIn4Hood(f, p))
				{
					peaks.push_back(Vec3(p.x(), p.y(), f(p)));
				}
			}
		}
	}
	std::sort(peaks.begin(), peaks.end(), [](Vec3& a, Vec3& b){ return(a.z() > b.z()); });

	// mark peaks in region signal.
	int tempIdx = 1;
	for(auto it = peaks.begin(); it != peaks.end(); it++)
	{
		Vec3 p = *it;
		r(Vec2(p.x(), p.y())) = tempIdx++;
	}
	
//	int regionIdx = 1;
	for(int j=1; j<h + 1; ++j)
	{
		for(int i=1; i < w + 1; ++i)
		{
			// while signal at (i, j) is not a peak and is not marked, 
			// move to highest value in 4-neighborhood and keep looking
			Vec2 p(i, j);
			if(f(p) > zMin)
			{
				while(r(p) == 0)
				{				
					Vec2 pMax = maxPositionIn4Hood(f, p);
					if(p == pMax)
					{
						// mark the peak
				//		r(p) = regionIdx++;
					}
					else if(r(pMax) != 0) // already part of a region?
					{
						r(p) = r(pMax);
					}
					else
					{
						p = pMax;
					}
				}
			}
		}
	}
	
	// copy to output, removing border
	MLSignal& out = mRegions;
	out.clear();
	out.add2D(r, -1, -1);
}

// find spans where pressure on a given row exceeds kSpanThreshold at the start and end, 
// end exceeds mOnThreshold at some point during the span.
void TouchTracker::findSpans()
{
	// constant span start / end pressure, chosen so that span lengths will stay as constant as possible. 
	const float t = kSpanThreshold;
	
	// some point on the span must be over this threshold to be recognized.
	const float zThresh = mOnThreshold;
	
	const MLSignal& in = mFFT2;
	int w = in.getWidth();
	int h = in.getHeight();

	const juce::ScopedLock lock(mSpansLock);
	mSpans.clear();
 
	for(int j=0; j<h; ++j)
	{
		bool spanActive = false;
		bool spanExceedsThreshold = false;
		float spanStart, spanEnd;
		float z1 = 0.f;
		float z2 = 0.f;
		float i1, i2;
		float a, za, zb;
		for(int i=0; i < w; ++i)
		{
			i1 = i - 1.f;
			i2 = i;
			z1 = z2;
			z2 = in(i, j);
			if((z1 <= t) && (z2 > t))
			{
				// get intersection and start a span
				za = t - z1;
				zb = z2 - t;
				a = za/(za + zb);
				spanStart = i - 1. + a;
				spanActive = true;
				if(z2 > zThresh) spanExceedsThreshold = true;
			}
			else if((z1 > t) && (z2 <= t))
			{
				// get intersection and end the span				
				za = z1 - t;
				zb = t - z2;
				a = za/(za + zb);
				spanEnd = i - 1. + a;
				
				if(spanExceedsThreshold)
				{
					mSpans.push_back(Vec3(spanStart, spanEnd, j)); 
				}
				spanActive = false;
				spanExceedsThreshold = false;
			}
			else if(spanActive)
			{
				if(z2 > zThresh) spanExceedsThreshold = true;
			}
		}
		// end row
		if(spanActive && spanExceedsThreshold)
		{
			spanEnd = w - 1;
			mSpans.push_back(Vec3(spanStart, spanEnd, j)); 
		}
	}
}


void TouchTracker::collectTemplate()
{	
	const MLSignal& in = mFFT2;
	int w = in.getWidth();
	int h = in.getHeight();
	int kw = mTemplateSpan.getWidth();
	
	float spanBuf[w];
	const juce::ScopedLock lock(mSpansLock);
	
	for(auto it = mSpans.begin(); it != mSpans.end(); ++it)
	{
		Vec3 s = *mSpans.begin();
		float a = s.x();
		float b = s.y();
		float length = b - a; 
		int row = s.z();
		float m = (a + b)/2.;
		
		int offset = (float)(kw - 1)/2.f;
		for(int i = 0; i < kw; ++i)
		{
			float x = m - offset + i;
			mTemplateSpan[i] = in.getInterpolatedLinear(x, row);
		}		
		
		float zMax = mTemplateSpan[kw/2];
		mTemplateSpan.scale(1.f/zMax);
		
		mTemplateSpanSum.add(mTemplateSpan);	
		mTemplateSamples++;
	}
	
	if(mCount == 0)
	{
		debug() << "\nSum: " ;
		mTemplateSpanSum.dump(std::cout, true);
		debug() << "\n Samples: " << mTemplateSamples << "\n";
		MLSignal d(kw);
		d = mTemplateSpanSum;
		d.scale(1.f/(float)mTemplateSamples);
		debug() << "\nTemplate: " ;
		d.dump(std::cout, true);
		debug() << "\n\n";
		
	}
}

// from spans, get pings. A ping is a spot where it's likely a touch lies directly over a sensor row.
void TouchTracker::findPings()
{	
	const MLSignal& in = mFFT2;
	int w = in.getWidth();
	int h = in.getHeight();

	const juce::ScopedLock lock(mPingsLock);
	mPings.clear();
	
	MLRange zRange(mOnThreshold, mMaxForce, 0., 1.);


	for(auto it = mSpans.begin(); it != mSpans.end(); ++it)
	{
		Vec3 s = *it;
		float a = s.x();
		float b = s.y();
		int row = s.z();
		
		// from ends of span inward, find best fits for touches and remove them.
		
		
		// TEST: best integer guess and distribute
		// NO - creates glitches
		float length = b - a; // what to do about surface ends?
		const float minSpan = 6.f;
		const float sensorsPerKey = 2.f;
		float fn;
		fn = 1.0f + ((length - minSpan) / sensorsPerKey);
		fn = max(fn, 1.f);
		fn = roundf(fn);
		int n = fn;
		float ends = (length - fn*sensorsPerKey) / 2.f;
		for(int k = 0; k < n; ++k)
		{
			float pingX = a + ends + (k + 0.5f)*sensorsPerKey;
			float z = in.getInterpolatedLinear(pingX, row);
			mPings.push_back(Vec3(pingX, row, zRange(z)));
		}
	}
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
		Touch& t = mTouches[i];			
		if(t.age > 1)
		{
			float xyc = 1.0f - powf(2.71828f, -kMLTwoPi * mLopass*0.1f / (float)mSampleRate);
			t.xf += (t.x - t.xf)*xyc;
			t.yf += (t.y - t.yf)*xyc;
		}
		else if(t.age == 1)
		{
			t.xf = t.x;
			t.yf = t.y;
		}
		out(xColumn, i) = t.xf;
		out(yColumn, i) = t.yf;
		out(zColumn, i) = (t.age > 0) ? t.zf : 0.;			
		out(dzColumn, i) = t.dz;
		out(ageColumn, i) = t.age;
		out(dtColumn, i) = t.tDist;
	}
}

void TouchTracker::setDefaultNormalizeMap()
{
	mCalibrator.setDefaultNormalizeMap();
	mpListener->hasNewCalibration(mNullSig, mNullSig, -1.f);
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


