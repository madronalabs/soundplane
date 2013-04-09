
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneTouchView.h"

SoundplaneTouchView::SoundplaneTouchView() :
	mpModel(nullptr)
{
	setInterceptsMouseClicks (false, false);	
	MLWidget::setComponent(this);

	mGLContext.setRenderer (this);
//	mGLContext.setComponentPaintingEnabled (true);
	mGLContext.attachTo (*this);
}

SoundplaneTouchView::~SoundplaneTouchView()
{
	mGLContext.detach();
}

void SoundplaneTouchView::setModel(SoundplaneModel* m)
{
	mpModel = m;
}

void SoundplaneTouchView::newOpenGLContextCreated()
{
}

void SoundplaneTouchView::openGLContextClosing()
{
}

void SoundplaneTouchView::mouseDrag (const MouseEvent& e)
{
}

void SoundplaneTouchView::renderOpenGL()
{
	if (!mpModel) return;
	if (!isShowing()) return;

	// Having used the juce 2D renderer, it will have messed-up a whole load of GL state, so
	// we'll put back any important settings before doing our normal GL 3D drawing..
	glEnable (GL_DEPTH_TEST);
	glDepthFunc (GL_ALWAYS);
	glEnable (GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable (GL_TEXTURE_2D);

	int viewW = getWidth();
	int viewH = getHeight();
	
	const MLSignal& currentTouch = mpModel->getTouchFrame();
	const MLSignal& touchHistory = mpModel->getTouchHistory();
	const int frames = mpModel->getModelFloatParam("max_touches");
		
	// erase
	const Colour c = findColour(MLLookAndFeel::backgroundColor);
	float p = c.getBrightness();
	glClearColor (p, p, p, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);	
	
	if (!frames) return;
	
	int margin = viewH / 30;
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
		// draw frames
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
			
		// draw history	
		MLRange frameXRange(fr.left(), fr.right());
		frameXRange.convertTo(MLRange(0, (float)kSoundplaneHistorySize));		
		MLRange frameYRange(0, 1);
		frameYRange.convertTo(MLRange(fr.bottom(), fr.top()));
		
		glBegin(GL_LINES);
		for(int i=fr.left() + 1; i<fr.right()-1; ++i)
		{
			int time = frameXRange(i);
			
			float force = touchHistory(2, j, time);
	//		float d = touchHistory(3, j, time);
	//		int age = touchHistory(4, j, time);
			float y = frameYRange.convert(force);
	//		float drawY = (age > 0) ? y : 0.;
	
	//		y = frameYRange.convert(d);
			
			// draw line
			glVertex2f(i, fr.bottom());	
			glVertex2f(i, y);	
		}
		glEnd();
	}
}
	