
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneZoneView.h"

SoundplaneZoneView::SoundplaneZoneView() :
	mpModel(nullptr)
{
#if JUCE_IOS
	// (On the iPhone, choose a format without a depth buffer)
	setPixelFormat (OpenGLPixelFormat (8, 8, 0, 0));
#endif
	setInterceptsMouseClicks (false, false);	
	MLWidget::setComponent(this);

	mGLContext.setRenderer (this);
//	mGLContext.setComponentPaintingEnabled (true);
	mGLContext.attachTo (*this);}

SoundplaneZoneView::~SoundplaneZoneView()
{
	mGLContext.detach();
}

void SoundplaneZoneView::setModel(SoundplaneModel* m)
{
	mpModel = m;
}

void SoundplaneZoneView::newOpenGLContextCreated()
{
#if ! JUCE_IOS
	// (no need to call makeCurrentContextActive(), as that will have
	// been done for us before the method call).
	glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth (1.0);

	glDisable (GL_DEPTH_TEST);
	glEnable (GL_TEXTURE_2D);
	glEnable (GL_BLEND);
	glShadeModel (GL_SMOOTH);

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glPixelStorei (GL_UNPACK_ALIGNMENT, 4);

	glHint (GL_LINE_SMOOTH_HINT, GL_NICEST);
	glHint (GL_POINT_SMOOTH_HINT, GL_NICEST);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
}

void SoundplaneZoneView::openGLContextClosing()
{
}

void SoundplaneZoneView::mouseDrag (const MouseEvent& e)
{
}

void SoundplaneZoneView::renderOpenGL()
{
	if (!mpModel) return;
	int viewW = getWidth();
	int viewH = getHeight();
//	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();

//	const MLSignal& currentTouch = mpModel->getTouchFrame();
		
	// erase
	const Colour c = findColour(MLLookAndFeel::backgroundColor);
	float p = c.getBrightness();
	glClearColor (p, p, p, 1.0f);
	glClearColor (0, 1.0, 0, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);	
	
//	int margin = myLookAndFeel->getSmallMargin();
//	int left = margin*2 + numSize;
	
//	int right = viewW - margin;
//	int top = margin;
//	int bottom = viewH - margin;
	

	orthoView(viewW, viewH);	

}
	