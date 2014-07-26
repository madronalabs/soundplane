
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneGridView.h"
#include "SoundplaneBinaryData.h"

SoundplaneGridView::SoundplaneGridView() :
	mpModel(nullptr),
	rotation(0.)
{
	setInterceptsMouseClicks (false, false);
	MLWidget::setComponent(this);
    MLWidget::setupGL(this);
}

SoundplaneGridView::~SoundplaneGridView()
{
}

void SoundplaneGridView::drawInfoBox(Vec3 pos, char* text, int colorIndex)
{
    //int bScale = getBackingLayerScale();
	int viewW = getBackingLayerWidth();
	int viewH = getBackingLayerHeight();

	int len = strlen(text);
	clamp(len, 0, 32);
	
	float margin = 2;
	float charWidth = 5; 
	float charHeight = 9;
	float w = len * charWidth + margin*2;
	float h = charHeight + margin*2;
//	float shadow = 2; // TODO
	
	Vec3 rectPos = pos;
	rectPos[2] = 0.2f;
	Vec3 surfacePos = pos;	
	surfacePos[2] = 0.f;
	Vec2 screen = MLGL::worldToScreen(rectPos);
	Vec2 surface = MLGL::worldToScreen(surfacePos);
//	screen[0] -= margin;
//	screen[1] -= margin;

	// push ortho projection
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, viewW, 0, viewH, -1, 1);	

	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glTranslatef(0, 0, 0);

	// box
	glColor4f(1, 1, 1, 1);
	glBegin(GL_QUADS);
	glVertex2f(screen[0], screen[1]);
	glVertex2f(screen[0] + w, screen[1]);
	glVertex2f(screen[0] + w, screen[1] + h);
	glVertex2f(screen[0], screen[1] + h);
	glEnd();
	
	// outline
	glColor4fv(MLGL::getIndicatorColor(colorIndex));
	glBegin(GL_LINE_LOOP);
	glVertex2f(screen[0], screen[1]);
	glVertex2f(screen[0] + w, screen[1]);
	glVertex2f(screen[0] + w, screen[1] + h);
	glVertex2f(screen[0], screen[1] + h);
	glEnd();
	
	// line down to surface
	glColor4fv(MLGL::getIndicatorColor(colorIndex));
	glBegin(GL_LINES);
	glVertex2f(screen[0], screen[1]);
	glVertex2f(surface[0], surface[1]);
	glEnd();
	
	// text
	glColor4fv(MLGL::getIndicatorColor(colorIndex));
    MLGL::drawTextAt(screen[0] + margin, screen[1] + margin, 0.f, text);
	
	// outline

	// pop ortho projection		
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
}

void SoundplaneGridView::setModel(SoundplaneModel* m)
{
	mpModel = m;
}

void SoundplaneGridView::renderXYGrid()
{
	int sensorWidth = mpModel->getWidth();
	int sensorHeight = mpModel->getHeight();

	int state = mpModel->getDeviceState();
	float fMax = mpModel->getFloatProperty("z_max");
    
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
	int margin = viewH / 30;
    
	int keyWidth = 30; // Soundplane A TODO get from tracker
	int keyHeight = 5;
    
    // put origin in lower left.
    MLGL::orthoView2(viewW, viewH);
    MLRange xRange(0, keyWidth, margin, viewW - margin);
	MLRange yRange(0, keyHeight, margin, viewH - margin);
    
	// Soundplane A
	MLRange xmRange(2, sensorWidth-2);
	xmRange.convertTo(MLRange(margin, viewW - margin));
	MLRange ymRange(0, sensorHeight);
	ymRange.convertTo(MLRange(margin, viewH - margin));

	float dotSize = fabs(yRange(0.08f) - yRange(0.f));
	const MLSignal& calSignal = mpModel->getSignalForViewMode(kCalibrated);
	float displayScale = mpModel->getFloatProperty("display_scale");
	
	// draw stuff in immediate mode. 
	// TODO don't use fixed function pipeline.
	//
	Vec4 lineColor;
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 gray(0.6f, 0.6f, 0.6f, 1.f);
	Vec4 lightGray(0.9f, 0.9f, 0.9f, 1.f);
	Vec4 blue2(0.1f, 0.1f, 0.5f, 1.f);
    
	// fill calibrated data areas
	for(int j=0; j<sensorHeight; ++j)
	{
		// Soundplane A-specific
		for(int i=2; i<sensorWidth - 2; ++i)
		{
			float mix = calSignal(i, j) / fMax;
			mix *= displayScale;
			Vec4 dataColor = vlerp(gray, lightGray, mix);
			glColor4fv(&dataColor[0]);
			glBegin(GL_QUADS);
			float x1 = xmRange.convert(i);
			float y1 = ymRange.convert(j);
			float x2 = xmRange.convert(i + 1);
			float y2 = ymRange.convert(j + 1);
			float z = 0.;
			glVertex3f(x1, y1, -z);
			glVertex3f(x2, y1, -z);
			glVertex3f(x2, y2, -z);
			glVertex3f(x1, y2, -z);
			glEnd();
		}
	}	
	
	// draw lines at key grid
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
			glVertex3f(x, y, -z);
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
			glVertex3f(x, y, -z);
		}
		glEnd();
	}
	
	// draw guitar dots
	for(int i=0; i<=keyWidth; ++i)
	{
		float x = xRange.convert(i + 0.5);
		float y = yRange.convert(2.5);
		int k = i%12;
        glColor4f(1.0f, 1.0f, 1.0f, 0.75f);
		if(k == 0)
		{
			MLGL::drawDot(Vec2(x, y - dotSize*1.5), dotSize);
			MLGL::drawDot(Vec2(x, y + dotSize*1.5), dotSize);
		}
		if((k == 3)||(k == 5)||(k == 7)||(k == 9))
		{
			MLGL::drawDot(Vec2(x, y), dotSize);
		}
	}
	
	// render current touch dots
	//
	const int nt = mpModel->getFloatProperty("max_touches");
	const MLSignal& touches = mpModel->getTouchFrame();

	for(int t=0; t<nt; ++t)
	{
		int age = touches(ageColumn, t);
		if (age > 0)
		{
			float x = touches(xColumn, t);
			float y = touches(yColumn, t);
                        
			Vec2 xyPos(x, y);
			Vec2 gridPos = mpModel->xyToKeyGrid(xyPos);
			float tx = xRange.convert(gridPos.x());
			float ty = yRange.convert(gridPos.y());
			float tz = touches(zColumn, t);
			
            Vec4 dataColor(MLGL::getIndicatorColor(t));
            dataColor[3] = 0.75;
			glColor4fv(&dataColor[0]);
			MLGL::drawDot(Vec2(tx, ty), dotSize*10.0*tz);
		}
	}
	
	// render touch position history xy
	const MLSignal& touchHistory = mpModel->getTouchHistory();
    
	int ctr = mpModel->getHistoryCtr();
	for(int touch=0; touch<nt; ++touch)
	{
		int age = touches(ageColumn, touch);
		if (age > 0)
		{
			glColor4fv(MLGL::getIndicatorColor(touch));
			glBegin(GL_LINE_STRIP);
			int cc = ctr;
			int a = 0;
			for(int t=0; t < kSoundplaneHistorySize - 2; ++t)
			{				
				float x = touchHistory(xColumn, touch, cc);
				float y = touchHistory(yColumn, touch, cc);
				if((x > 0.) && (y > 0.))
				{
					Vec2 gridPos = mpModel->xyToKeyGrid(Vec2(x, y));
					float px = xRange.convert(gridPos.x());
					float py = yRange.convert(gridPos.y());
					glVertex3f(px, py, 0.);	
				}
				if(--cc < 0) { cc = kSoundplaneHistorySize - 1; }
				if(++a >= age - 2) break;
			}
			glEnd();
		}
	}
}

void SoundplaneGridView::renderZGrid()
{
	if (!mpModel) return;
	int width = mpModel->getWidth();
	int height = mpModel->getHeight();
    
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
    
    const float scale = getRenderingScale();
    
    ScopedPointer<LowLevelGraphicsContext> glRenderer
        (createOpenGLGraphicsContext (*getGLContext(), viewW, viewH));
    
    if (glRenderer != nullptr)
    {
        Graphics g (*glRenderer);
        
        g.addTransform (AffineTransform::scale (scale));    // draw grid
        float myAspect = (float)viewW / (float)viewH;
        float soundplaneAspect = 4.f; // TEMP
        int state = mpModel->getDeviceState();
        
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(8.0, myAspect, 0.5, 50.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        gluLookAt(0.0, -16.0, 3., // eyepoint x y z
                  0.0, 0.0, -0.5, // center x y z
                  0.0, 1.0, 0.0); // up vector
        
        glColor4f(1, 1, 1, 0.5);
        MLRange xRange(0, width-1);
        float r = 0.95;
        xRange.convertTo(MLRange(-myAspect*r, myAspect*r));
        MLRange yRange(0, height-1);
        float sh = myAspect*r/soundplaneAspect;
        yRange.convertTo(MLRange(-sh, sh));
        
        const MLSignal& viewSignal = mpModel->getSignalForViewMode(mViewMode);
        
        float displayScale = mpModel->getFloatProperty("display_scale");
        float scale = displayScale;
        float offset = 0.f;
        bool separateSurfaces = false;
        switch(mViewMode)
        {
            case kRaw:
                offset = -1.;
                scale *= 8.;
                separateSurfaces = true;
                break;
            case kCalibrated:
                scale *= 10.;
                break;
            case kCooked:
                scale *= 10.;
                break;
            case kNrmMap:
                offset = -displayScale;
                scale *= 1.;
                break;
            case kTest1:
            default:
                scale *= 10.;
                break;
        }
        
        // draw stuff in immediate mode. TODO vertex buffers and modern GL code in general.
        //
        Vec4 lineColor;
        Vec4 white(1.f, 1.f, 1.f, 1.f);
        Vec4 darkBlue(0.f, 0.f, 0.4f, 1.f);
        Vec4 green(0.f, 0.5f, 0.1f, 1.f);
        Vec4 blue(0.1f, 0.1f, 0.9f, 1.f);
        Vec4 purple(0.7f, 0.2f, 0.7f, 0.5f);
        
        int leftEdge = 0;
        int rightEdge = width;
        if(mViewMode == kNrmMap)
        {
            leftEdge += 1;
            rightEdge -= 1;
        }
        
        if (separateSurfaces)
        {
            // draw lines
            for(int i=0; i<width; ++i)
            {
                // alternate colors every flex circuit
                lineColor = (i/16)&1 ? darkBlue : blue;
                if(state != kDeviceHasIsochSync)
                {
                    lineColor[3] = 0.1f;
                }
                glColor4fv(&lineColor[0]);
                
                // vert
                glBegin(GL_LINE_STRIP);
                for(int j=0; j<height; ++j)
                {
                    float x = xRange.convert(i);
                    float y = yRange.convert(j);
                    float zMean = viewSignal(i, j)*scale + offset;
                    glVertex3f(x, y, -zMean);
                }
                glEnd();
                
                // horiz
                if (i%16 != 15)
                {
                    glBegin(GL_LINES);
                    for(int j=0; j<height; ++j)
                    {
                        float x1 = xRange.convert(i);
                        float y1 = yRange.convert(j);
                        float z1 = viewSignal(i, j)*scale + offset;
                        glVertex3f(x1, y1, -z1);
                        
                        float x2 = xRange.convert(i + 1);
                        float y2 = yRange.convert(j);
                        float z2 = viewSignal(i + 1, j)*scale + offset;
                        glVertex3f(x2, y2, -z2);
                    }
                    glEnd();
                }
            }
        }
        else
        {
            lineColor = darkBlue;
            if(state != kDeviceHasIsochSync)
            {
                lineColor[3] = 0.1f;
            }
            glColor4fv(&lineColor[0]);
            for(int j=0; j<height; ++j)
            {
                glBegin(GL_LINE_STRIP);
                for(int i=leftEdge; i<rightEdge; ++i)
                {
                    float x = xRange.convert(i);
                    float y = yRange.convert(j);
                    float z = viewSignal(i, j)*scale + offset;
                    glVertex3f(x, y, -z);
                }
                glEnd();
            }
            // vert lines
            for(int i=leftEdge; i<rightEdge; ++i)
            {
                glBegin(GL_LINE_STRIP);
                for(int j=0; j<height; ++j)
                {
                    float x = xRange.convert(i);
                    float y = yRange.convert(j);
                    float z = viewSignal(i, j)*scale + offset;
                    glVertex3f(x, y, -z);
                }
                glEnd();
            }
        }
        
        if(state != kDeviceHasIsochSync)
        {	// TODO draw question mark
            
        }
        
        // render current touches on top of surface
        //
        const int nt = mpModel->getFloatProperty("max_touches");
        const MLSignal& touches = mpModel->getTouchFrame();
        
        char strBuf[64] = {0};
        for(int t=0; t<nt; ++t)
        {
            int age = touches(ageColumn, t);
            if (age > 0)
            {
                float x = touches(xColumn, t) * width;
                float y = touches(yColumn, t) * height;
                float z = touches(zColumn, t);
                
                float tx = xRange.convert(x);
                float ty = yRange.convert(y);
                float tz = (z);			

    #if DEBUG
                float dt = touches(dtColumn, t);
                sprintf(strBuf, "%5.3f, %i", dt, age);
    #else
                sprintf(strBuf, "%5.3f", z);
    #endif
                float stackOffset = 0.25f;
                ty += (float)t*stackOffset;
                tz += (float)t*stackOffset;
                
// TODO fix boxes in resolution-independent way
//                drawInfoBox(Vec3(tx, ty, -tz), strBuf, t);                
// debug() << "x: " << x << ", y: " << y << ", z: " << z << ", age:" << age << "\n";
            }
        }
    }
}


void SoundplaneGridView::renderBarChart()
{
	int sensorWidth = mpModel->getWidth();
	int sensorHeight = mpModel->getHeight();
	int state = mpModel->getDeviceState();
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
	int margin = viewH / 30;
	int keyWidth = 30; // Soundplane A TODO get from tracker
	int keyHeight = 5;
    
    // put origin in lower left.
    MLGL::orthoView2(viewW, viewH);
    MLRange xRange(0, keyWidth, margin, viewW - margin);
	MLRange yRange(0, keyHeight, margin, viewH - margin);
    
	// Soundplane A
	MLRange xmRange(2, sensorWidth-2);
	xmRange.convertTo(MLRange(margin, viewW - margin));
	MLRange ymRange(0, sensorHeight);
	ymRange.convertTo(MLRange(margin, viewH - margin));
    
    const MLSignal& viewSignal = mpModel->getSignalForViewMode(mViewMode);
    
    float displayScale = mpModel->getFloatProperty("display_scale");
    float scale = displayScale;
    float offset = 0.f;
    switch(mViewMode)
    {
        case kTest2:
        default:
            offset = 0.;
            scale *= 1.;
            break;
    }
	
	// draw stuff in immediate mode.
	// TODO don't use fixed function pipeline.
	//
	Vec4 lineColor;
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 gray(0.6f, 0.6f, 0.6f, 1.f);
	Vec4 lightGray(0.9f, 0.9f, 0.9f, 1.f);
	Vec4 blue2(0.1f, 0.1f, 0.5f, 1.f);
    
	// draw lines at key grid
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
			glVertex3f(x, y, -z);
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
			glVertex3f(x, y, -z);
		}
		glEnd();
	}
	
	// draw dots
	for(int j=0; j<sensorHeight; ++j)
	{
        for(int i=0; i<sensorWidth; ++i)
        {
            float x = xmRange.convert(i + 0.5);
            float y = ymRange.convert(j + 0.5);

            float z = viewSignal(i, j)*scale + offset;
            MLGL::drawDot(Vec2(x, y), z);
            
        }
    }
}

void SoundplaneGridView::renderOpenGL()
{
    jassert (OpenGLHelpers::isContextActive());
    if (!mpModel) return;    
    glEnable(GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const Colour c = findColour(MLLookAndFeel::backgroundColor);
    OpenGLHelpers::clear (c);
    if (mViewMode == kXY)
    {
        renderXYGrid();
    }
    else if (mViewMode == kTest2)
    {
        renderBarChart();
    }
    else
    {
        renderZGrid();
    }
}


