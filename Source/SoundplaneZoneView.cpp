
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
// TODO 2d OpenGL drawing helpers
void SoundplaneZoneView::drawDot(Vec2 pos, float r)
{
	Vec4 dotColor(0.6f, 0.6f, 0.6f, 1.f);
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

void SoundplaneZoneView::renderGrid()
{
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
    MLGL::orthoView(viewW, viewH);

	int gridWidth = 30; // Soundplane A TODO get from tracker
	int gridHeight = 5;
   // int margin = viewW / 50;
	MLRange xRange(0, gridWidth, 1, viewW);
	MLRange yRange(0, gridHeight, 0, viewH - 1);
    
	// draw thin lines at key grid
	Vec4 lineColor;
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 gray(0.6f, 0.6f, 0.6f, 1.f);
	lineColor = gray;

	// horiz lines
	glColor4fv(&lineColor[0]);
	for(int j=0; j<=gridHeight; ++j)
	{
		glBegin(GL_LINE_STRIP);
		for(int i=0; i<=gridWidth; ++i)
		{
			float x = xRange.convert(i);
			float y = yRange.convert(j);
			glVertex2f(x, y);
		}
		glEnd();
	}
	// vert lines
	for(int i=0; i<=gridWidth; ++i)
	{
		glBegin(GL_LINE_STRIP);
		for(int j=0; j<=gridHeight; ++j)
		{
			float x = xRange.convert(i);
			float y = yRange.convert(j);
			glVertex2f(x, y);
		}
		glEnd();
	}
	
	// draw dots
    float r = viewH / 100.;
	for(int i=0; i<=gridWidth; ++i)
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

void SoundplaneZoneView::renderZones()
{
	if (!mpModel) return;
    int viewW = getBackingLayerWidth();
    int viewH = getBackingLayerHeight();
	// float viewAspect = (float)viewW / (float)viewH;

	int gridWidth = 30; // Soundplane A TODO get from tracker
	int gridHeight = 5;
	// float soundplaneAspect = 4.f;
    
	//int modelWidth = mpModel->getWidth();
	//int modelHeight = mpModel->getHeight();

    int lineWidth = viewW / 150;
    // int margin = lineWidth*2;
    
    MLRange xRange(0, gridWidth, 1, viewW);
	MLRange yRange(0, gridHeight, 0, viewH - 1);
//    MLRange2D xyRange(xRange, yRange);
    
    MLGL::orthoView(viewW, viewH);

	Vec4 lineColor;
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 gray(0.6f, 0.6f, 0.6f, 1.f);
	Vec4 lightGray(0.9f, 0.9f, 0.9f, 1.f);
	Vec4 blue2(0.1f, 0.1f, 0.5f, 1.f);
    
   // float strokeWidth = viewW / 100;
    const std::list<SoundplaneModel::ZonePtr>& zoneList = mpModel->getZoneList();
    std::list<SoundplaneModel::ZonePtr>::const_iterator it;
    int i = 0;
    
   
    for(it = zoneList.begin(); it != zoneList.end(); ++it)
    {
        const SoundplaneModel::Zone& z = **it;
        MLRect zr = z.mRect;
        
        // affine transforms TODO    MLRect zrd = zr.xform(gridToView);
        MLRect zrd(xRange.convert(zr.x()), yRange.convert(zr.y()), xRange.convert(zr.width()), yRange.convert(zr.height()));
        zrd.shrink(lineWidth);
        Vec4 zoneStroke(MLGL::getIndicatorColor(z.mType));
        Vec4 zoneFill(zoneStroke);
        zoneFill[3] = 0.1f;
        
        // draw box common to all kinds of zones
        glColor4fv(&zoneFill[0]);
        MLGL::fillRect(zrd);
        glColor4fv(&zoneStroke[0]);
        glLineWidth(lineWidth);
        MLGL::strokeRect(zrd);
        glLineWidth(1);
        // draw name
        MLGL::drawTextAt(zrd.left() + lineWidth, zrd.top() + zrd.height() - lineWidth, 0.f, z.mName.c_str());
        
        switch(z.mType)
        {
            case SoundplaneModel::kNoteRow:

                break;
            default:
                break;
        }
        i++;
    }
      
    /*
     types
     kNoteRow = 0,
     kControllerHorizontal = 1,
     kControllerVertical = 2,
     kControllerXY = 3,
     kToggleRow = 4
     
    ZoneType mType;
    MLRect mRect;
    int mStartNote;
    int mControllerNumber;
    int mChannel;
    std::string mName;
    */
    
}

void SoundplaneZoneView::renderOpenGL()
{
	if (!mpModel) return;
    int backW = getBackingLayerWidth();
    int backH = getBackingLayerHeight();
    ScopedPointer<LowLevelGraphicsContext> glRenderer(createOpenGLGraphicsContext (mGLContext, backW, backH));
    if (glRenderer != nullptr)
    {
        Graphics g (glRenderer);
        const Colour c = findColour(MLLookAndFeel::backgroundColor);
        OpenGLHelpers::clear (c);
        glEnable(GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        renderGrid();
        renderZones();
    }
}


	