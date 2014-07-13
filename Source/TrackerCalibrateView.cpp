
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "TrackerCalibrateView.h"

TrackerCalibrateView::TrackerCalibrateView() :
	mpModel(nullptr)
{
	setInterceptsMouseClicks (false, false);
	MLWidget::setComponent(this);
	setupGL (this);
}

TrackerCalibrateView::~TrackerCalibrateView()
{
}

void TrackerCalibrateView::setModel(SoundplaneModel* m)
{
	mpModel = m;
}

void TrackerCalibrateView::drawTextAt(float x, float y, float z, const char* ps)
{
	int len, i;

	glRasterPos3f(x, y, z);
	len = (int) strlen(ps);
	for (i = 0; i < len; i++) 
	{
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, ps[i]);
	}
}

Vec2 TrackerCalibrateView::worldToScreen(const Vec3& world) 
{
	GLint viewport[4];
	GLdouble mvmatrix[16], projmatrix[16];
	GLdouble wx, wy, wz;
		
	glGetIntegerv(GL_VIEWPORT, viewport);
	glGetDoublev(GL_MODELVIEW_MATRIX, mvmatrix);
	glGetDoublev(GL_PROJECTION_MATRIX, projmatrix);
	
	GLint result = gluProject(world[0], world[1], world[2],
		mvmatrix, projmatrix, viewport,
		&wx, &wy, &wz);
	
	if (result == GL_TRUE)
	{
		return Vec2(wx, wy);
	}
	else
	{
		return Vec2(0, 0);
	}
}

void TrackerCalibrateView::renderOpenGL()
{
	if (!mpModel) return;
	Vec2 dim = mpModel->getTrackerCalibrateDims();
	int width = dim.x();
	int height = dim.y();
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
    ScopedPointer<LowLevelGraphicsContext> glRenderer
        (createOpenGLGraphicsContext (*getGLContext(), viewW, viewH));
    
    if (glRenderer != nullptr)
    {
        Graphics g (*glRenderer);
        
        // colors
        Vec4 fillColor1(0.2f, 0.2f, 0.2f, 1.f);	
        Vec4 fillColor2(0.9f, 0.9f, 0.9f, 1.f);	
        Vec4 whiteColor(1.f, 1.f, 1.4f, 1.f);	
        Vec4 blue(0.4f, 0.4f, 1.f, 1.f);	
        Vec4 green(0.4f, 1.f, 0.4f, 1.f);	
        Vec4 doneColor(0.4f, 1.f, 0.4f, 1.f);	
    //	const Colour c = findColour(MLLookAndFeel::backgroundColor2);

        // erase
        float p = 0.1;
        glClearColor (p, p, p, 1.0f);
        
        glDisable (GL_DEPTH_TEST);
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable (GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        float myAspect = (float)viewW / (float)viewH;
        float soundplaneAspect = 4.f;
        
        // temp, sloppy
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(8.0, myAspect, 0.5, 50.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        gluLookAt(0.0, 0.0, 15.0, // eyepoint x y z
                  0.0, 0.0, -0.25, // center x y z
                  0.0, 1.0, 0.0); // up vector	
        glColor4f(1, 1, 1, 0.5);
        MLRange xRange(0, width);
        xRange.convertTo(MLRange(-myAspect, myAspect));
        MLRange yRange(0, height-1);
        float sh = myAspect/soundplaneAspect;	
        yRange.convertTo(MLRange(-sh, sh));	
            
        if (mpModel->trackerIsCalibrating())
        {	
            // draw stuff in immediate mode. TODO vertex buffers and modern GL code in general. 
            
            if(mpModel->trackerIsCollectingMap())
            {
                doneColor = blue;
            }
            else
            {
                doneColor = green;
            }

            const MLSignal& viewSignal = mpModel->getTrackerCalibrateSignal();
            // draw grid
            float elementSize = 0.03f;
            for(int j=0; j<height; ++j)
            {
                for(int i=0; i<width; ++i)
                {
                    if(mpModel->isWithinTrackerCalibrateArea(i, j))
                    {
                        float x0 = xRange.convert(i);
                        float y0 = yRange.convert(j);
                        float x1 = xRange.convert(i+1);
                        float y1 = yRange.convert(j+1);
                        {
                            glBegin(GL_QUADS);
                            float mix = viewSignal(i, j);
                            if(mix < 1.0f)
                            {
                                Vec4 elementColor = vlerp(fillColor1, fillColor2, mix);
                                glColor4fv(&elementColor[0]);
                            }
                            else
                            {
                                glColor4fv(&doneColor[0]);
                            }

                            float z = 0.f;
                            glVertex3f(x0, y0, z);
                            glVertex3f(x1, y0, z);
                            glVertex3f(x1, y1, z);
                            glVertex3f(x0, y1, z);
                            glEnd();
                        }
                        {
                            glBegin(GL_LINE_LOOP);
                            glColor4fv(&fillColor2[0]);
                            float z = 0.f;
                            glVertex3f(x0, y0, z);
                            glVertex3f(x1, y0, z);
                            glVertex3f(x1, y1, z);
                            glVertex3f(x0, y1, z);

                            glEnd();
                        }
                        
                    }
                }
            }
            
            // draw peak
            Vec3 peak = mpModel->getTrackerCalibratePeak();
            float peakX = peak.x();
            float peakY = peak.y();
            {
                glColor4fv(&whiteColor[0]);
                glBegin(GL_QUADS);
                float x = xRange.convert(peakX + 0.5f);
                float y = yRange.convert(peakY + 0.5f);
                float z = 0.f;
                float r = elementSize;
                glVertex3f(x - r, y - r, -z);
                glVertex3f(x + r, y - r, -z);
                glVertex3f(x + r, y + r, -z);
                glVertex3f(x - r, y + r, -z);
                glEnd();
            }
        }
	}
}

	
