
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
}

SoundplaneZoneView::~SoundplaneZoneView()
{
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

void SoundplaneZoneView::mouseDrag (const MouseEvent& e)
{
}


void SoundplaneZoneView::renderOpenGL()
{
	if (!mpModel) return;
	int viewW = getWidth();
	int viewH = getHeight();
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();

	const MLSignal& currentTouch = mpModel->getTouchFrame();
	const int frames = mpModel->getModelFloatParam("max_touches");
		
	// erase
	const Colour c = findColour(MLLookAndFeel::backgroundColor);
	float p = c.getBrightness();
	glClearColor (p, p, p, 1.0f);
	glClearColor (0, 1.0, 0, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);	
	
	if (!frames) return;
	
	int margin = myLookAndFeel->getSmallMargin();
	int numSize = margin*2;
	int left = margin*2 + numSize;
	
	int right = viewW - margin;
	int top = margin;
	int bottom = viewH - margin;
	
	int frameWidth = right - left;
	int frameOffset = (bottom - top)/frames;
	int frameHeight = frameOffset - margin;
	MLRect frameSize(0, 0, frameWidth, frameHeight);		

	orthoView(viewW, viewH);	
	for(int j=0; j<frames; ++j)
	{
		// draw frame outlines
		p = 0.9f;
		glColor4f(p, p, p, 1.0f);
		MLRect fr = frameSize.translated(Vec2(left, margin + j*frameOffset));
		fillRect(fr);	
		p = 0.6f;
		glColor4f(p, p, p, 1.0f);
		frameRect(fr);
		
		// draw indicators at left
		int cIdx = j % kGLViewNColors;
		glColor4fv(kGLViewIndicatorColors + 4*cIdx);
		
		MLRect r(0, 0, numSize, numSize);		
		MLRect tr = r.translated(Vec2(margin, margin + j*frameOffset + (frameHeight - numSize)/2));		
		
		int age = currentTouch(4, j);		
		if (age > 0)
			fillRect(tr);	
		else
			frameRect(tr);
	}
}
	