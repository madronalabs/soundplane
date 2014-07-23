
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_TOUCH_GRAPH_VIEW__
#define __SOUNDPLANE_TOUCH_GRAPH_VIEW__

#include "MLUI.h"
#include "MLWidget.h"
#include "MLLookAndFeel.h"
#include "SoundplaneModel.h"
#include "MLGL.h"

#ifdef _WIN32
 #include <windows.h>
#endif

#ifndef GL_BGRA_EXT
 #define GL_BGRA_EXT 0x80e1
#endif

class SoundplaneTouchGraphView  : 
	public juce::Component,
	public MLWidget
{
public:
    SoundplaneTouchGraphView();
    ~SoundplaneTouchGraphView();
 
    void mouseDrag (const MouseEvent& e);
    void renderTouchBarGraphs();
    void renderOpenGL();
 	void setModel(SoundplaneModel* m);
	
private:
	SoundplaneModel* mpModel;
};

#endif // __SOUNDPLANE_TOUCH_GRAPH_VIEW__

