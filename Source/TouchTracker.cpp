
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "TouchTracker.h"

#include <algorithm>
#include <numeric>


template<size_t ROW_LENGTH>
void appendVectorToRow(std::array<Vec4, ROW_LENGTH>& row, Vec4 b)
{
	// if full (last element is not null), return
	if(row[ROW_LENGTH - 1]) { debug() << "!"; return; }
	
	auto firstNull = std::find_if(row.begin(), row.end(), [](Vec4 a){ return !bool(a); });
	*firstNull = b;
}


inline float cityBlockDistance(Vec4 a, Vec4 b)
{
	return fabs(a.x() - b.x()) + fabs(a.y() - b.y());
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
	mSampleRate(1000.f),
	mPrevTouchForRotate(0),
	mRotate(false),
	mDoNormalize(true),
	mUseTestSignal(false)
{

	mBackground.setDims(w, h);
	mFilteredInput.setDims(w, h);
	mFilteredInputX.setDims(w, h);
	mFilteredInputY.setDims(w, h);
	mCalibrationProgressSignal.setDims(w, h);
	

	// clear key states
	for(auto& row : mKeyStates1.data)
	{
		row.fill(Vec4());	
	} 	
	for(auto& row : mKeyStates.data)
	{
		row.fill(Vec4());	
	} 	
	
	// clear previous pings
	for(auto& row : mPingsHorizY1.data)
	{
		row.fill(Vec4::null());	
	} 
	for(auto& row : mPingsVertY1.data)
	{
		row.fill(Vec4::null());	
	} 
	
	mTouchSortOrder.fill(0);
	for(int i = 0; i < kMaxTouches; i++)
	{
		mTouchSortOrder[i] = i;
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

void TouchTracker::setRotate(bool b)
{ 
	mRotate = b; 
	if(!b)
	{
		mPrevTouchForRotate = 0;
	}
}

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
	
	debug() << "mOnThreshold: " << mOnThreshold << "\n";
	
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

	{	
		// convolve input with 3x3 smoothing kernel.
		// a lot of filtering is needed here to get good position accuracy for Soundplane A.
		float kc, kex, key, kk;	
		kc = 4./16; kex = 2./16.; key = 2./16.; kk=1./16.;
		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		
		// MLTEST
		mCalibratedSignal = mFilteredInput;

		if(mMaxTouchesPerFrame > 0)
		{
			mThresholdBits = findThresholdBits(mFilteredInput);
			
			mPingsHorizRaw = findPings<kSensorRows, kSensorCols, 0>(mThresholdBits, mFilteredInput);
			mPingsVertRaw = findPings<kSensorCols, kSensorRows, 1>(mThresholdBits, mFilteredInput);
			
//			mPingsHorizRaw = reducePingsH(mPingsHorizRaw);
			
			mKeyStates = pingsToKeyStates(mPingsHorizRaw, mPingsVertRaw, mKeyStates1);
			
			mKeyStates1 = mKeyStates;
							
			// get touches, in key coordinates
			mTouchesRaw = findTouches(mKeyStates);
			mTouches = combineTouches(mTouchesRaw);
			
			
			mTouches = sortTouchesWithHysteresis(mTouches, mTouchSortOrder);
			
			mTouches = limitNumberOfTouches(mTouches);
			
			
			mTouches = matchTouches(mTouches, mTouchesMatch1);	
			mTouches = filterTouchesXY(mTouches, mTouchesMatch1);
			mTouchesMatch1 = mTouches;
		
			
			mTouches = filterTouchesZ(mTouches, mTouches1);
			mTouches1 = mTouches;
			
			// match then filter within feedback picks up lingering touches. but is otherwise more stable. 
			// how to not pick them up?
			
			
			mTouches = clampTouches(mTouches);
			
			
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

	if (mCount++ > 1000) 
	{
		mCount = 0;			 
	}   
 
}


TouchTracker::SensorBitsArray TouchTracker::findThresholdBits(const MLSignal& in)
{
	// TODO add expert setting?
	// also, this can be reduced when we reject disconnected touches better.
	// maybe make adaptive but do not increase with filter threshold.
	const float kMinPressureThresh = 0.0008f; 
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
	
	if(0)
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
	constexpr float kThresh = 0.0001f;
	
	// TEST
	float maxZ, yAtMaxZ;
	maxZ = 0;
	float maxDz = -MAXFLOAT;
	float minDz = MAXFLOAT;
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
					float dzm2 = 0.f;
					float dzm3 = 0.f;
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

							float p = ((a - c)/(a - 2.f*b + c))*0.5f;
							float x = i - 2.f + p;							
							float za = zm3;
							float zb = zm2;
							float zc = zm1;
							float zPeak = zb - 0.25f*(za - zc)*p;
							

							// TODO ad-hoc correction away from center. Right now two touches close by still influence one another a bit.

							
							if(within(x, intSpanStart + 0.f, intSpanEnd - 0.f))
							{
								
								
								appendVectorToRow(y.data[j], Vec4(x, zPeak, 0.f, 0.f));

								if(zPeak > maxZ)
								{
									maxZ = zPeak;
									yAtMaxZ = x;
								}
							}
						}
						
						zm3 = zm2;
						zm2 = zm1;
						zm1 = z;
						dzm3 = dzm2;
						dzm2 = dzm1;
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
	
	// display # pings per row or column
	//if(0)
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
			debug() << "max dz: " << maxDz  <<  "  min dz: " << minDz  << "\n"; 
		}
	}
	
	//	if(maxK > 0.f)
	//	debug() << "max k: " << maxK  << "max z: " << maxZ << "\n";
	return y;
}

// if pairs of pings are closer than a cutoff distance, remove the lesser of the two
TouchTracker::VectorsH TouchTracker::reducePingsH(const TouchTracker::VectorsH& pings)
{
	const float kMinDist = 3.0f;
	TouchTracker::VectorsH out;
	
	int j = 0;
	for(auto pingsArray : pings.data)
	{
		int n = 0;
		for(Vec4 ping : pingsArray)
		{
			if(!ping) break;
			n++;
		}
		out.data[j].fill(Vec4::null());
		
		for(int i=0; i<n; ++i)
		{
			Vec4 left = pingsArray[i];

			if(i < n - 1)
			{
				
				Vec4 right = pingsArray[i + 1];
				if(right.x() - left.x() < kMinDist)
				{
					
					// TODO makes discontinuity
					
					Vec4 larger = pingsArray[i + (right.y() > left.y())];					
					appendVectorToRow(out.data[j], larger);
					i++;
				}
				else
				{
					appendVectorToRow(out.data[j], left);
				}
			}
			else
			{
				appendVectorToRow(out.data[j], left);
				
			}
		}
		 
		j++;
	}
	return out;
}


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

					// multiplying x by y pings means both must be present
					float zn = zVec.z();
					float wn = zVec.w();
					float z = sqrtf((cz/zn)*(cw/wn)) * 8.f;			
					
					// reject below a low threshold here to reduce # of key states we have to process
					const float kMinKeyZ = 0.001f;
					if(z < kMinKeyZ) z = 0.f;
					
					key.setZ(z);
					
				//	debug() << "*";
				}
				else
				{
					// keep last valid position for decay
					
					Vec4 prevKey = ym1.data[j][i];
					key = Vec4(prevKey.x(), prevKey.y(), 0.f, 0.f);
					
			//		key = Vec4(prevKey.x(), prevKey.y(), prevKey.z(), 0.f);
					
				//	debug() << ".";
					
					// TEST
				//	key = Vec4(0.5f, 0.5f, 0.f, 0.f);
					
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

			if(z > mFilterThreshold) 
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
	
	return touches;
}


std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::combineTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& in)
{	
	float kCombineDistance = 2.0f; // minimum distance in keys, width and height -- must be > max connect distance!
	
	std::array<Vec4, kMaxTouches> touches(in);
	int nIn = 0;
	for(int i = 0; i < touches.size(); i++)
	{
		if (touches[i].z() == 0.)
		{
			break;
		}
		nIn++;
	}
	
	std::array<Vec4, kMaxTouches> out(in);
	if(nIn > 1)
	{
	
		std::array<int, kMaxTouches> used;
		used.fill(0);
		
		out.fill(Vec4());
		int nOut = 0;
		
		// for each touch i, collect centroid of any touches near i and mark those touches as used 
		for(int i=0; i<nIn; ++i)
		{
			Vec4 ta = touches[i];
			
			if(!used[i])
			{
				float ax = ta.x();
				float ay = ta.y();
				float az = ta.z();

				float sxz = ax*az;
				float syz = ay*az;
				float sz = az;
				
				for(int j = i + 1; j<nIn; ++j)
				{
					Vec4 tb = touches[j];
					if(!used[j])
					{
						if(cityBlockDistance(ta, tb) < kCombineDistance)
						{						
							float bx = tb.x();
							float by = tb.y();
							float bz = tb.z();
							
							sxz += bx*bz;
							syz += by*bz;
							sz += bz;
							used[j] = true;
						}
					}
				}
				
				out[nOut++] = Vec4(sxz/sz, syz/sz, az, 0.f);
				
			}
		}
	}
	
//	if(nIn > nOut)
//	debug() << "\ncombine:" << nIn << "->" << nOut << "\n";
	
	if(mCount == 0)
	{
		debug() << "combine: ";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			debug() << out[i];
		}
		debug() << "\n";
	}
	
	return out;
}



// sort the input touches in z order. A hysteresis offset for each array member prevents members from changing order too often.
// side effect: the new sorted order is written to the currentSortedOrder array.
//
std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::sortTouchesWithHysteresis(const std::array<Vec4, TouchTracker::kMaxTouches>& in, std::array<int, TouchTracker::kMaxTouches>& previousSortedOrder)
{	
	const float kHysteresisOffset = 0.01f; // TODO adjust
	
	std::array<Vec4, kMaxTouches> preSort(in); // TEMP
	std::array<Vec4, kMaxTouches> postSort(in); // TEMP
	std::array<Vec4, kMaxTouches> touches(in);
	std::array<int, TouchTracker::kMaxTouches> newSortedOrder;
	
	// count input touches
	int n = 0;
	for(int i=0; i<kMaxTouches; ++i)
	{
		if(preSort[i].z() == 0.f) break;
		n++;
	}
	
	// sort by x first to give stable initial order
	std::sort(preSort.begin(), preSort.begin() + kMaxTouches, [](Vec4 a, Vec4 b){ return a.x() > b.x(); } );
	
	postSort = preSort; // TEMP
	
	// add multiples of hysteresis offset to input data according to previous sorted order
	for(int i = 0; i < kMaxTouches; i++)
	{
		int v = kMaxTouches - i;
		postSort[previousSortedOrder[i]].setZ(postSort[previousSortedOrder[i]].z() + v*kHysteresisOffset);
		postSort[i].setW(i); // stash index in w
	}
	
	std::sort(postSort.begin(), postSort.begin() + kMaxTouches, [](Vec4 a, Vec4 b){ return a.z() > b.z(); } );
	
	// get new sorted order
	for(int i = 0; i < kMaxTouches; i++)
	{
		newSortedOrder[i] = postSort[i].w();
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
				debug() << in[i].z() << " ";
			}
			
			debug() << "\n    pre: ";		
			for(int i=0; i<kMaxTouches; ++i)
			{			
				debug() << preSort[i].z() << " ";
			}
			
			debug() << "\n    post: ";		
			for(int i=0; i<kMaxTouches; ++i)
			{			
				debug() << postSort[i].z() << " ";
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
				debug() << touches[i].z() << " ";
			}
			
			
			debug() << "\n";		 
		}
	previousSortedOrder = newSortedOrder;
	
	if(mCount == 0)
	{
		debug() << "sort: ";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			debug() << touches[i];
		}
		debug() << "\n";
	}
	
	return touches;
}



std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::limitNumberOfTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& in)
{	
	std::array<Vec4, kMaxTouches> touches(in);

	/*
	int nTouches = 0;
	for(int i = 0; i < touches.size(); i++)
	{
		if (touches[i].z() == 0.)
		{
			nTouches = i;
			break;
		}
	}
*/
	
	// limit number of touches by overwriting with zeroes
	for(int i = mMaxTouchesPerFrame; i < kMaxTouches; i++)
	{
		touches[i] = Vec4();
	}
	
	if(mCount == 0)
	{
		debug() << "limit: ";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			debug() << touches[i];
		}
		debug() << "\n";
	}
	
	return touches;
}

int TouchTracker::getFreeIndex(std::array<Vec4, TouchTracker::kMaxTouches> touches, Vec4 t, int currIdx, int prevIdx)
{
	
	/*
	// return the index of the free touch with closest latest position to input position pos.
	float minDist = MAXFLOAT;
	int minIdx = -1;
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Vec4 a = touches[i];
		
		if((a.z() == 0.f) || ((prevIdx == currIdx) && (currIdx == i)))// don't allow stealing any current input unless our index is not changing
		{
			float d = cityBlockDistance(t, a);
			if(d < minDist)
			{
				minDist = d;
				minIdx = i;
			}
		}
	}

	freeIdx = minIdx;
*/
	// when to rotate? if all are free?
	
	
	// write new
	// find a free spot (TODO rotate) 
	
	

	int freeIdx = -1;
	for(int j=0; j<mMaxTouchesPerFrame; ++j)
	{
	   int k = j % mMaxTouchesPerFrame;
	   if(touches[k].z() == 0.0)
	   {
		   freeIdx = k;
		   break;
	   }
	}
	
	
	if(mCount == 0)
	{
		debug() << "         getFreeIndex: prevIdx: " << prevIdx << " currIdx: " << currIdx << " curr: " << t << " freeIdx: " << freeIdx << "\n";
		debug() << "         newTouches: ";
		for(int i=0; i<mMaxTouchesPerFrame; ++i)
		{
			debug() << touches[i];
		}
		debug() << "\n";
	}

	return freeIdx;
}			   

// match incoming touches in x with previous frame of touches in x1.
// for each possible touch slot, output the touch x closest in location to the previous frame.
// if the incoming touch is a continuation of the previous one, set its age (w) to 1, otherwise to 0. 
// if there is no incoming touch to match with a previous one at index i, and no new touch needs index i, the position at index i will be maintained.
//
std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::matchTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& x, const std::array<Vec4, TouchTracker::kMaxTouches>& x1)
{
	
	bool dump = false; // DEBUG
	
	const float kMaxConnectDist = 2.0f;  // must be <= kCombineDistance!
	
	std::array<Vec4, kMaxTouches> newTouches;
	newTouches.fill(Vec4());
	
	std::array<int, kMaxTouches> reverseMatchIdx; 
	reverseMatchIdx.fill(-1);
	
	std::array<int, kMaxTouches> forwardMatchIdx; 
	forwardMatchIdx.fill(-1);
	
	// get number of current input touches, assuming no holes in input.
	int n = 0;
	for(; n < x.size(); n++)
	{
		if (x[n].z() == 0.) break;
	}
	
	// for each current touch, find minimum distance to a previous touch
	// with nonzero pressure
	for(int i=0; i<n; ++i)
	{
		float minDist = MAXFLOAT;
		Vec4 curr = x[i];
		Vec2 currPos(curr.x(), curr.y());
		
		// first try match with all previous z > 0
		for(int j=0; j < mMaxTouchesPerFrame; ++j)
		{
			Vec4 prev = x1[j];
			if(prev.z() > 0.f)
			{
				float distToPreviousTouch = cityBlockDistance(prev, curr);

				if(distToPreviousTouch < minDist)
				{
					reverseMatchIdx[i] = j;
					minDist = distToPreviousTouch;						
				}
			}
		}
	}

	// for each previous touch, find minimum distance to a current touch.
	// here matching with zero pressure is OK, because it lets us restart touches
	// that went to 0 for a little bit
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		float minDist = MAXFLOAT;
		Vec4 prev = x1[i];
		Vec2 prevPos(prev.x(), prev.y());
		
		for(int j=0; j < n; ++j)
		{
			Vec4 curr = x[j];
			float distToCurrentTouch = cityBlockDistance(prev, curr);
			if(distToCurrentTouch < minDist)
			{
				forwardMatchIdx[i] = j;
				minDist = distToCurrentTouch;						
			}
		}
	}
	
	

	// for each current touch k,
	// if k matched a previous touch at index j,
	// write k to new touches at index j

	// get mutual matches
	std::array<int, kMaxTouches> mutualMatches; 
	mutualMatches.fill(0);
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		Vec4 curr = x[i];
		int prevIdx = reverseMatchIdx[i];
		if(prevIdx >= 0)
		{
			Vec4 prev = x1[prevIdx];
			if(forwardMatchIdx[prevIdx] == i) 	
			{
				mutualMatches[i] = true;
			}
		}
	}
	
	
	std::array<bool, kMaxTouches> currWrittenToNew; 
	currWrittenToNew.fill(false);
	for(int i=0; i<n; ++i)
	{
		Vec4 curr = x[i];
		
		if(curr.z() > 0.f)
		{
			// a touch is here, it should be written
			
			bool close = false;
			int prevIdx = reverseMatchIdx[i];
			
			//		TODO first pass with all mutual match, then find space for outliers

			
			if(prevIdx >= 0)
			{
				Vec4 prev = x1[prevIdx];
				close = cityBlockDistance(prev, curr) < kMaxConnectDist;

				// match with the closest touch if it is active

				if(prev.z() > 0.f) // without this, leftover inactive touches pick up active touches they shouldn't
				{					
//					if((forwardMatchIdx[prevIdx] == i) && close)
						if(close)
					{	
						// touch is continued, mark as connected and write to new touches
						curr.setW(1);						
						newTouches[prevIdx] = curr;
						currWrittenToNew[i] = true;						
					}
				}
			}
	
		}
	}
	
				
	for(int i=0; i<n; ++i)
	{
		Vec4 curr = x[i];
		
		if(curr.z() > 0.f)
		{
			// a touch is here, it should be written
			
			bool close = false;
			int prevIdx = reverseMatchIdx[i];
			
			if(!currWrittenToNew[i])
			{
				// TODO  improve free select: for new unconnected touch, try not to take over a touch that might be reconnected to another thing.
				// in other words maybe use the previous touch whose minimum distance is biggest.
				
				if(mCount == 0)
				{
					dump = true;
				}
								
				int freeIdx = getFreeIndex(newTouches, curr, i, prevIdx);
				
				if(freeIdx >= 0)
				{					
					Vec4 free = x1[freeIdx];
					
					close = cityBlockDistance(free, curr) < kMaxConnectDist;
					
					curr.setW(close);
					
					newTouches[freeIdx] = curr;					
				}
				else
				{
					// fail!
					debug() << "FAIL";
				}
			}
		}
	}
		
	
	// fill in any unused touches with previous locations. This will allow old touches to re-link if not reused.
	for(int i=0; i < mMaxTouchesPerFrame; ++i)
	{
		Vec4 t = newTouches[i];

		if(t.z() == 0.f)
		{
			newTouches[i].setX(x1[i].x());
			newTouches[i].setY(x1[i].y());
		}
	}

	
	if(dump)
	{
			debug() << "\n n = " << n << "\n";
			debug() << "fwd: ";
			for(int i=0; i<mMaxTouchesPerFrame; ++i)
			{
				debug() << forwardMatchIdx[i] << " ";
			}
			debug() << "\nrev: ";
			for(int i=0; i<n; ++i)
			{
				debug() << reverseMatchIdx[i] << " ";
			}
			debug() << "\n";
			debug() << "mut: ";
			for(int i=0; i<mMaxTouchesPerFrame; ++i)
			{
				debug() << mutualMatches[i] << " ";
			}
			debug() << "\n";

		
		
		debug() << "\n";
		for(int i=0; i < mMaxTouchesPerFrame; ++i)
		{
			debug() << x1[i];
		}
		debug() << " + \n " ;
		for(int i=0; i < n; ++i)
		{
			debug() << x[i];
		}
		debug() << " -> \n " ;
		for(int i=0; i < mMaxTouchesPerFrame; ++i)
		{
			debug() << newTouches[i];
		}
		debug() << "\n";
		
		debug() << "(filtT = " << mFilterThreshold << ", offT = " << mOffThreshold << ", onT = " << mOnThreshold << "\n";
	}
	
	
	if(mCount == 0)
	{
		debug() << "match: ";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			debug() << newTouches[i];
		}
		debug() << "\n";
	}
	return newTouches;
}

// input: vec4<x, y, z, k> where k is 1 if the touch is connected to the previous touch at the same index.
//
std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::filterTouchesXY(const std::array<Vec4, TouchTracker::kMaxTouches>& in, const std::array<Vec4, TouchTracker::kMaxTouches>& inz1)
{
	float sr = 1000.f; // Soundplane A
	
	// snap to new XY value immediately when dz is over this threshold
	// TODO possibly user option
	const float kRetrigThresh = 0.007f; 
	
	MLRange zToXYFreq(0., 0.1, 4.0, 20.); 

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
//		float w1 = inz1[i].w();
		
		// filter, or not
		float newX, newY;

		
		/*
		if(newZ > mOnThreshold)
		{
			debug() << "O";
		}
		else if(newZ > mOffThreshold)
		{
			debug() << "o";
		}
		else if(newZ > mFilterThreshold)
		{
			debug() << "_";
		}
		else 
		{
			debug() << ".";
		}
		*/
		

		if(w)
		{
			
			//		debug() << "C";
			
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
			
	//		debug() << "#";
			newX = x;
			newY = y;
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
		out[i] = Vec4(newX, newY, z, w);
	}
	
	
	if(mCount == 0)
	{
		debug() << "filterxy: ";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			debug() << out[i];
		}
		debug() << "\n";
	}
	return out;
}


std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::filterTouchesZ(const std::array<Vec4, TouchTracker::kMaxTouches>& in, const std::array<Vec4, TouchTracker::kMaxTouches>& inz1)
{
	// get z coeffs from user setting
	const float sr = 1000.f;
	float omegaUp = mLopass*kMLTwoPi/sr;
	float kUp = expf(-omegaUp);
	float a0Up = 1.f - kUp;
	float b1Up = kUp;
	float omegaDown = omegaUp*0.1f;
	float kDown = expf(-omegaDown);
	float a0Down = 1.f - kDown;
	float b1Down = kDown;
	
	std::array<Vec4, TouchTracker::kMaxTouches> out;
	
	for(int i=0; i<mMaxTouchesPerFrame; ++i)
	{
		float x = in[i].x();
		float y = in[i].y();
		float z = in[i].z();

		float z1 = inz1[i].z();
		float w1 = inz1[i].w();		

		float newZ, newW;
		
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
		else
		{
			newW = w1 + 1;
		}
		
		out[i] = Vec4(x, y, newZ, newW);
	}
	
	
	if(mCount == 0)
	{
		debug() << "filter z: ";
		for(int i = 0; i < mMaxTouchesPerFrame; i++)
		{
			debug() << out[i];
		}
		debug() << "\n";
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
		
		if(t.x() != t.x())
		{
			debug() << i << "x!";
			t.setX(0.f);
		}
		if(t.y() != t.y())
		{
			debug() << i << "y!";
			t.setY(0.f);
		}
		   
		
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
	

	
	for(int i = 0; i < mMaxTouchesPerFrame; ++i)
	{
		Vec4 t = touches[i];
		out(xColumn, i) = t.x();
		out(yColumn, i) = t.y();
		out(zColumn, i) = t.z();
		out(ageColumn, i) = t.w();
	}
	

}

void TouchTracker::setDefaultNormalizeMap()
{
//	mCalibrator.setDefaultNormalizeMap();
//	mpListener->hasNewCalibration(mNullSig, mNullSig, -1.f);
}
