
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_TOUCH_VIEW__
#define __SOUNDPLANE_TOUCH_VIEW__

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

class SoundplaneTouchView  : 
	public juce::Component,
	public OpenGLRenderer,
	public MLWidget
{
public:
    SoundplaneTouchView();
    ~SoundplaneTouchView();
 
    // when the component creates a new internal context, this is called, and
    // we'll use the opportunity to create the textures needed.
    void newOpenGLContextCreated();
    void mouseDrag (const MouseEvent& e);
    void renderTouches();
    void renderOpenGL();
    void openGLContextClosing();
 	void setModel(SoundplaneModel* m);
	
private:
    OpenGLContext mGLContext;

	SoundplaneModel* mpModel;
};

#endif // __SOUNDPLANE_TOUCH_VIEW__

