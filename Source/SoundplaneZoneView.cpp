
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

// temporary, ugly
void SoundplaneZoneView::drawDot(Vec2 pos, float r)
{
	Vec4 dotColor(0.8f, 0.8f, 0.8f, 1.f);
	glColor4fv(&dotColor[0]);
	int steps = 16;
    
	float x = pos.x();
	float y = pos.y();
    
	glBegin(GL_TRIANGLE_FAN);
	glVertex2f(x, y);
	for(int i=0; i<=steps; ++i)
	{
		float theta = kMLTwoPi * (float)i / (float)steps;
		float rx = r*cosf(theta);
		float ry = r*sinf(theta);
		glVertex3f(x + rx, y + ry, 0.f);
	}
	glEnd();
}	

void SoundplaneZoneView::renderZones()
{
	if (!mpModel) return;
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
	// float viewAspect = (float)viewW / (float)viewH;

	int keyWidth = 30; // Soundplane A TODO get from tracker
	// float soundplaneAspect = 4.f;
	int keyHeight = 5;
    
	//int modelWidth = mpModel->getWidth();
	//int modelHeight = mpModel->getHeight();
	int state = mpModel->getDeviceState();

    orthoView(viewW, viewH);
    int margin = viewW / 50;
	MLRange xRange(0, keyWidth, margin, viewW - margin);
	MLRange yRange(0, keyHeight, margin, viewH - margin);
 	
	Vec4 lineColor;
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 gray(0.6f, 0.6f, 0.6f, 1.f);
	Vec4 lightGray(0.9f, 0.9f, 0.9f, 1.f);
	Vec4 blue2(0.1f, 0.1f, 0.5f, 1.f);
    	
	// draw thin lines at key grid
	lineColor = darkBlue;
	if(state != kDeviceHasIsochSync)
	{
		lineColor[3] = 0.1f;
	}
	// horiz lines
	glColor4fv(&lineColor[0]);
	for(int j=0; j<=keyHeight; ++j)
	{
		glBegin(GL_LINE_STRIP);
		for(int i=0; i<=keyWidth; ++i)
		{
			float x = xRange.convert(i);
			float y = yRange.convert(j);
			float z = 0.;
			glVertex3f(x, y, z);
		}
		glEnd();
	}
	// vert lines
	for(int i=0; i<=keyWidth; ++i)
	{
		glBegin(GL_LINE_STRIP);
		for(int j=0; j<=keyHeight; ++j)
		{
			float x = xRange.convert(i);
			float y = yRange.convert(j);
			float z = 0.;
			glVertex3f(x, y, z);
		}
		glEnd();
	}
	
	// draw dots
    float r = viewH / 100.;
	for(int i=0; i<=keyWidth; ++i)
	{
		float x = xRange.convert(i + 0.5);
		float y = yRange.convert(2.5);
		int k = i%12;
		if(k == 0)
		{
			float d = viewH / 40;
			drawDot(Vec2(x, y - d), r);
			drawDot(Vec2(x, y + d), r);
		}
		if((k == 3)||(k == 5)||(k == 7)||(k == 9))
		{
			drawDot(Vec2(x, y), r);
		}
	}
}

void SoundplaneZoneView::renderOpenGL()
{
	if (!mpModel) return;
    int backW = getBackingLayerWidth();
    int backH = getBackingLayerHeight();

     // Create an OpenGLGraphicsContext that will draw into this GL window..
    {
        ScopedPointer<LowLevelGraphicsContext> glRenderer(createOpenGLGraphicsContext (mGLContext, backW, backH));
        if (glRenderer != nullptr)
        {
            Graphics g (glRenderer);
            const Colour c = findColour(MLLookAndFeel::backgroundColor);
            OpenGLHelpers::clear (c);
            renderZones();
        }
    }
}


	