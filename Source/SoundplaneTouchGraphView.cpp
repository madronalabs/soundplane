
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneTouchGraphView.h"

SoundplaneTouchGraphView::SoundplaneTouchGraphView() :
	mpModel(nullptr)
{
	setInterceptsMouseClicks (false, false);	
	MLWidget::setComponent(this);
	setupGL(this);
}

SoundplaneTouchGraphView::~SoundplaneTouchGraphView()
{
}

void SoundplaneTouchGraphView::setModel(SoundplaneModel* m)
{
	mpModel = m;
}

void SoundplaneTouchGraphView::mouseDrag (const MouseEvent& e)
{
}

void SoundplaneTouchGraphView::setupOrthoView()
{
	int viewW = getBackingLayerWidth();
	int viewH = getBackingLayerHeight();	
	MLGL::orthoView(viewW, viewH);
}

void SoundplaneTouchGraphView::renderTouchBarGraphs()
{
	if (!mpModel) return;
    
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
	int viewScale = getRenderingScale();
	
	const MLSignal& currentTouch = mpModel->getTouchFrame();
	const MLSignal& touchHistory = mpModel->getTouchHistory();
	const int frames = mpModel->getFloatProperty("max_touches");
	if (!frames) return;
		
	const Colour c = findColour(MLLookAndFeel::backgroundColor);
	float p = c.getBrightness();
		
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
	
	setupOrthoView();
	
	for(int j=0; j<frames; ++j)
	{
		// draw frames
		p = 0.9f;
		glColor4f(p, p, p, 1.0f);
		MLRect fr = frameSize.translated(Vec2(left, margin + j*frameOffset));
		MLGL::fillRect(fr);	
		p = 0.6f;
		glColor4f(p, p, p, 1.0f);
        MLGL::strokeRect(fr, viewScale);
		
		// draw touch activity indicators at left
		glColor4fv(MLGL::getIndicatorColor(j));
		MLRect r(0, 0, numSize, numSize);		
		MLRect tr = r.translated(Vec2(margin, margin + j*frameOffset + (frameHeight - numSize)/2));				
		int age = currentTouch(ageColumn, j);		
		if (age > 0)
			MLGL::fillRect(tr);	
		else
			MLGL::strokeRect(tr, viewScale);
			
		// draw history	
		MLRange frameXRange(fr.left(), fr.right());
		frameXRange.convertTo(MLRange(0, (float)kSoundplaneHistorySize));		
		MLRange frameYRange(1., 0.);
		frameYRange.convertTo(MLRange(fr.bottom(), fr.top()));
		
		glBegin(GL_LINES);
		for(int i=fr.left() + 1; i<fr.right()-1; ++i)
		{
			int time = frameXRange(i);			
			float force = touchHistory(2, j, time);
			float y = frameYRange.convert(force);
			
			// draw line
			glVertex2f(i, fr.top());	
			glVertex2f(i, y);	
		}
		glEnd();
	}
}

void SoundplaneTouchGraphView::renderOpenGL()
{
	if (!mpModel) return;
    const Colour c = findColour(MLLookAndFeel::backgroundColor);
    OpenGLHelpers::clear (c);
    renderTouchBarGraphs();
}

	
