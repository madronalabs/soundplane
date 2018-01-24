// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2017 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include <iostream>
#include <cmath>
#include <list>
#include <thread>
#include <mutex>
#include <array>
#include <bitset>
#include <algorithm>

#include "TouchTracker.h"

constexpr float kTwoPi = 3.1415926535f*2.f;

template <class c>
inline c (clamp)(const c& x, const c& min, const c& max)
{
	return (x < min) ? min : (x > max ? max : x);
}

// within range, including start, excluding end value.
template <class c>
inline bool (within)(const c& x, const c& min, const c& max)
{
	return ((x >= min) && (x < max));
}

inline float lerp(const float a, const float b, const float m)
{
	return(a + m*(b-a));
}

inline float unlerp(const float a, const float b, const float x)
{
	return(x - a)/(b-a);
}

inline float mapRange(const float a, const float b, const float c, const float d, const float x)
{
	return lerp(c, d, unlerp(a, b, x));
}

inline float mapAndClipRange(const float a, const float b, const float c, const float d, const float x)
{
	return lerp(c, d, clamp(unlerp(a, b, x), 0.f, 1.f));
}

// return city block distance between two touches with a "z importance" parameter.
// if z scale is too small, zero touches will get matched with new active ones in the same position.
// if too big, z is more important than position and nothing works.	
inline float cityBlockDistanceXYZ(Touch a, Touch b, float zScale)
{
	return fabs(a.x - b.x) + fabs(a.y - b.y) + zScale*fabs(a.z - b.z);
}

// TouchTracker

TouchTracker::TouchTracker() :
	mSampleRate(1000.f),
	mMaxTouchesPerFrame(0),
	mLopassZ(50.),
	mRotate(false)
{
	setThresh(0.1);
	
	for(int i = 0; i < kMaxTouches; i++)
	{
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
	mOnThreshold = clamp(f, 0.005f, 1.f); 
	mFilterThreshold = mOnThreshold * 0.5f; 
	mOffThreshold = mOnThreshold * 0.75f; 
}

void TouchTracker::setLopassZ(float k)
{ 
	mLopassZ = k; 
}

SensorFrame smoothPressureX(const SensorFrame& in)
{
	int i, j;
	const float * pr2;
	float * prOut; 		

    SensorFrame out{};
	
	for(j = 0; j < SensorGeometry::height; j++)
	{
		// row ptrs
		pr2 = (in.data() + (j*SensorGeometry::width));
		prOut = (out.data() + (j*SensorGeometry::width));
		
		i = 0; // left side
		{
			prOut[i] = (pr2[i] + pr2[i+1]);	
		}
		
		for(i = 1; i < SensorGeometry::width - 1; i++) // center
		{
			prOut[i] = (pr2[i-1] + pr2[i] + pr2[i+1]);	
		}
		
		i = SensorGeometry::width - 1; // right side
		{
			prOut[i] = (pr2[i-1] + pr2[i]);	
		}
	}
	return out;
}

SensorFrame smoothPressureY(const SensorFrame& in)
{
	int i, j;
	const float * pr1, * pr2, * pr3; // input row ptrs
	float * prOut; 	
	
    SensorFrame out{};
	
	j = 0;	// top row
	{
		pr2 = (in.data() + j*SensorGeometry::width);
		pr3 = (in.data() + (j + 1)*SensorGeometry::width);
		prOut = (out.data() + j*SensorGeometry::width);
		
		for(i = 0; i < SensorGeometry::width; i++) 
		{
			(prOut[i] = pr2[i] + pr3[i]);
		}
	}
	for(j = 1; j < SensorGeometry::height - 1; j++) // center rows
	{
		pr1 = (in.data() + (j - 1)*SensorGeometry::width);
		pr2 = (in.data() + j*SensorGeometry::width);
		pr3 = (in.data() + (j + 1)*SensorGeometry::width);
		prOut = (out.data() + j*SensorGeometry::width);

		for(i = 0; i < SensorGeometry::width; i++) 
		{
			(prOut[i] = pr1[i] + pr2[i] + pr3[i]);		
		}
	}
	j = SensorGeometry::height - 1;	// bottom row
	{
		pr1 = (in.data() + (j - 1)*SensorGeometry::width);
		pr2 = (in.data() + j*SensorGeometry::width);
		prOut = (out.data() + j*SensorGeometry::width);
		
		for(i = 0; i < SensorGeometry::width; i++) 
		{
			(prOut[i] = pr1[i] + pr2[i]);
		}
	}
	return out;
}


SensorFrame TouchTracker::preprocess(const SensorFrame& in)
{
    SensorFrame y;
    
    // fixed IIR filter input
    float k = 0.25f;
    y = multiply(in, k);
    mInputZ1 = multiply(mInputZ1, 1.0 - k);
    y = add(y, mInputZ1);
    mInputZ1 = y;
    
    // filter out any negative values. negative values can show up from capacitive coupling near edges,
    // from motion or bending of the whole instrument,
    // from the elastic layer deforming and pushing up on the sensors near a touch.
    y = max(y, 0.f);
    
    // a lot of filtering is needed here for Soundplane A to make sure peaks are in centers of touches.
    // it also reduces noise.
    // the down side is, contiguous touches are harder to tell apart. a smart blob-shape algorithm
    // can make up for this later, with this filtering still intact.
    y = smoothPressureX(smoothPressureX(smoothPressureX(smoothPressureX(y))));
    y = smoothPressureY(smoothPressureY(smoothPressureY(y)));
    y = getCurvatureXY(multiply(y, 1.f/64.f));
    
    return y;
}

TouchArray TouchTracker::process(const SensorFrame& in, int maxTouches)
{
	setMaxTouches(maxTouches);

    mTouches = TouchArray{};
    
	if(mMaxTouchesPerFrame > 0)
	{
		mTouches = findTouches(in);
					
		// match -> position filter -> feedback
		mTouches = matchTouches(mTouches, mTouchesMatch1);	
		mTouches = filterTouchesXYAdaptive(mTouches, mTouchesMatch1);
		mTouchesMatch1 = mTouches;
		
		// asymmetrical z filter from user setting. Ages are created here.
		mTouches = filterTouchesZ(mTouches, mTouches2, mLopassZ*2.f, mLopassZ*0.25f);
		mTouches2 = mTouches;
		
		// after variable filter, exile decayed touches so they are not matched. Note this affects match feedback!
		mTouchesMatch1 = exileUnusedTouches(mTouchesMatch1, mTouches);
								
		// TODO hysteresis after matching to prevent glitching when there are more
		// physical touches than mMaxTouchesPerFrame and touches are stolen
		
		if(mRotate)
		{
			mTouches = rotateTouches(mTouches);
		}	
		
		mTouches = clampAndScaleTouches(mTouches);
	}
    
    return mTouches;
}

Touch correctPeakX(Touch pos, const SensorFrame& in) 
{		
	Touch newPos = pos;
	const float maxCorrect = 0.5f;
	const int w = SensorGeometry::width;
	int x = pos.x;
	int y = pos.y;
	
	if(within(x, 1, w - 1))
	{
		float a = in[y*w + x - 1];
		float b = in[y*w + x];
		float c = in[y*w + x + 1];
		float p = ((a - c)/(a - 2.f*b + c))*0.5f;
		float fx = x + clamp(p, -maxCorrect, maxCorrect);										
        newPos = Touch{.x = fx, .y = pos.y, .z = pos.z};
	}
	
	return newPos;
}

Touch correctPeakY(Touch pos, const SensorFrame& in) 
{		
	const float maxCorrect = 0.5f;
	const int w = SensorGeometry::width;
	const int h = SensorGeometry::height;
	int x = pos.x;
	int y = pos.y;
	
	float a, b, c;
	if(y <= 0)
	{
		a = 0;
		b = in[y*w + x];
		c = in[(y + 1)*w + x];
	}
	else if(y >= h - 1)
	{
		a = in[(y - 1)*w + x];
		b = in[y*w + x];
		c = 0;
	}
	else
	{
		a = in[(y - 1)*w + x];
		b = in[y*w + x];
		c = in[(y + 1)*w + x];
	}
	float p = ((a - c)/(a - 2.f*b + c))*0.5f;		
	float fy = y + clamp(p, -maxCorrect, maxCorrect);							
    return Touch{.x = pos.x, .y = fy, .z = pos.z};
}

float sensorToKeyY(float sy)
{
	float ky = 0.f;
	
	constexpr int mapSize = 6;
	
	// Soundplane A as measured.
	// NOTE: these y locations depend on the amount of smoothing done in smoothPressureX / smoothPressureY.
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
		// piecewise linear map
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
    return Touch{.x = mapRange(3.5f, 59.5f, 1.f, 29.f, p.x), .y = sensorToKeyY(p.y), .z = p.z};
}

// quick touch finder based on peaks of curvature. 
// this works well, but a different approach based on blob sizes / shapes could do a much better
// job with contiguous keys.
TouchArray TouchTracker::findTouches(const SensorFrame& in)
{
	constexpr int kMaxPeaks = kMaxTouches*2;
	constexpr int w = SensorGeometry::width;
	constexpr int h = SensorGeometry::height;
	const float* pIn = in.data();
	int i, j;
	
	std::array<Touch, kMaxPeaks> peaks;
    TouchArray touches{};
	std::array<std::bitset<w>, h> map;
	
	// get peaks
	float f11, f12, f13;
	float f21, f22, f23;
	float f31, f32, f33;
	
	j = 0;
	{
		auto& row = map[j];
		const float* pRow2 = pIn + (j)*w; 
		const float* pRow3 = pIn + (j + 1)*w; 
		for(i = 1; i < w - 1; ++i)
		{
			f21 = pRow2[i - 1]; f22 = pRow2[i]; f23 = pRow2[i + 1];
			f31 = pRow3[i - 1]; f32 = pRow3[i]; f33 = pRow3[1 + 1];
			row[i] = (f22 > f21) && (f22 > mFilterThreshold) && (f22 > f23)
			&& (f22 > f31) && (f22 > f32) && (f22 > f33);
		}
	}
	
	for (j = 1; j < h - 1; ++j)
	{
		auto& row = map[j];
		const float* pRow1 = pIn + (j - 1)*w; 
		const float* pRow2 = pIn + (j)*w; 
		const float* pRow3 = pIn + (j + 1)*w; 
		for(i = 1; i < w - 1; ++i)
		{
			f11 = pRow1[i - 1]; f12 = pRow1[i]; f13 = pRow1[i + 1];
			f21 = pRow2[i - 1]; f22 = pRow2[i]; f23 = pRow2[i + 1];
			f31 = pRow3[i - 1]; f32 = pRow3[i]; f33 = pRow3[1 + 1];
			row[i] = (f22 > f11) && (f22 > f12) && (f22 > f13)
				&& (f22 > f21) && (f22 > mFilterThreshold) && (f22 > f23)
				&& (f22 > f31) && (f22 > f32) && (f22 > f33);
		}	
	}
	
	j = h - 1;
	{
		auto& row = map[j];
		const float* pRow1 = pIn + (j - 1)*w; 
		const float* pRow2 = pIn + (j)*w; 
		for(i = 1; i < w - 1; ++i)
		{
			f11 = pRow1[i - 1]; f12 = pRow1[i]; f13 = pRow1[i + 1];
			f21 = pRow2[i - 1]; f22 = pRow2[i]; f23 = pRow2[i + 1];
			row[i] = (f22 > f11) && (f22 > f12) && (f22 > f13)
				&& (f22 > f21) && (f22 > mFilterThreshold) && (f22 > f23);
		}
	}
	
	// gather all peaks.
	int nPeaks = 0;
	for (int j=0; j<h; j++)
	{
		auto& mapRow = map[j];
		for (int i=0; i < w; i++)
		{
			// if peak
			if (mapRow[i])
			{
				float z = in[j*w + i];
                peaks[nPeaks++] = Touch{.x = static_cast<float>(i), .y = static_cast<float>(j), .z = z};
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
				
		Touch px = correctPeakX(p, in);	
		Touch pxy = correctPeakY(px, in);			
		touches[i] = peakToTouch(pxy);	
	}

	return touches;
}

// match incoming touches in x with previous frame of touches in x1.
// for each possible touch slot, output the touch x closest in location to the previous frame.
// if the incoming touch is a continuation of the previous one, set its age (w) to 1, otherwise to 0. 
// if there is no incoming touch to match with a previous one at index i, and no new touch needs index i, the position at index i will be maintained.

// TODO first touch below filter threshold(?) is on one index, then active touch switches index?! investigate.

TouchArray TouchTracker::matchTouches(const TouchArray& x, const TouchArray& x1)
{
	const float kMaxConnectDist = 2.f; 
	
    TouchArray newTouches{};
	
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
	
	// first, continue mutually matched  touches
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		if(mutualMatches[i])
		{
			int j = reverseMatchIdx[i];
			Touch curr = x[i];		
			Touch prev = x1[j];
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
	// these filter settings have a big and sort of delicate impact on play feel, so they are not user settable
	const float kFixedXYFreqMax = 20.f;
	const float kFixedXYFreqMin = 1.f;
	
    TouchArray out{};
	
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
			float freq = mapAndClipRange(0., 0.02, kFixedXYFreqMin, kFixedXYFreqMax, z); 			
			float omegaXY = freq*kTwoPi/mSampleRate;
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
		
        out[i] = Touch{.x = newX, .y = newY, .z = z, .age = age};
	}
	
	return out;
}

TouchArray TouchTracker::filterTouchesZ(const TouchArray& in, const TouchArray& inz1, float upFreq, float downFreq)
{
	const float omegaUp = upFreq*kTwoPi/mSampleRate;
	const float kUp = expf(-omegaUp);
	const float a0Up = 1.f - kUp;
	const float b1Up = kUp;
	const float omegaDown = downFreq*kTwoPi/mSampleRate;
	const float kDown = expf(-omegaDown);
	const float a0Down = 1.f - kDown;
	const float b1Down = kDown;
	
    TouchArray out{};
	
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		float x = in[i].x;
		float y = in[i].y;
		float z = in[i].z;

		float z1 = inz1[i].z;
		int age1 = inz1[i].age;		

        float newZ;
		
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
        bool gate1 = (age1 > 0);
        bool newGate = gate1;
		if(newZ > mOnThreshold)
		{
			newGate = true;
		}
		else if (newZ < mOffThreshold)
		{
			newGate = false;
		}
		
		// increment age
        int newAge = newGate ? (age1 + 1) : 0;

        // set state
        int newState = newGate ? (gate1 ? kTouchStateContinue : kTouchStateOn) : (gate1 ? kTouchStateOff : kTouchStateInactive);
		
        out[i] = Touch{.x=x, .y=y, .z=newZ, .dz=dz, .age=newAge, .state=newState};
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

TouchArray TouchTracker::clampAndScaleTouches(const TouchArray& in)
{
	const float kTouchOutputScale = 4.f;
    TouchArray out{};
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Touch t = in[i];
		
		if(t.x != t.x)
		{
			t.x = (0.f);
		}
		if(t.y != t.y)
		{
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

