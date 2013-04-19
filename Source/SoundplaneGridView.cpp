
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

	mpGLContext = new OpenGLContext();
	mpGLContext->setRenderer (this);
	mpGLContext->setComponentPaintingEnabled (false);
	mpGLContext->attachTo (*getComponent());
}

SoundplaneGridView::~SoundplaneGridView()
{
	mpGLContext->detach();
	delete mpGLContext;
}

// when the component creates a new internal context, this is called, and
// we'll use the opportunity to create the textures needed.
void SoundplaneGridView::newOpenGLContextCreated()
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

void SoundplaneGridView::drawInfoBox(Vec3 pos, char* text, int colorIndex)
{
	int viewW = getWidth();
	int viewH = getHeight();

	int len = strlen(text);
	clamp(len, 0, 32);
	int c = colorIndex % kGLViewNColors;
	
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
	Vec2 screen = worldToScreen(rectPos);
	Vec2 surface = worldToScreen(surfacePos);
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
	glColor4fv(kGLViewIndicatorColors + 4*c);
	glBegin(GL_LINE_LOOP);
	glVertex2f(screen[0], screen[1]);
	glVertex2f(screen[0] + w, screen[1]);
	glVertex2f(screen[0] + w, screen[1] + h);
	glVertex2f(screen[0], screen[1] + h);
	glEnd();
	
	// line down to surface
	glColor4fv(kGLViewIndicatorColors + 4*c);
	glBegin(GL_LINES);
	glVertex2f(screen[0], screen[1]);
	glVertex2f(surface[0], surface[1]);
	glEnd();
	
	// text
	glColor4fv(kGLViewIndicatorColors + 4*c);
	drawTextAt(screen[0] + margin, screen[1] + margin, 0.f, text);
	
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

void SoundplaneGridView::drawTextAt(float x, float y, float z, const char* ps)
{
	int len, i;

	glRasterPos3f(x, y, z);
	len = (int) strlen(ps);
	for (i = 0; i < len; i++) 
	{
		glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, ps[i]);
	}
}

Vec2 SoundplaneGridView::worldToScreen(const Vec3& world) 
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

// temporary, ugly
void SoundplaneGridView::drawDot(Vec2 pos)
{

	Vec4 dotColor(0.8f, 0.8f, 0.8f, 1.f);
	glColor4fv(&dotColor[0]);
	int steps = 16;
	float r = 0.04f;

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

void SoundplaneGridView::renderXYGrid()
{
	int width = 30; // Soundplane A TODO get from tracker
	int height = 5;
	int modelWidth = mpModel->getWidth();
	int modelHeight = mpModel->getHeight();
	int viewW = getWidth();
	int viewH = getHeight();
	// draw grid
	float myAspect = (float)viewW / (float)viewH;
	float soundplaneAspect = 4.f; // TEMP
	int state = mpModel->getDeviceState();
	float fMax = mpModel->getModelFloatParam("z_max");
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(8.0, myAspect, 0.125, 50.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(0.0, 0.0, 20., // eyepoint x y z
			  0.0, 0.0, 0.5, // center x y z
			  0.0, 1.0, 0.0); // up vector

	glColor4f(1, 1, 1, 0.5);
	float r = 0.95;
	MLRange xRange(0, width);
	xRange.convertTo(MLRange(-myAspect*r, myAspect*r));
	MLRange yRange(0, height);
	float sh = myAspect*r/soundplaneAspect;	
	yRange.convertTo(MLRange(-sh, sh));	
		
	// Soundplane A	
	MLRange xmRange(2, modelWidth-2);
	xmRange.convertTo(MLRange(-myAspect*r, myAspect*r));
	MLRange ymRange(0, modelHeight);
	ymRange.convertTo(MLRange(-sh, sh));	
		
	const MLSignal& calSignal = mpModel->getSignalForViewMode(kCalibrated);
	float displayScale = mpModel->getModelFloatParam("display_scale");
	
	// draw stuff in immediate mode. 
	// TODO don't use fixed function pipeline.
	//
	Vec4 lineColor;
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 gray(0.6f, 0.6f, 0.6f, 1.f);
	Vec4 lightGray(0.9f, 0.9f, 0.9f, 1.f);
	Vec4 blue2(0.1f, 0.1f, 0.5f, 1.f);

	// fill calibrated data areas
	for(int j=0; j<modelHeight; ++j)
	{
		// Soundplane A-specific
		for(int i=2; i<modelWidth - 2; ++i)
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
	for(int j=0; j<=height; ++j)
	{
		glBegin(GL_LINE_STRIP);
		for(int i=0; i<=width; ++i)
		{
			float x = xRange.convert(i);
			float y = yRange.convert(j);
			float z = 0.;
			glVertex3f(x, y, -z);
		}
		glEnd();
	}	
	// vert lines
	for(int i=0; i<=width; ++i)
	{
		glBegin(GL_LINE_STRIP);
		for(int j=0; j<=height; ++j)
		{
			float x = xRange.convert(i);
			float y = yRange.convert(j);
			float z = 0.;
			glVertex3f(x, y, -z);
		}
		glEnd();
	}
	
	// draw dots
	for(int i=0; i<=width; ++i)
	{
		float x = xRange.convert(i + 0.5);
		float y = yRange.convert(2.5);
		int k = i%12;
		if(k == 0)
		{
			float d = 0.1f;
			drawDot(Vec2(x, y - d));
			drawDot(Vec2(x, y + d));
		}
		if((k == 3)||(k == 5)||(k == 7)||(k == 9))
		{
			drawDot(Vec2(x, y));
		}
	}
	
	// render current touches on top of surface
	//
	const int nt = mpModel->getModelFloatParam("max_touches");
	const MLSignal& touches = mpModel->getTouchFrame();
	float k = 0.04f;
	for(int t=0; t<nt; ++t)
	{
		int age = touches(ageColumn, t);
		if (age > 0)
		{
			int c = t % kGLViewNColors;
			glColor4fv(kGLViewIndicatorColors + 4*c);

			float x = touches(xColumn, t) * modelWidth;
			float y = touches(yColumn, t) * modelHeight;
			Vec2 xyPos(x, y);
			Vec2 gridPos = mpModel->xyToKeyGrid(xyPos);
			
			float tx = xRange.convert(gridPos.x() + 0.5f);
			float ty = yRange.convert(gridPos.y() + 0.5f);
			float tz = 0.;
			
			glBegin(GL_QUADS);
			float x1 = tx - k;
			float y1 = ty - k;
			float x2 = tx + k;
			float y2 = ty + k;

			glVertex3f(x1, y1, tz);
			glVertex3f(x2, y1, tz);
			glVertex3f(x2, y2, tz);
			glVertex3f(x1, y2, tz);
			glEnd();
			
			
//debug() << "x: " << x << ", y: " << y << ", z: " << z << ", d: " << d << ", age:" << age << "\n";

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
			int c = touch % kGLViewNColors;
			glColor4fv(kGLViewIndicatorColors + 4*c);
			glBegin(GL_LINE_STRIP);
			int cc = ctr;
			int a = 0;
			for(int t=0; t < kSoundplaneHistorySize - 2; ++t)
			{				
				float x = touchHistory(xColumn, touch, cc);
				float y = touchHistory(yColumn, touch, cc);
				if((x > 0.) && (y > 0.))
				{
					Vec2 gridPos = mpModel->xyToKeyGrid(Vec2(x*modelWidth, y*modelHeight));
					float px = xRange.convert(gridPos.x() + 0.5f);
					float py = yRange.convert(gridPos.y() + 0.5f);
					glVertex3f(px, py, 0.);	
				}
				if(--cc < 0) { cc = kSoundplaneHistorySize - 1; }
				if(++a >= age - 2) break;
			}
			glEnd();
		}
	}
}

void SoundplaneGridView::renderOpenGL()
{
	if (!mpModel) return;
	int width = mpModel->getWidth();
	int height = mpModel->getHeight();
	int viewW = getWidth();
	int viewH = getHeight();
	
	// erase
	const Colour c = findColour(MLLookAndFeel::backgroundColor);
	float p = c.getBrightness();
	glClearColor (p, p, p, 1.0f);
	glDisable (GL_DEPTH_TEST);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable (GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	char strBuf[64] = {0};
	
	if (mViewMode == kXY)
	{
		renderXYGrid();
		return;
	}

	// draw grid
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
	
	float displayScale = mpModel->getModelFloatParam("display_scale");
	float scale = displayScale;
	float offset = 0.f;
	bool separateSurfaces = false;
	switch(mViewMode)
	{
		case kRaw:
			offset = -0.75;
			scale *= 4.;
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
		case kTest:
		default:
			scale *= 10.;
			break;
	}
	
	// draw stuff in immediate mode. TODO vertex buffers and modern GL code in general. 
	//
	Vec4 lineColor;
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
	const int nt = mpModel->getModelFloatParam("max_touches");
	const MLSignal& touches = mpModel->getTouchFrame();
	
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
			drawInfoBox(Vec3(tx, ty, -tz), strBuf, t);
			
//debug() << "x: " << x << ", y: " << y << ", z: " << z << ", d: " << d << ", age:" << age << "\n";

		}
	}
}

	
