
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
	mGLContext.attachTo (*this);
	mGLContext.setComponentPaintingEnabled (true);
    mGLContext.setContinuousRepainting(true);
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

void SoundplaneTouchView::renderTouches()
{
	if (!mpModel) return;
	if (!isShowing()) return;
    
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
	
	const MLSignal& currentTouch = mpModel->getTouchFrame();
	const MLSignal& touchHistory = mpModel->getTouchHistory();
	const int frames = mpModel->getModelFloatParam("max_touches");
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

    MLGL::orthoView(viewW, viewH);
	for(int j=0; j<frames; ++j)
	{
		// draw frames
		p = 0.9f;
		glColor4f(p, p, p, 1.0f);
		MLRect fr = frameSize.translated(Vec2(left, margin + j*frameOffset));
		MLGL::fillRect(fr);	
		p = 0.6f;
		glColor4f(p, p, p, 1.0f);
        MLGL::strokeRect(fr);
		
		// draw touch activity indicators at left
		glColor4fv(MLGL::getIndicatorColor(j));
		MLRect r(0, 0, numSize, numSize);		
		MLRect tr = r.translated(Vec2(margin, margin + j*frameOffset + (frameHeight - numSize)/2));				
		int age = currentTouch(4, j);		
		if (age > 0)
			MLGL::fillRect(tr);	
		else
			MLGL::strokeRect(tr);
			
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

void SoundplaneTouchView::renderOpenGL()
{
	if (!mpModel) return;
    int backW = getBackingLayerWidth();
    int backH = getBackingLayerHeight();
    
    // Create an OpenGLGraphicsContext that will draw into this GL window..
    {
 //       ScopedPointer<LowLevelGraphicsContext> glRenderer(createOpenGLGraphicsContext (mGLContext, backW, backH));
   //     if (glRenderer != nullptr)
        {
//            Graphics g (glRenderer);
            // g.addTransform (AffineTransform::scale ((float) getScale()));
            const Colour c = findColour(MLLookAndFeel::backgroundColor);
            OpenGLHelpers::clear (c);
            renderTouches();
        }
    }
}

// GL views need to attach to their components here, because on creation
// the component might not be visible and can't be attached to.
void SoundplaneTouchView::resizeWidget(const MLRect& b, const int u)
{
    MLWidget::resizeWidget(b, u);
    mGLContext.attachTo (*MLWidget::getComponent());
}


	