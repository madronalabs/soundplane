
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
		kc = 4./24.; kex = 3./24.; key = 3./24.; kk=2./24.;
		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		mFilteredInput.convolve3x3xy(kc, kex, key, kk);
		
		// MLTEST
		mCalibratedSignal = mFilteredInput;

		if(mMaxTouchesPerFrame > 0)
		{
			mThresholdBits = findThresholdBits(mFilteredInput);
			
			mPingsHorizRaw = findPings<kSensorRows, kSensorCols, 0>(mThresholdBits, mFilteredInput);
			mPingsVertRaw = findPings<kSensorCols, kSensorRows, 1>(mThresholdBits, mFilteredInput);
			
			mPingsHorizRaw = reducePingsH(mPingsHorizRaw);
			
			mKeyStates = pingsToKeyStates(mPingsHorizRaw, mPingsVertRaw, mKeyStates1);
			
//			mKeyStates = reduceKeyStates(mKeyStates);

//			mKeyStates = filterKeyStates(mKeyStates, mKeyStates1);
			mKeyStates1 = mKeyStates;

//			mKeyStates = combineKeyStates(mKeyStates);
			
							
			// get touches, in key coordinates
			mTouchesRaw = findTouches(mKeyStates);
			
			
			
			mTouches = combineTouches(mTouchesRaw);
			
			mTouches = matchTouches(mTouches, mTouchesMatch1);			
//			mTouches = matchTouchesSimple(mTouches, mTouchesMatch1);
			mTouchesMatch1 = mTouches;
			
			mTouches = filterTouches(mTouches, mTouches1);
//			mTouches = filterTouchesSimple(mTouches, mTouches1);
			mTouches1 = mTouches;
			
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
#if DEBUG   

	if(0)
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

							// TODO ad-hoc correction away from center. Right now two touches close by still influence one another a bit.
							
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
	
	// display # pings per row or column
	if(0)
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
					
					Vec4 prevKey = ym1.data[j][i];
					key = Vec4(prevKey.x(), prevKey.y(), 0.f, 0.f);
					
					// TEST
					//key = Vec4(0.5f, 0.5f, 0.f, 0.f);
					
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

	for(int j=0; j<kKeyRows; ++j)
	{
		for(int i=0; i<kKeyCols; ++i)
		{			
			Vec4& aOut = out.data[j][i];
			Vec4 a = in.data[j][i];
			a.setZ(0.f);
			aOut = a;
		}
	}
	
	int written = 0;
	
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
			
			int pa = (az > 0.f) && (ax > 0.5f) && (ay > 0.5f);
			int pb = (bz > 0.f) && (bx <= 1.5f) && (by > 0.5f);
			int pc = (cz > 0.f) && (cx > 0.5f) && (cy <= 1.5f);
			int pd = (dz > 0.f) && (dx <= 1.5f) && (dy <= 1.5f);
			
			int pBits = (pd << 3) | (pc << 2) | (pb << 1) | pa;
			float kx, ky, kz;
			float sxz, syz, sz;
			
			bool doWrite = true;
			
			// get position centroid for combination of states at corner
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


			// write centroid back to the proper state for the corner it's in
			if(doWrite)
			{
				Vec4* outputs[4] {&aOut, &bOut, &cOut, &dOut};
				Vec4* inputs[4] {&a, &b, &c, &d};
									
				int right = kx > 1.f;
				int top = ky > 1.f;
				int qBits = (top << 1) | right;
				
				if(right) kx -= 1.f;
				if(top) ky -= 1.f;
				
				float zOut = (*inputs[qBits]).z();
				
				*outputs[qBits] = Vec4(kx, ky, zOut, 0.f);

				written++;
			}

		}
	}
//	debug() << "[" << written << "]";
		
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
			if(newZ < mFilterThreshold) newZ = 0.f;
		
			yKey = Vec4(x, y, newZ, 0.f);
			i++;
		}
		j++;
	}
	
	// display within-ness
if(0)
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
	TouchTracker::KeyStates out = pIn;
	
	/*
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
			
			int pa = (az > 0.f) && (ax > 0.5f) && (ay > 0.5f);
			int pb = (bz > 0.f) && (bx <= 1.5f) && (by > 0.5f);
			int pc = (cz > 0.f) && (cx > 0.5f) && (cy <= 1.5f);
			int pd = (dz > 0.f) && (dx <= 1.5f) && (dy <= 1.5f);
			
			int pBits = (pd << 3) | (pc << 2) | (pb << 1) | pa;
			float kx, ky, kz;
			float sxz = 0;
			float syz = 0;
			float sz = 0;
			float maxZ = 0;

			// add z and make position centroid for any keys at this corner
			if(pa)
			{
				a.setZ(0.f); // needed? pa etc. should ensure energy is only counted once
				sxz += ax*az;
				syz += ay*az;
				sz += az;
				maxZ = max(maxZ, az);
			}
			if(pb)
			{
				b.setZ(0.f);
				sxz += bx*bz;
				syz += by*bz;
				sz += bz;
				maxZ = max(maxZ, bz);
			}
			if(pc)
			{
				c.setZ(0.f);
				sxz += cx*cz;
				syz += cy*cz;
				sz += cz;
				maxZ = max(maxZ, cz);
			}
			if(pd)
			{
				d.setZ(0.f);
				sxz += dx*dz;
				syz += dy*dz;
				sz += dz;
				maxZ = max(maxZ, dz);
			}
			if(sz > mFilterThreshold)
			{
				kx = sxz/sz;
				ky = syz/sz;
				kz = sz;
				
				Vec4* outputs[4] {&aOut, &bOut, &cOut, &dOut}; // OK
				
				
 				// get position centroid
				
				// write corner centroid back to the proper key
				int right = kx > 1.f;
				int top = ky > 1.f;
				int qBits = (top << 1) | right;
				

				if(right) kx -= 1.f;
				if(top) ky -= 1.f;

				*outputs[qBits] = Vec4(kx, ky, maxZ, 0.f);				
			}
		}		
	}
	*/
	
	// combine any close pairs horiz
	for(int j=0; j<kKeyRows; ++j)
	{
		for(int i=0; i<kKeyCols - 1; ++i)
		{
			Vec4& a = out.data[j][i];
			Vec4& b = out.data[j][i + 1];
			
			if((a.x() > 0.5f) && (b.x() < 0.5f))
			{
				float ax = a.x();
				float ay = a.y();
				float az = a.z();	
				float bx = b.x() + 1.f;
				float by = b.y();
				float bz = b.z();
				
				float sxz = ax*az + bx*bz;
				float syz = ay*az + by*bz;
				float sz = az + bz;			
				float cx = sxz/sz;
				float cy = syz/sz;
				
				if(cx < 1.f)
				{
					a = Vec4(cx, cy, az, 0.f);
					b = Vec4();
				}
				else
				{	
					a = Vec4();
					b = Vec4(cx - 1.f, cy, bz, 0.f);
				}
			}
		}
	}
	
	// combine any close pairs vert
	for(int j=0; j<kKeyRows - 1; ++j)
	{
		for(int i=0; i<kKeyCols; ++i)
		{
			Vec4& a = out.data[j][i];
			Vec4& b = out.data[j + 1][i];
			
			if((a.y() > 0.5f) && (b.y() < 0.5f))
			{
				float ax = a.x();
				float ay = a.y();
				float az = a.z();	
				float bx = b.x() + 1.f;
				float by = b.y();
				float bz = b.z();
				
				float sxz = ax*az + bx*bz;
				float syz = ay*az + by*bz;
				float sz = az + bz;			
				float cx = sxz/sz;
				float cy = syz/sz;
				
				if(cx < 1.f)
				{
					a = Vec4(cx, cy, az, 0.f);
					b = Vec4();
				}
				else
				{	
					a = Vec4();
					b = Vec4(cx, cy - 1.0f, bz, 0.f);
				}
			}
		}
	}
	
	
	if(0)
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

	if(0)
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
	float kMergeDistance = 4.0f;
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
	
	std::sort(touches.begin(), touches.begin() + nIn, [](Vec4 a, Vec4 b){ return a.z() > b.z(); } );
	
	std::array<Vec4, kMaxTouches> out;
	out.fill(Vec4());
	int nOut = 0;
	
	// for each touch i, collect centroid of any touches near i and mark those touches as used 
	for(int i=0; i<nIn; ++i)
	{
		Vec4 ta = touches[i];
		float ax = ta.x();
		float ay = ta.y();
		float az = ta.z();
		float aw = ta.w();
		Vec2 pa(ax, ay);
		if(aw == 0)
		{
		//	debug() << i << ":";
			
			float sxz = ax*az;
			float syz = ay*az;
			float sz = az;
			for(int j = i + 1; j<nIn; ++j)
			{
				Vec4 tb = touches[j];
				float bx = tb.x();
				float by = tb.y();
				float bz = tb.z();
				float bw = tb.w();
				
		//		debug() << " +" << j << ":";
				
				if(bw == 0)
				{
					Vec2 pb(bx, by);
					Vec2 dab = pb - pa;
					float d = dab.magnitude();
					if(d < kMergeDistance)
					{						
		//				sxz += bx*bz;
		//				syz += by*bz;
		//				sz += bz; // ?
						touches[j].setW(1);
						
		//				debug() << "J";
					}
				}
				else
				{
		//			debug() << "-";
				}
			}
			
			out[nOut++] = Vec4(sxz/sz, syz/sz, az, 0.f);
			
		}
		
		
		debug() << "\n";
		
	}

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

// match incoming touches in x with previous frame of touches in x1.
// for each possible touch slot, output the touch x closest in location to the previous frame.
// if the incoming touch is a continuation of the previous one, set its age (w) to 1, otherwise to 0.
//
std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::matchTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& x, const std::array<Vec4, TouchTracker::kMaxTouches>& x1)
{
	const float kMaxConnectDist = 2.0f;
	
	std::array<Touch, kMaxTouches> prevTouches{};
	std::array<Touch, kMaxTouches> currTouches{};
	std::array<Touch, kMaxTouches> newTouches{};
	
	// get max incoming indices, allowing for holes
	int m = 0, n = 0;
	for(int i = 0; i < x1.size(); i++)
	{
		if (x1[i].z() > 0.)
		{
			m = i;	
		}
	}
	
	for(int i = 0; i < x.size(); i++)
	{
		if (x[i].z() > 0.)
		{
			n = i;
		}
	}
	
	// convert to Touches
	for(int j=0; j <= m; ++j)
	{
		prevTouches[j] = vec4ToTouch(x1[j]);
		prevTouches[j].currIdx = j;
	}
	for(int i=0; i <= n; ++i)
	{
		currTouches[i] = vec4ToTouch(x[i]);
		currTouches[i].currIdx = i;
	}
	
	// categorize touches into one of continued, new, or removed.
	
	// for each current touch, find closest distance to a previous touch
	for(int i=0; i<=n; ++i)
	{
		Touch curr = currTouches[i];
		Vec2 currPos(curr.x, curr.y);
		for(int j=0; j <= m; ++j)
		{
			Touch prev = prevTouches[j];
			
			if(prev.z > 0.f)
			{
				Vec2 prevPos(prev.x, prev.y);
				Vec2 dab = currPos - prevPos;
				float dist = dab.magnitude(); // NOTE (aPos - bPos).magnitude FAILS becasue it's a Vec4! TODO	
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
	for(int i=0; i<=n; ++i)
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
				
				// mark age
				newTouches[connectedIdx].age = 1;//(connectedPrevTouch.age + 1);
				
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
				
				// new touch age 
				newTouches[freeIdx].age = 0;
				
				
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
	
	if(maxOccupiedIdx >= 0)
	{
	debug() << "------------ connected: \n";
	for(int i=0; i <= maxOccupiedIdx; ++i)
	{
		debug() << y[i].w();
	}
	debug() << "\n";
	}
	
	
	return y;
}


std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::matchTouchesSimple(const std::array<Vec4, TouchTracker::kMaxTouches>& x, const std::array<Vec4, TouchTracker::kMaxTouches>& x1)
{
	std::array<Vec4, TouchTracker::kMaxTouches> y(x);
	
	// get max incoming indices, allowing for holes
	int maxPrevIdx = -1;
	int maxCurrIdx = -1;
	for(int i = 0; i < x1.size(); i++)
	{
		if (x1[i].z() > 0.)
		{
			maxPrevIdx = i;	
		}
	}
	
	for(int i = 0; i < x.size(); i++)
	{
		if (x[i].z() > 0.)
		{
			maxCurrIdx = i;
		}
	}
	
	// sort current touches by x
	std::sort(y.begin(), y.begin() + maxCurrIdx + 1, [](Vec4 a, Vec4 b){ return a.x() > b.x(); } );

	return y;
}


std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::filterTouches(const std::array<Vec4, TouchTracker::kMaxTouches>& in, const std::array<Vec4, TouchTracker::kMaxTouches>& inz1)
{
	float sr = 1000.f; // Soundplane A
	
	// get z coeffs from user setting
	float omegaUp = mLopass*kMLTwoPi/sr;
	float kUp = expf(-omegaUp);
	float a0Up = 1.f - kUp;
	float b1Up = kUp;
	float omegaDown = omegaUp*0.25f;
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
		// w is the incoming connection flag from matchTouches()
		
		
		if((w == 1) && (newZ > mFilterThreshold))
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
		else if((dz < 0.f))
		{			
			// decay, hold position
			newX = x1;
			newY = y1;
		}
		else
		{
			// new touch, set new position
			newX = x;
			newY = y;
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


std::array<Vec4, TouchTracker::kMaxTouches> TouchTracker::filterTouchesSimple(const std::array<Vec4, TouchTracker::kMaxTouches>& in, const std::array<Vec4, TouchTracker::kMaxTouches>& inz1)
{
	
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

		float newX = x;
		float newY = y;
		float newZ = z;
		float newW;
		
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
	
	if(0)
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
		
	}
	

}

void TouchTracker::setDefaultNormalizeMap()
{
//	mCalibrator.setDefaultNormalizeMap();
//	mpListener->hasNewCalibration(mNullSig, mNullSig, -1.f);
}
