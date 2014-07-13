
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __TRK_CAL_VIEW__
#define __TRK_CAL_VIEW__

#include "SoundplaneModel.h"
#include "MLUI.h"
#include "MLWidget.h"
#include "MLLookAndFeel.h"
#include "MLGL.h"

/*
#ifdef _WIN32
 #include <windows.h>
#endif
*/

#ifndef GL_BGRA_EXT
 #define GL_BGRA_EXT 0x80e1
#endif

class TrackerCalibrateView  : 
	public Component,
	public MLWidget
{
public:
    TrackerCalibrateView();
    ~TrackerCalibrateView();
 
	void renderOpenGL();
 	void setModel(SoundplaneModel* m);
	void setViewMode(SoundplaneViewMode v){ mViewMode = v; }
 	
private:
	Vec2 worldToScreen(const Vec3& world); 

	void drawTextAt(float x, float y, float z, const char* ps);
	void drawInfoBox(Vec3 pos, char* text, int colorIndex);

	SoundplaneModel* mpModel;
	SoundplaneViewMode mViewMode;
};

#endif // __TRK_CAL_VIEW__

