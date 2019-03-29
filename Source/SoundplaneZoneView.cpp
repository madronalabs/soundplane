
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneZoneView.h"

SoundplaneZoneView::SoundplaneZoneView(MLWidget* pContainer) :
MLWidget (pContainer),
mpModel(nullptr)
{
	setInterceptsMouseClicks (false, false);
	MLWidget::setComponent(this);
	setupGL();
}

SoundplaneZoneView::~SoundplaneZoneView()
{
}

void SoundplaneZoneView::setModel(SoundplaneModel* m)
{
	mpModel = m;
}

void SoundplaneZoneView::mouseDrag (const MouseEvent& e)
{
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
	MLRange yRange(0, gridHeight, 1, viewH);
	
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
	float r = viewH / 80.;
	for(int i=0; i<=gridWidth; ++i)
	{
		float x = xRange.convert(i + 0.5);
		float y = yRange.convert(2.5);
		int k = i%12;
		if(k == 0)
		{
			float d = viewH / 50;
			Vec4 dotColor(0.6f, 0.6f, 0.6f, 1.f);
			glColor4fv(&dotColor[0]);
			MLGL::drawDot(Vec2(x, y - d), r);
			MLGL::drawDot(Vec2(x, y + d), r);
		}
		if((k == 3)||(k == 5)||(k == 7)||(k == 9))
		{
			Vec4 dotColor(0.6f, 0.6f, 0.6f, 1.f);
			glColor4fv(&dotColor[0]);
			MLGL::drawDot(Vec2(x, y), r);
		}
	}
}

void SoundplaneZoneView::renderZones()
{
	if (!mpModel) return;
	
	int viewW = getBackingLayerWidth();
	int viewH = getBackingLayerHeight();
	int viewScale = getRenderingScale();
	// float viewAspect = (float)viewW / (float)viewH;
	
	int gridWidth = 30; // Soundplane A TODO get from tracker
	int gridHeight = 5;
	int lineWidth = viewW / 200;
	int thinLineWidth = viewW / 400;
	// int margin = lineWidth*2;
	
	// put origin in lower left.
	MLGL::orthoView(viewW, viewH);
	MLRange xRange(0, gridWidth, 1, viewW);
	MLRange yRange(0, gridHeight, 1, viewH);
	
	Vec4 lineColor;
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 gray(0.6f, 0.6f, 0.6f, 1.f);
	Vec4 lightGray(0.9f, 0.9f, 0.9f, 1.f);
	Vec4 blue2(0.1f, 0.1f, 0.5f, 1.f);
	float smallDotSize = xRange(1.f);
	
	// float strokeWidth = viewW / 100;
	
	for(auto it = mpModel->getZonesBegin(); it != mpModel->getZonesEnd(); ++it)
	{
		const Zone& zone = *it;
		
		MLRect zr = zone.getBounds();
		int offset = zone.getOffset();
		
		MLRange unityToKeyX(0.f, 1.f, zr.left(), zr.right());
		MLRange unityToKeyY(0.f, 1.f, zr.top(), zr.bottom());
		
		// affine transforms TODO for better syntax: MLRect zrd = zr.xform(gridToView);
		
		MLRect zoneRectInView(xRange.convert(zr.x()), yRange.convert(zr.y()), xRange.convert(zr.width()), yRange.convert(zr.height()));
		zoneRectInView.shrink(lineWidth);
		
		// color idx = port offset.
		Vec4 zoneStroke(MLGL::getIndicatorColor(offset));
		Vec4 zoneFill(zoneStroke);
		zoneFill[3] = 0.1f;
		Vec4 activeFill(zoneStroke);
		activeFill[3] = 0.25f;
		Vec4 dotFill(zoneStroke);
		dotFill[3] = 0.5f;
		
		// draw box common to all kinds of zones
		glColor4fv(&zoneFill[0]);
		MLGL::fillRect(zoneRectInView);
		glColor4fv(&zoneStroke[0]);
		glLineWidth(lineWidth);
		MLGL::strokeRect(zoneRectInView, 2.0f*viewScale);
		glLineWidth(1);
		
		// draw name
		// all these rect calculations read upside-down here because view origin is at bottom
		TextFragment nameFrag = zone.getName();
		MLGL::drawTextAt(zoneRectInView.left() + lineWidth, zoneRectInView.top() + lineWidth, 0.f, 0.1f, viewScale, nameFrag.getText());
		
		// draw zone-specific things
		float x, y;
		int toggle;
		const ZoneMessage& c = zone.getController();
		Symbol t = zone.getType();
		if(t == "note_row")
		{
				for(int i = 0; i < kMaxTouches; ++i)
				{
					Touch t = zone.touchToKeyPos(zone.getTouch(i));
					if(touchIsActive(t))
					{
						glColor4fv(&dotFill[0]);
						float dx = xRange(t.x);
						float dy = yRange(t.y);
						float dz = t.z;
						MLGL::drawDot(Vec2(dx, dy), dz*smallDotSize);
					}
				}
		}
		else if(t == "x")
		{
				x = xRange(unityToKeyX(c.x));
				glColor4fv(&zoneStroke[0]);
				glLineWidth(thinLineWidth);
				MLGL::strokeRect(MLRect(x, zoneRectInView.top(), 0., zoneRectInView.height()), viewScale);
				glColor4fv(&activeFill[0]);
				MLGL::fillRect(MLRect(zoneRectInView.left(), zoneRectInView.top(), x - zoneRectInView.left(), zoneRectInView.height()));
		}
		else if(t == "y")
		{
				y = yRange(unityToKeyY(c.y));
				glColor4fv(&zoneStroke[0]);
				glLineWidth(thinLineWidth);
				MLGL::strokeRect(MLRect(zoneRectInView.left(), y, zoneRectInView.width(), 0.), viewScale);
				glColor4fv(&activeFill[0]);
				MLGL::fillRect(MLRect(zoneRectInView.left(), zoneRectInView.top(), zoneRectInView.width(), y - zoneRectInView.top()));
		}
		else if(t == "xy")
		{
				x = xRange(unityToKeyX(c.x));
				y = yRange(unityToKeyY(c.y));
				glColor4fv(&zoneStroke[0]);
				glLineWidth(thinLineWidth);
				// cross-hairs centered on dot
				MLGL::strokeRect(MLRect(x, zoneRectInView.top(), 0., zoneRectInView.height()), viewScale);
				MLGL::strokeRect(MLRect(zoneRectInView.left(), y, zoneRectInView.width(), 0.), viewScale);
				glColor4fv(&dotFill[0]);
				MLGL::drawDot(Vec2(x, y), smallDotSize*0.25f);
		}
		else if(t == "z")
		{
				y = yRange(unityToKeyY(c.z)); // look at z value over y range
				glColor4fv(&zoneStroke[0]);
				glLineWidth(thinLineWidth);
				MLGL::strokeRect(MLRect(zoneRectInView.left(), y, zoneRectInView.width(), 0.), viewScale);
				glColor4fv(&activeFill[0]);
				MLGL::fillRect(MLRect(zoneRectInView.left(), zoneRectInView.top(), zoneRectInView.width(), y - zoneRectInView.top()));
		}
		else if(t == "toggle")
		{
				toggle = c.x; // toggle is controller x
				glColor4fv(&zoneStroke[0]);
				glLineWidth(thinLineWidth);
				if(toggle)
				{
					MLRect toggleFill = zoneRectInView;
					Vec2 zoneCenter = zoneRectInView.getCenter();
					glColor4fv(&activeFill[0]);
					MLGL::fillRect(zoneRectInView);
					glColor4fv(&dotFill[0]);
					MLGL::drawDot(zoneCenter, smallDotSize*0.25f);
				}
		}
	}
}

void SoundplaneZoneView::renderOpenGL()
{
	if (!mpModel) return;
	if (!mpGLContext) return;
	if(!mpGLContext->isAttached()) return;
	{
		const Colour c = findColour(MLLookAndFeel::backgroundColor);
		OpenGLHelpers::clear (c);
		glEnable(GL_BLEND);
		glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		renderGrid();
		renderZones();
	}
}

