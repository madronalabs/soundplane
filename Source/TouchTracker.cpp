void setRotate(bool b);


// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "TouchTracker.h"

#include <algorithm>

using Touch = TouchTracker::Touch;
using TouchArray = TouchTracker::TouchArray;

// return city block distance between two touches with a "z importance" parameter.
// if z scale is too small, zero touches will get matched with new active ones in the same position.
// if too big, z is more important than position and nothing works.	
inline float cityBlockDistanceXYZ(Touch a, Touch b, float zScale)
{
	return fabs(a.x - b.x) + fabs(a.y - b.y) + zScale*fabs(a.z - b.z);
}

TouchTracker::TouchTracker(int w, int h) :
	mWidth(w),
	mHeight(h),
	mMaxTouchesPerFrame(0),
	mSampleRate(1000.f),
	mLopassZ(50.),
	mRotate(false),
	mPairs(false),
	mCount(0)
{
	setThresh(0.1);
	mFilteredInput.setDims(w, h);
	mInput1.setDims(w, h);
		
	for(int i = 0; i < kMaxTouches; i++)
	{
		mTouchSortOrder[i] = i;
		mRotateShuffleOrder[i] = i;
	}
}
		
TouchTracker::~TouchTracker()
{
}

void TouchTracker::setMaxTouches(int t)
{
	int kmax = kMaxTouches;
	int newT = clamp(t, 0, kmax);
	if(newT != mMaxTouchesPerFrame)
	{
		mMaxTouchesPerFrame = newT;
		
		// reset shuffle order
		for(int i = 0; i < kMaxTouches; i++)
		{
			mTouchSortOrder[i] = i;
			mRotateShuffleOrder[i] = i;
		}
	}
}

void TouchTracker::setRotate(bool b)
{ 
	mRotate = b; 
	for(int i = 0; i < kMaxTouches; i++)
	{
		mRotateShuffleOrder[i] = i;
	}
}

void TouchTracker::clear()
{
	for (int i=0; i<kMaxTouches; i++)	
	{
		mTouches[i] = Touch();	
	}
}

// set the threshold of curvature that will cause a touch. Note that this will not correspond with the pressure (z) values reported by touches. 
void TouchTracker::setThresh(float f) 
{ 
	mOnThreshold = clamp(f*4.f, 0.005f, 1.f); 
	mFilterThreshold = mOnThreshold * 0.5f; 
	mOffThreshold = mOnThreshold * 0.75f; 
}

void TouchTracker::setLopassZ(float k)
{ 
	mLopassZ = k; 
}

MLSignal smoothPressureX(const MLSignal& in)
{
	int i, j;
	const float * pr2;
	float * prOut; 	
	
	// MLTEST
	// temp 
	int width = in.getWidth();
	int height = in.getHeight();
	MLSignal out(width, height);
	
	const float* pIn = in.getBuffer();
	float* pOut = out.getBuffer();
	
	int wb = in.getWidthBits();
	
	for(j = 0; j < height; j++)
	{
		// row ptrs
		pr2 = (pIn + (j << wb));
		prOut = (pOut + (j << wb));
		
		i = 0; // left side
		{
			prOut[i] = pr2[i] + pr2[i+1];	
		}
		
		for(i = 1; i < width - 1; i++) // center
		{
			prOut[i] = pr2[i-1] + pr2[i] + pr2[i+1];	
		}
		
		i = width - 1; // right side
		{
			prOut[i] = pr2[i-1] + pr2[i];	
		}
	}
	return out;
}

MLSignal smoothPressureY(const MLSignal& in)
{
	int i, j;
	const float * pr1, * pr2, * pr3; // input row ptrs
	float * prOut; 	
	
	
	// MLTEST
	// temp 
	int width = in.getWidth();
	int height = in.getHeight();
	MLSignal out(width, height);
	
	
	const float* pIn = in.getBuffer();
	float* pOut = out.getBuffer();
	
	int wb = in.getWidthBits();
	
	
	j = 0;	// top row
	{
		pr2 = (pIn + (j << wb));
		pr3 = (pIn + ((j + 1) << wb));
		prOut = (pOut + (j << wb) );
		
		for(i = 0; i < width; i++) 
		{
			prOut[i] = pr2[i] + pr3[i];
		}
	}
	for(j = 1; j < height - 1; j++) // center rows
	{
		pr1 = (pIn + ((j - 1) << wb));
		pr2 = (pIn + (j << wb));
		pr3 = (pIn + ((j + 1) << wb));
		prOut = (pOut + (j << wb));

		for(i = 0; i < width; i++) 
		{
			prOut[i] = pr1[i] + pr2[i] + pr3[i];		
		}
	}
	j = height - 1;	// bottom row
	{
		pr1 = (pIn + ((j - 1) << wb));
		pr2 = (pIn + (j << wb));
		prOut = (pOut + (j << wb));
		
		for(i = 0; i < width; i++) 
		{
			prOut[i] = pr1[i] + pr2[i];
		}
	}
	return out;
}

			
// --------------------------------------------------------------------------------

#pragma mark process

// 
// TODO take pointer to input / output arrays. 

// also pass max # of touches to output. 

void TouchTracker::process(MLSignal* pIn, int maxTouches, TouchArray* pOut)
{	
	if (!pIn || !pOut) return;
	setMaxTouches(maxTouches);
	
	// fixed IIR filter input
	float k = 0.25f;
	mFilteredInput = *pIn;
	mFilteredInput.scale(k);	
	mInput1.scale(1.0 - k);
	mFilteredInput.add(mInput1);
	mInput1 = mFilteredInput;

	// filter out any negative values. negative values can show up from capacitive coupling near edges,
	// from motion or bending of the whole instrument, 
	// from the elastic layer deforming and pushing up on the sensors near a touch. 
	mFilteredInput.sigMax(0.f);

	// a lot of filtering is needed here for Soundplane A to make sure peaks are in centers of touches.
	// it also reduces noise. 
	// the down side is, contiguous touches are harder to tell apart. a smart blob-shape algorithm
	// can make up for this later, with this filtering still intact.				
	mFilteredInput = smoothPressureX(mFilteredInput);
	mFilteredInput = smoothPressureX(mFilteredInput);
	mFilteredInput = smoothPressureX(mFilteredInput);
	mFilteredInput = smoothPressureX(mFilteredInput);
	mFilteredInput = smoothPressureY(mFilteredInput);
	mFilteredInput = smoothPressureY(mFilteredInput);
	mFilteredInput = smoothPressureY(mFilteredInput);
	mFilteredInput.scale(1.f/64.f);

	if(mMaxTouchesPerFrame > 0)
	{
		mTouches = findTouches(mFilteredInput);
					
		// sort touches by z. 
		std::sort(mTouches.begin(), mTouches.begin() + kMaxTouches, [](Touch a, Touch b){ return a.z > b.z; } );
		
		// match -> position filter -> fixed z filter -> feedback
		mTouches = matchTouches(mTouches, mTouchesMatch1);	
		mTouches = filterTouchesXYAdaptive(mTouches, mTouchesMatch1);
		mTouchesMatch1 = mTouches;
		
		// asymmetrical z filter from user setting. Ages are created here.
		mTouches = filterTouchesZ(mTouches, mTouches2, mLopassZ*2.f, mLopassZ*0.25f);
		mTouches2 = mTouches;
		
		// after variable filter, exile decayed touches so they are not matched. Note this affects match feedback!
		mTouchesMatch1 = exileUnusedTouches(mTouchesMatch1, mTouches);
								
		//	TODO hysteresis after matching to prevent glitching when there are more
		// physical touches than mMaxTouchesPerFrame and touches are stolen
//			mTouches = sortTouchesWithHysteresis(mTouches, mTouchSortOrder);			
//			mTouches = limitNumberOfTouches(mTouches);
		
		if(mRotate)
		{
			mTouches = rotateTouches(mTouches);
		}	
		
		mTouches = clampTouches(mTouches);
		

	}


	if (mCount++ > 1000) 
	{
		mCount = 0;			 
	}   
	
	*pOut = mTouches;
}

MLSignal TouchTracker::getCurvatureX(const MLSignal& in)
{
	const int w = kSensorCols;
	const int h = kSensorRows;
	
	// TODO no dynamic signals MLTEST
	MLSignal cx(w, h);
	
	// rows
	for(int j=0; j<h; ++j)
	{
		float z = 0.f;
		float zm1 = 0.f;
		float dz = 0.f;
		float dzm1 = 0.f;
		float ddz = 0.f;
		
		for(int i=0; i <= w; ++i)
		{
			if(within(i, 0, w))
			{
				z = in(i, j);
			}
			else
			{
				z = 0.f;
			}
			dz = z - zm1;
			ddz = dz - dzm1;					
			zm1 = z;
			dzm1 = dz;
			
			if(i >= 1)
			{
				cx(i - 1, j) = clamp(-ddz, 0.f, 1.f);
			}
		}
	}	
		
	return cx;
}

MLSignal TouchTracker::getCurvatureY(const MLSignal& in)
{
	const int w = kSensorCols;
	const int h = kSensorRows;
	
	// TODO no dynamic signals MLTEST
	MLSignal cy(w, h);
	
	// cols
	for(int i=0; i<w; ++i)
	{
		float z = 0.f;
		float zm1 = 0.f;
		float dz = 0.f;
		float dzm1 = 0.f;
		float ddz = 0.f;
		for(int j=0; j <= h; ++j)
		{
			if(within(j, 0, h))
			{
				z = in(i, j);
			}
			else
			{
				z = 0.f;
			}
			
			dz = z - zm1;
			ddz = dz - dzm1;					
			zm1 = z;
			dzm1 = dz;
			if(j >= 1)
			{
				cy(i, j - 1) = clamp(-ddz, 0.f, 1.f);
			}
		}
	}

	return cy;
}

MLSignal TouchTracker::getCurvatureXY(const MLSignal& in)
{
	MLSignal kx(in);
	MLSignal ky(in);
	MLSignal kxy(in);
	
	kx = getCurvatureX(kx);
	ky = getCurvatureY(ky);
	kxy = kx;
	kxy.multiply(ky);
	kxy.sqrt();
	return kxy;
}


Touch correctPeakX(Touch pos, const MLSignal& in) 
{		
	Touch newPos = pos;
	const float maxCorrect = 0.5f;
	const int w = kSensorCols;
	int x = pos.x;
	int y = pos.y;
	
	if(within(x, 1, w - 1))
	{
		float a = in(x - 1, y);
		float b = in(x, y);
		float c = in(x + 1, y);
		float p = ((a - c)/(a - 2.f*b + c))*0.5f;
		float fx = x + clamp(p, -maxCorrect, maxCorrect);										
		newPos = Touch(fx, pos.y, pos.z, 0.f, 0);
	}
	
	return newPos;
}

Touch correctPeakY(Touch pos, const MLSignal& in) 
{		
	const float maxCorrect = 0.5f;
	const int h = kSensorRows;
	int x = pos.x;
	int y = pos.y;
	
	float a, b, c;
	if(y <= 0)
	{
		a = 0;
		b = in(x, y);
		c = in(x, y + 1);
	}
	else if(y >= h - 1)
	{
		a = in(x, y - 1);
		b = in(x, y);
		c = 0;
	}
	else
	{
		a = in(x, y - 1);
		b = in(x, y);
		c = in(x, y + 1);
	}
	float p = ((a - c)/(a - 2.f*b + c))*0.5f;		
	float fy = y + clamp(p, -maxCorrect, maxCorrect);							
	return Touch(pos.x, fy, pos.z, 0.f, 0);
}

float sensorToKeyY(float sy)
{
	float ky = 0.f;
	
	// Soundplane A as measured.
	constexpr int mapSize = 6;
	
	// these y locations depend on the amount of smoothing done in smoothPressureX / smoothPressureY.
	constexpr std::array<float, mapSize> sensorMap{{0.7, 1.2, 2.7, 4.3, 5.8, 6.3}};
	constexpr std::array<float, mapSize> keyMap{{0.01, 1., 2., 3., 4., 4.99}};
	
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
		// TODO piecewise map MLRange type
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

Touch peakToTouch(Touch p)
{
	MLRange sensorToKeyX(3.5f, 59.5f, 1.f, 29.f);
	return Touch(sensorToKeyX(p.x), sensorToKeyY(p.y), p.z, 0.f, 0);
}

// quick touch finder based on peaks of curvature. 
// this works well, but a different approach based on blob sizes / shapes could do a much better
// job with contiguous keys.
TouchArray TouchTracker::findTouches(const MLSignal& kxy)
{
	constexpr int kMaxPeaks = kMaxTouches*2;
	const int w = kSensorCols;
	const int h = kSensorRows;	
	int i, j;
	
	// TODO preallocate
	std::array<Touch, kMaxTouches*2> peaks;
	TouchArray touches;
	std::array<std::bitset<kSensorCols>, kSensorRows> map;
					
	// get peaks
	float f11, f12, f13;
	float f21, f22, f23;
	float f31, f32, f33;
	
	j = 0;
	{
		std::bitset<kSensorCols>& row = map[j];
		for(i = 1; i < w - 1; ++i)
		{
			f21 = kxy(i - 1, j    ); f22 = kxy(i, j    ); f23 = kxy(i + 1, j    );
			f31 = kxy(i - 1, j + 1); f32 = kxy(i, j + 1); f33 = kxy(i + 1, j + 1);
			row[i] = (f22 > f21) && (f22 > mFilterThreshold) && (f22 > f23)
				&& (f22 > f31) && (f22 > f32) && (f22 > f33);
		}
	}
	
	for (j = 1; j < h - 1; ++j)
	{
		std::bitset<kSensorCols>& row = map[j];
		for(i = 1; i < w - 1; ++i)
		{
			f11 = kxy(i - 1, j - 1); f12 = kxy(i, j - 1); f13 = kxy(i + 1, j - 1);
			f21 = kxy(i - 1, j    ); f22 = kxy(i, j    ); f23 = kxy(i + 1, j    );
			f31 = kxy(i - 1, j + 1); f32 = kxy(i, j + 1); f33 = kxy(i + 1, j + 1);
			row[i] = (f22 > f11) && (f22 > f12) && (f22 > f13)
				&& (f22 > f21) && (f22 > mFilterThreshold) && (f22 > f23)
				&& (f22 > f31) && (f22 > f32) && (f22 > f33);
		}	
	}
	
	j = h - 1;
	{
		std::bitset<kSensorCols>& row = map[j];
		for(i = 1; i < w - 1; ++i)
		{
			f11 = kxy(i - 1, j - 1); f12 = kxy(i, j - 1); f13 = kxy(i + 1, j - 1);
			f21 = kxy(i - 1, j    ); f22 = kxy(i, j    ); f23 = kxy(i + 1, j    );
			row[i] = (f22 > f11) && (f22 > f12) && (f22 > f13)
				&& (f22 > f21) && (f22 > mFilterThreshold) && (f22 > f23);
		}
	}
	
	// gather all peaks.
	int nPeaks = 0;
	for (int j=0; j<h; j++)
	{
		std::bitset<kSensorCols>& mapRow = map[j];
		for (int i=0; i < mWidth; i++)
		{
			// if peak
			if (mapRow[i])
			{
				float z = kxy(i, j);
				peaks[nPeaks++] = Touch(i, j, z, 0.f, 0);
				if (nPeaks >= kMaxPeaks) break;
			}
		}
	}
	
	if(nPeaks > 1)
	{
		std::sort(peaks.begin(), peaks.begin() + nPeaks, [](Touch a, Touch b){ return a.z > b.z; } );
	}

	// correct and clip
	int nTouches = std::min(nPeaks, (int)kMaxTouches);
	for(int i=0; i<nTouches; ++i)
	{
		Touch p = peaks[i];
		if(p.z < mFilterThreshold) break;
				
		Touch px = correctPeakX(p, kxy);		
		Touch pxy = correctPeakY(px, kxy);		
		touches[i] = peakToTouch(pxy);	
	}
	
	return touches;
}


// sort the input touches in z order. A hysteresis offset for each array member prevents members from changing order too often.
// side effect: the new sorted order is written to the currentSortedOrder array.
//
// TODO: just storing the shuffle transform to sorted order, and not letting it change often, may be a clearer algorithm?
//
TouchArray TouchTracker::sortTouchesWithHysteresis(const TouchArray& in, std::array<int, TouchTracker::kMaxTouches>& previousSortedOrder)
{	
	const float kHysteresisOffset = 0.01f; // TODO adjust
	
	TouchArray preSort(in); // TEMP
	TouchArray postSort(in); // TEMP
	TouchArray touches(in);
	std::array<int, TouchTracker::kMaxTouches> newSortedOrder;
	
	// count input touches
	int n = 0;
	for(int i=0; i<kMaxTouches; ++i)
	{
		if(preSort[i].z == 0.f) break;
		n++;
	}
	
	// sort by x first to give stable initial order
	std::sort(preSort.begin(), preSort.begin() + kMaxTouches, [](Touch a, Touch b){ return a.x > b.x; } );
	
	postSort = preSort; // TEMP
	
	// add multiples of hysteresis offset to input data according to previous sorted order
	for(int i = 0; i < kMaxTouches; i++)
	{
		int v = kMaxTouches - i;
		postSort[previousSortedOrder[i]].z += (v*kHysteresisOffset);
		postSort[i].age = i; // stash index in age
	}
	
	std::sort(postSort.begin(), postSort.begin() + kMaxTouches, [](Touch a, Touch b){ return a.z > b.z; } );
	
	// get new sorted order
	for(int i = 0; i < kMaxTouches; i++)
	{
		newSortedOrder[i] = postSort[i].age;
	}
	
	// get touches in sorted order without hysteresis
	for(int i = 0; i < kMaxTouches; i++)
	{
		touches[i] = preSort[newSortedOrder[i]];
	}	
	
	// compare sorted orders
	bool orderChanged = false;
	for(int i=0; i<kMaxTouches; ++i)
	{
		if(previousSortedOrder[i] != newSortedOrder[i])
		{
			orderChanged = true;
			break;
		}
	}
	
	if(0)
		if(n > 1)
		{
			
			debug() << "\n    inputs: ";		
			for(int i=0; i<kMaxTouches; ++i)
			{			
				debug() << in[i].z << " ";
			}
			
			debug() << "\n    pre: ";		
			for(int i=0; i<kMaxTouches; ++i)
			{			
				debug() << preSort[i].z << " ";
			}
			
			debug() << "\n    post: ";		
			for(int i=0; i<kMaxTouches; ++i)
			{			
				debug() << postSort[i].z << " ";
			}
			
			
			debug() << "\n   prev: ";
			for(int i = 0; i < kMaxTouches; i++)
			{
				debug() << previousSortedOrder[i] << " ";
			}
			
			debug() << "\n   new: ";
			for(int i = 0; i < kMaxTouches; i++)
			{
				debug() << newSortedOrder[i] << " ";
			}
			
			debug() << "\n    outputs: ";
			
			for(int i=0; i<kMaxTouches; ++i)
			{			
				debug() << touches[i].z << " ";
			}
			
			debug() << "\n";		 
		}
	previousSortedOrder = newSortedOrder;
	
	if(mCount == 0)
	{
		debug() << "sort: ";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			//debug() << touches[i];
		}
		debug() << "\n";
	}
	
	return touches;
}

TouchArray TouchTracker::limitNumberOfTouches(const TouchArray& in)
{	
	TouchArray touches(in);
	
	// limit number of touches by overwriting with zeroes
	for(int i = mMaxTouchesPerFrame; i < kMaxTouches; i++)
	{
		touches[i] = Touch();
	}
	
	if(mCount == 0)
	{
		debug() << "limit: ";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			//debug() << touches[i];
		}
		debug() << "\n";
	}
	
	return touches;
}


// match incoming touches in x with previous frame of touches in x1.
// for each possible touch slot, output the touch x closest in location to the previous frame.
// if the incoming touch is a continuation of the previous one, set its age (w) to 1, otherwise to 0. 
// if there is no incoming touch to match with a previous one at index i, and no new touch needs index i, the position at index i will be maintained.
//

// TODO first touch below filter threshold(?) is on one index, then active touch switches index?! 

TouchArray TouchTracker::matchTouches(const TouchArray& x, const TouchArray& x1)
{
	const float kMaxConnectDist = 2.f; 
	
	TouchArray newTouches;
	newTouches.fill(Touch());
	
	std::array<int, kMaxTouches> forwardMatchIdx; 
	forwardMatchIdx.fill(-1);	
	
	std::array<int, kMaxTouches> reverseMatchIdx; 
	reverseMatchIdx.fill(-1);

	// for each previous touch, find minimum distance to a current touch.
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		float minDist = MAXFLOAT;
		Touch prev = x1[i];
		
		for(int j=0; j < mMaxTouchesPerFrame; ++j)
		{
			Touch curr = x[j];
			if((curr.z > mFilterThreshold) && (prev.z > mFilterThreshold))
			{			
				float distToCurrentTouch = cityBlockDistanceXYZ(prev, curr, 20.f);
				if(distToCurrentTouch < minDist)
				{
					forwardMatchIdx[i] = j;
					minDist = distToCurrentTouch;						
				}
			}
		}
	}

	// for each current touch, find minimum distance to a previous touch
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		float minDist = MAXFLOAT;
		Touch curr = x[i];
		for(int j=0; j < mMaxTouchesPerFrame; ++j)
		{
			Touch prev = x1[j];
			if((curr.z > mFilterThreshold) && (prev.z > mFilterThreshold))
			{			
				float distToPreviousTouch = cityBlockDistanceXYZ(prev, curr, 20.f);
				if(distToPreviousTouch < minDist)
				{
					reverseMatchIdx[i] = j;
					minDist = distToPreviousTouch;						
				}
			}
		}
	}

	// get mutual matches
	std::array<int, kMaxTouches> mutualMatches; 
	mutualMatches.fill(0);
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		int prevIdx = reverseMatchIdx[i];
		if(prevIdx >= 0)
		{
			if(forwardMatchIdx[prevIdx] == i) 	
			{
				mutualMatches[i] = true;
			}
		}
	}
	
	std::array<bool, kMaxTouches> currWrittenToNew; 
	currWrittenToNew.fill(false);
	
	// first, continue well-matched nonzero touches
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		if(mutualMatches[i])
		{
			int j = reverseMatchIdx[i];
			Touch curr = x[i];		
			Touch prev = x1[j];
	//		if((curr.z() > mFilterThreshold) && (prev.z() > mFilterThreshold))
			{			
				// touch is continued, mark as connected and write to new touches
				curr.age = (cityBlockDistanceXYZ(prev, curr, 0.f) < kMaxConnectDist);						
				newTouches[j] = curr;
				currWrittenToNew[i] = true;		
			}
		}
	}
		
	// now take care of any remaining nonzero current touches
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Touch curr = x[i];		
		if((!currWrittenToNew[i]) && (curr.z > mFilterThreshold))
		{
			int freeIdx = -1;
			float minDist = MAXFLOAT;
			
			// first, try to match same touch index (important for decay!)
			Touch prev = x1[i];
			if(prev.z <= mFilterThreshold)
			{
				freeIdx = i;
			}
			
			// then try closest free touch
			if(freeIdx < 0)
			{					
				for(int j=0; j<mMaxTouchesPerFrame; ++j)
				{
					Touch prev = x1[j];
					if(prev.z <= mFilterThreshold)
					{
						float d = cityBlockDistanceXYZ(curr, prev, 0.f);
						if(d < minDist)
						{
							minDist = d;
							freeIdx = j;
						}
					}
				}
			}
						
			// if a free index was found, write the current touch						
			if(freeIdx >= 0)
			{					
				Touch free = x1[freeIdx];
				curr.age = (cityBlockDistanceXYZ(free, curr, 0.f) < kMaxConnectDist);				
				newTouches[freeIdx] = curr;	
			}
		}
	}	
	
	// fill in any free touches with previous touches at those indices. This will allow old touches to re-link if not reused.
	for(int i=0; i < mMaxTouchesPerFrame; ++i)
	{
		Touch t = newTouches[i];
		if(t.z <= mFilterThreshold)
		{
			newTouches[i].x = (x1[i].x);
			newTouches[i].y = (x1[i].y);
		}
	}

	return newTouches;
}

// input: vec4<x, y, z, k> where k is 1 if the touch is connected to the previous touch at the same index.
//
TouchArray TouchTracker::filterTouchesXYAdaptive(const TouchArray& in, const TouchArray& inz1)
{
	float sr = 1000.f; // Soundplane A
	
	// these filter settings have a big and sort of delicate impact on play feel, so they are not user settable at this point
	const float kFixedXYFreqMax = 20.f;
	const float kFixedXYFreqMin = 1.f;
	MLRange zToXYFreq(0., 0.02, kFixedXYFreqMin, kFixedXYFreqMax); 
	
	TouchArray out;
	
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		float x = in[i].x;
		float y = in[i].y;
		float z = in[i].z;
		int age = in[i].age; 
		
		float x1 = inz1[i].x;
		float y1 = inz1[i].y;
		
		// filter, or not, based on w from matchTouches
		float newX, newY;
		if(age > 0) 
		{
			// get xy coeffs, adaptive based on z
			float freq = zToXYFreq.convertAndClip(z);			
			float omegaXY = freq*kMLTwoPi/sr;
			float kXY = expf(-omegaXY);
			float a0XY = 1.f - kXY;
			float b1XY = kXY;
			
			// onepole filters			
			newX = (x*a0XY) + (x1*b1XY);
			newY = (y*a0XY) + (y1*b1XY);		
		}
		else
		{
			newX = x;
			newY = y;
		}
		
		out[i] = Touch(newX, newY, z, 0.f, age);
	}
	
	return out;
}

TouchArray TouchTracker::filterTouchesZ(const TouchArray& in, const TouchArray& inz1, float upFreq, float downFreq)
{
	const float sr = 1000.f;
	const float omegaUp = upFreq*kMLTwoPi/sr;
	const float kUp = expf(-omegaUp);
	const float a0Up = 1.f - kUp;
	const float b1Up = kUp;
	const float omegaDown = downFreq*kMLTwoPi/sr;
	const float kDown = expf(-omegaDown);
	const float a0Down = 1.f - kDown;
	const float b1Down = kDown;
	
	TouchArray out;
	
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		float x = in[i].x;
		float y = in[i].y;
		float z = in[i].z;

		float z1 = inz1[i].z;
		int age1 = inz1[i].age;		

		float newZ, newAge;
		
		// filter z fixed for note-on velocity
		
		
		// filter z variable
		float dz = z - z1;
		if(dz > 0.f)
		{
			newZ = (z*a0Up) + (z1*b1Up);
		}
		else
		{
			newZ = (z*a0Down) + (z1*b1Down);				
		}	
		
		// gate with hysteresis
		bool gate = (age1 > 0);
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
			newAge = 0;
		}
		else
		{
			newAge = age1 + 1;
		}
		
		out[i] = Touch(x, y, newZ, 0.f, newAge);
	}
	
	return out;
}

// if a touch has decayed below the filter threshold after z filtering, move it off the scene so it won't match to other nearby touches.
TouchArray TouchTracker::exileUnusedTouches(const TouchArray& preFiltered, const TouchArray& postFiltered)
{
	TouchArray out(preFiltered);
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Touch a = preFiltered[i];
		Touch b = postFiltered[i];
		
		if(b.x > 0.f)
		{
			if(b.z <= mFilterThreshold) 
			{
				a.x = (-1.f);
				a.y = (-10.f);
				a.z = (0.f);
			}
		}
		
		out[i] = a;					
	}
	return out;
}

// rotate order of touches, changing order every time there is a new touch in a frame.
// side effect: writes to mRotateShuffleOrder
TouchArray TouchTracker::rotateTouches(const TouchArray& in)
{	
	TouchArray touches(in);
	if(mMaxTouchesPerFrame > 1)
	{		
		bool doRotate = false;
		for(int i = 0; i < mMaxTouchesPerFrame; ++i)
		{
			if(in[i].age == 1)
			{
				// we have a new touch at index i.
				doRotate = true;
				break;
			}
		}
		
		if(doRotate)
		{
			// rotate the indices of all free or new touches in the shuffle order		
			int nFree = 0;
			std::array<int, kMaxTouches> freeIndexes;
			
			for(int i=0; i<mMaxTouchesPerFrame; ++i)
			{
				if((in[i].z < mFilterThreshold) || (in[i].age == 1))
				{
					freeIndexes[nFree++] = i;
				}
			}
					
			if(nFree > 1)
			{
				int first = mRotateShuffleOrder[freeIndexes[0]];
				for(int i=0; i < nFree - 1; ++i)
				{				
					mRotateShuffleOrder[freeIndexes[i]] = mRotateShuffleOrder[freeIndexes[i + 1]];
				}
				mRotateShuffleOrder[freeIndexes[nFree - 1]] = first;
			}
		}

		// shuffle
		for(int i = 0; i < mMaxTouchesPerFrame; ++i)
		{
			touches[mRotateShuffleOrder[i]] = in[i];
		}
	}
	return touches;
}

// clamp touches and remove hysteresis threshold.
TouchArray TouchTracker::clampTouches(const TouchArray& in)
{
	const float kTouchOutputScale = 1.f;// 16.f;
	TouchArray out;
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Touch t = in[i];
		
		if(t.x != t.x)
		{
			debug() << i << "x!";
			t.x = (0.f);
		}
		if(t.y != t.y)
		{
			debug() << i << "y!";
			t.y = (0.f);
		}
				
		out[i] = t;			
		float newZ = (clamp((t.z - mOnThreshold)*kTouchOutputScale, 0.f, 8.f));
		if(t.age == 0)
		{
			newZ = 0.f;
		}
		out[i].z = (newZ);
	}
	return out;
}
