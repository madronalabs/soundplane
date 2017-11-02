// geometry for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SensorFrame.h"
#include <iostream>
#include <cmath>

template <class c>
c (clamp)(const c& x, const c& min, const c& max)
{
	return (x < min) ? min : (x > max ? max : x);
}

template <class c>
inline bool (within)(const c& x, const c& min, const c& max)
{
	return ((x >= min) && (x < max));
}

float get(const SensorFrame& a, int col, int row)
{
	return a[row*SensorGeometry::width + col];
}

void set(SensorFrame& a, int col, int row, float val)
{
	a[row*SensorGeometry::width + col] = val;
}

float getColumnSum(const SensorFrame& a, int col)
{
	float sum = 0.f;
	for(int j=0; j<SensorGeometry::height; ++j)
	{
		sum += get(a, col, j);
	}
	return sum;
}

SensorFrame add(const SensorFrame& a, const SensorFrame& b)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = a[i] + b[i];
	}
	return out;
}

SensorFrame subtract(const SensorFrame& a, const SensorFrame& b)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = a[i] - b[i];
	}
	return out;
}

SensorFrame multiply(const SensorFrame& a, const SensorFrame& b)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = a[i]*b[i];
	}
	return out;
}

SensorFrame divide(const SensorFrame& a, const SensorFrame& b)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = a[i]/b[i];
	}
	return out;
}

SensorFrame add(const SensorFrame& a, const float k)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = a[i] + k;
	}
	return out;
}

SensorFrame subtract(const SensorFrame& a, const float k)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = a[i] - k;
	}
	return out;
}

SensorFrame multiply(const SensorFrame& a, const float k)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = a[i]*k;
	}
	return out;
}

SensorFrame divide(const SensorFrame& a, const float k)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = a[i]/k;
	}
	return out;
}

SensorFrame fill(const float k)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = k;
	}
	return out;
}

SensorFrame max(const SensorFrame& b, const float k)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = std::max(b[i], k);
	}
	return out;
}

SensorFrame min(const SensorFrame& b, const float k)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = std::min(b[i], k);
	}
	return out;
}

SensorFrame clamp(const SensorFrame& b, const float k, const float m)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = clamp(b[i], k, m);
	}
	return out;
}

SensorFrame sqrt(const SensorFrame& b)
{
	SensorFrame out;
	for(int i=0; i<SensorGeometry::elements; ++i)
	{
		out[i] = sqrtf(b[i]);
	}
	return out;
}

SensorFrame getCurvatureX(const SensorFrame& in)
{
	SensorFrame out;
	
	// rows
	for(int j=0; j<SensorGeometry::height; ++j)
	{
		float z = 0.f;
		float zm1 = 0.f;
		float dz = 0.f;
		float dzm1 = 0.f;
		float ddz = 0.f;
		
		for(int i=0; i <= SensorGeometry::width; ++i)
		{
			if(within(i, 0, SensorGeometry::width))
			{
				z = in[j*SensorGeometry::width + i];
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
				out[(j)*SensorGeometry::width + i - 1] = std::max(-ddz, 0.f);
			}
		}
	}	
	
	return out;
}

SensorFrame getCurvatureY(const SensorFrame& in)
{
	SensorFrame out;
	
	// cols
	for(int i=0; i<SensorGeometry::width; ++i)
	{
		float z = 0.f;
		float zm1 = 0.f;
		float dz = 0.f;
		float dzm1 = 0.f;
		float ddz = 0.f;
		for(int j=0; j <= SensorGeometry::height; ++j)
		{
			if(within(j, 0, SensorGeometry::height))
			{
				z = in[j*SensorGeometry::width + i];
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
				out[(j - 1)*SensorGeometry::width + i] = std::max(-ddz, 0.f);
			}
		}
	}
	
	return out;
}

SensorFrame getCurvatureXY(const SensorFrame& in)
{
	return sqrt(multiply(getCurvatureX(in), getCurvatureY(in)));
} 

SensorFrame calibrate(const SensorFrame& in, const SensorFrame& calibrateMean)
{
	return subtract(divide(in, calibrateMean), 1.0f);
} 

void dumpFrameAsASCII(std::ostream& s, const SensorFrame& f)
{
	const char* g = " .:;+=xX$&";
	int w = SensorGeometry::width;
	int h = SensorGeometry::height;
	
	int scale = strlen(g);
	for (int j=0; j<h; ++j)
	{
		s << "|";
		for(int i=0; i<w; ++i)
		{
			int v = (f[j*w + i]*scale);
			s << g[clamp(v, 0, scale - 1)];
		}
		s << "|\n";
	}
}
