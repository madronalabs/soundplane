// geometry for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include <array>

namespace SensorGeometry
{
    constexpr int width = 64;
    constexpr int height = 8;
    constexpr int elements = width*height;
};

typedef std::array<float, SensorGeometry::elements> SensorFrame;

float get(const SensorFrame& a, int col, int row);
void set(SensorFrame& a, int col, int row, float val);
float getColumnSum(const SensorFrame& a, int col);

SensorFrame add(const SensorFrame& a, const SensorFrame& b);
SensorFrame subtract(const SensorFrame& a, const SensorFrame& b);
SensorFrame multiply(const SensorFrame& a, const SensorFrame& b);
SensorFrame divide(const SensorFrame& a, const SensorFrame& b);

SensorFrame add(const SensorFrame& a, const float k);
SensorFrame subtract(const SensorFrame& a, const float k);
SensorFrame multiply(const SensorFrame& a, const float k);
SensorFrame divide(const SensorFrame& a, const float k);

SensorFrame fill(const float k);

SensorFrame max(const SensorFrame& b, const float k);
SensorFrame min(const SensorFrame& b, const float k);
SensorFrame clamp(const SensorFrame& b, const float k, const float m);
SensorFrame sqrt(const SensorFrame& b);
SensorFrame getCurvatureX(const SensorFrame& in);
SensorFrame getCurvatureY(const SensorFrame& in);
SensorFrame getCurvatureXY(const SensorFrame& in);
SensorFrame calibrate(const SensorFrame& in, const SensorFrame& calibrateMean);
void dumpFrameAsASCII(std::ostream& s, const SensorFrame& f);
void dumpFrame(std::ostream& s, const SensorFrame& f);
void dumpFrameStats(std::ostream& s, const SensorFrame& f);

class SensorFrameStats
{
public:
	SensorFrameStats() : mCount(0) {}
	
	void clear()
	{
		mCount = 0;
	}
	
	void accumulate(SensorFrame x)
	{
		mCount++;
		
		// compute a running mean.
		// See Knuth TAOCP vol 2, 3rd edition, page 232
		if (mCount == 1)
		{
			m_oldM = m_newM = x;
			m_oldS = fill(0.0);
		}
		else
		{
			m_newM = add(m_oldM, divide(subtract(x, m_oldM), mCount));
			m_newS = add(m_oldS, multiply(subtract(x, m_oldM), subtract(x, m_newM)));
			
			// set up for next iteration
			m_oldM = m_newM; 
			m_oldS = m_newS;
		}
	}
	
	int getCount()
	{
		return mCount;
	}
	
	SensorFrame mean() const
	{
		return (mCount > 0) ? m_newM : fill(0.0);
	}
	
	SensorFrame variance() const
	{
		return ( (mCount > 1) ? divide(m_newS, (mCount - 1)) : fill(0.0) );
	}
	
	SensorFrame standardDeviation() const
	{
		return sqrt( variance() );
	}
	
private:
	int mCount;
	SensorFrame m_oldM, m_newM, m_oldS, m_newS;
};



