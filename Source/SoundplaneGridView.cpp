
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneGridView.h"
#include "SoundplaneBinaryData.h"

SoundplaneGridView::SoundplaneGridView() :
	mpModel(nullptr),
	mInitialized(false),
	mResized(false),
	mSensorWidth(64),
	mSensorHeight(8),
	mCount(0),
	mMaxRawTouches(0)

{
	setInterceptsMouseClicks (false, false);
	MLWidget::setComponent(this);
    MLWidget::setupGL(this);
}

SoundplaneGridView::~SoundplaneGridView()
{
}

// MLModelListener implementation
void SoundplaneGridView::doPropertyChangeAction(MLSymbol p, const MLProperty & v)
{
}

void SoundplaneGridView::drawInfoBox(Vec3 pos, char* text, int colorIndex)
{
	int viewScale = getRenderingScale();
	int viewW = getBackingLayerWidth();
	int viewH = getBackingLayerHeight();
	
	int len = strlen(text);
	clamp(len, 0, 32);
	
	float margin = 5*viewScale;
	float charWidth = 10*viewScale; 
	float charHeight = 10*viewScale;
	float w = len * charWidth + margin*2;
	float h = charHeight + margin*2;
//	float shadow = 2; // TODO
	
	const float heightAboveSurface = 0.4f;
	Vec3 rectPos = pos;
	rectPos[2] = heightAboveSurface;
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
    MLGL::drawTextAt(screen[0] + margin, screen[1] + margin, 0.f, 0.1f, viewScale, text);
	
	// outline

	// pop ortho projection		
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
}


void SoundplaneGridView::setupOrthoView()
{
	int viewW = getBackingLayerWidth();
	int viewH = getBackingLayerHeight();	
	if(mBackingLayerSize != Vec2(viewW, viewH))
	{
		mBackingLayerSize = Vec2(viewW, viewH);
		doResize();
	}
	MLGL::orthoView(viewW, viewH);
}

void SoundplaneGridView::drawSurfaceOverlay()
{

	float dotSize = fabs(mKeyRangeY(0.08f) - mKeyRangeY(0.f));
	
	setupOrthoView();
	
	// draw stuff in immediate mode. 
	// TODO don't use fixed function pipeline.
	//
	Vec4 lineColor;
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 0.5f);
	Vec4 gray(0.2f, 0.2f, 0.2f, 0.5f);
	Vec4 lightGray(0.9f, 0.9f, 0.9f, 1.f);
	Vec4 blue2(0.1f, 0.1f, 0.5f, 1.f);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glDisable(GL_LINE_SMOOTH);
	glLineWidth(1.0*mViewScale);
	
	// draw lines at key grid
	lineColor = gray;

	// horiz lines
	glColor4fv(&lineColor[0]);
	for(int j=0; j<=mKeyHeight; ++j)
	{
		glBegin(GL_LINE_STRIP);
		for(int i=0; i<=mKeyWidth; ++i)
		{
			// TODO affine 2D
			float x = mKeyRangeX.convert(i);
			float y = mKeyRangeY.convert(j);
			float z = 0.;
			glVertex3f(x, y, -z);
		}
		glEnd();
	}	
	
	// vert lines
	for(int i=0; i<=mKeyWidth; ++i)
	{
		glBegin(GL_LINE_STRIP);
		for(int j=0; j<=mKeyHeight; ++j)
		{
			float x = mKeyRangeX.convert(i);
			float y = mKeyRangeY.convert(j);
			float z = 0.;
			glVertex3f(x, y, -z);
		}
		glEnd();
	}
	
	// draw fret dots
	for(int i=0; i<=mKeyWidth; ++i)
	{
		float x = mKeyRangeX.convert(i + 0.5);
		float y = mKeyRangeY.convert(2.5);
		int k = i%12;
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
}


void SoundplaneGridView::setModel(SoundplaneModel* m)
{
	mpModel = m;
}

void SoundplaneGridView::renderXYGrid()
{
	const int kTouchHistorySize = 500;
	float fMax = mpModel->getFloatProperty("z_max");
	
	const MLSignal calSignal = mpModel->getCalibratedSignal();
	
	TouchTracker::SensorBitsArray thresholds = mpModel->getThresholdBits();
	
	setupOrthoView();
	float dotSize = fabs(mKeyRangeY(0.08f) - mKeyRangeY(0.f));
	float displayScale = mpModel->getFloatProperty("display_scale");
	
	// draw stuff in immediate mode. 
	// TODO don't use fixed function pipeline.
	//
	Vec4 lineColor;
	Vec4 darkBlue(0.1f, 0.1f, 0.5f, 1.f);
	Vec4 gray(0.6f, 0.6f, 0.6f, 1.f);
	Vec4 lightGray(0.9f, 0.9f, 0.9f, 1.f);
	Vec4 green(0.3f, 0.9f, 0.3f, 1.f);
	Vec4 blue2(0.1f, 0.1f, 0.5f, 1.f);
	
	// calSignal is not right sometimes
	
	// fill calibrated data areas
	for(int j=0; j<mSensorHeight; ++j)
	{
		// Soundplane A-specific
		for(int i=mLeftSensor; i<mRightSensor; ++i)
		{
			float mix = (calSignal)(i, j) / fMax;
			mix *= displayScale*2.f;
			mix = clamp(mix, 0.f, 1.f);
			Vec4 dataColor = vlerp(gray, lightGray, mix);
			
			// mark sensor junction if over threshold
			bool t = thresholds[j*kSensorCols + i];			
			if(t)
			{
				dataColor[0]*= 0.5f;
				dataColor[2]*= 0.5f;
			}
			
			glColor4fv(&dataColor[0]);
			
			glBegin(GL_QUADS);
			float x1 = mSensorRangeX.convert(i - 0.5f);
			float y1 = mSensorRangeY.convert(j - 0.5f);
			float x2 = mSensorRangeX.convert(i + 0.5f);
			float y2 = mSensorRangeY.convert(j + 0.5f);
			float z = 0.;
			glVertex3f(x1, y1, -z);
			glVertex3f(x2, y1, -z);
			glVertex3f(x2, y2, -z);
			glVertex3f(x1, y2, -z);
			glEnd();
		}
	}	
	
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glDisable(GL_LINE_SMOOTH);
	glLineWidth(mViewScale);
		
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
			
			Vec2 gridPos(x, y);
			float tx = mKeyRangeX.convert(gridPos.x());
			float ty = mKeyRangeY.convert(gridPos.y());
			float tz = touches(zColumn, t);
			
			Vec4 dataColor(MLGL::getIndicatorColor(t));
			dataColor[3] = 0.75;
			glColor4fv(&dataColor[0]);
			MLGL::drawDot(Vec2(tx, ty), dotSize*10.0*tz);
		}
	}
	
	// render touch position history xy lines
	const MLSignal& touchHistory = mpModel->getTouchHistory();
	
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glEnable(GL_LINE_SMOOTH);
	glLineWidth(1.0*mViewScale);
	
	int ctr = mpModel->getHistoryCtr();
	for(int touch=0; touch<nt; ++touch)
	{
//		int currentAge = touches(ageColumn, touch);
		
//		debug() << "age:" << currentAge << "\n";
//		if (age > 0)
		{
			glColor4fv(MLGL::getIndicatorColor(touch));
			glBegin(GL_LINE_STRIP);
			
		//	int a = 0;
			const int kDrawHistorySize = 500;
			int cc = ctr;
			
//			int totalAge = 0;
			
			for(int t=0; t < kDrawHistorySize; ++t)
			{				
				float x = touchHistory(xColumn, touch, cc);
				float y = touchHistory(yColumn, touch, cc);
				int age = touchHistory(ageColumn, touch, cc);

				if((age > 0))
				{
					Vec2 gridPos(x, y);
					float px = mKeyRangeX.convert(gridPos.x());
					float py = mKeyRangeY.convert(gridPos.y());
					glVertex2f(px, py);	
				}

				if(--cc < 0) { cc = kSoundplaneHistorySize - 1; }
				
//				debug() << age << " ";
//				if(age < 0) break;
//				if(++a >= age - 2) break;
				
//				totalAge++;
			}
			
//			debug() << "\ntotal age: " << totalAge << "\n\n";
			
			glEnd();
		}
	}
}


void SoundplaneGridView::renderPingsHoriz()
{
	setupOrthoView();
 
	// draw stuff in immediate mode. 
	// TODO don't use fixed function pipeline.
	//
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 darkRed(0.6f, 0.3f, 0.3f, 1.f);
	Vec4 white(1.f, 1.f, 1.f, 1.f);
	float ph = 0.4;
	const float kGraphAmp = 4.0f;
	float displayScale = mpModel->getFloatProperty("display_scale")*10.f;
	
	
	// draw calibrated data
	
	const MLSignal viewSignal = mpModel->getCalibratedSignal();
	
	// draw line graph
	glLineWidth(mViewScale);
	
	for(int j=0; j<mSensorHeight; ++j)
	{
		float y1 = mSensorRangeY.convert(j - ph);   
		float y2 = mSensorRangeY.convert(j + ph);   
		
		glBegin(GL_LINE_STRIP);
		
		for(int i=0; i<mSensorWidth; ++i)
		{
			float x = mSensorRangeX.convert(i);
			
			float amp = viewSignal(i, j)*displayScale*kGraphAmp;
			float yAmp = lerp(y1, y2, amp);
			
			glColor4fv(&darkRed[0]);
			
			glVertex2f(x, yAmp);
		}
		glEnd();
	}
	
	// draw horiz pings as vert lines
	auto pings = mpModel->getPingsHorizRaw();
	int j = 0;
	for(auto row : pings.data)
	{
		for(auto p : row)
		{
			if(!p) break; // each array of spans is null-terminated
			
			float x1 = mSensorRangeX.convert(p.x());
			float x2 = x1;
			float y1 = mSensorRangeY.convert(j - ph);   
			float y2 = mSensorRangeY.convert(j + ph);   
			
			Vec4 dotColor = darkBlue;
			dotColor[3] = 0.5f;
			glColor4fv(&dotColor[0]);
			
			MLGL::drawLine(x1, y1, x2, y2, 4.0f*mViewScale);
		}
		j++;
	}       
 
}


void SoundplaneGridView::renderPingsVert()
{
	setupOrthoView();
	
	// draw stuff in immediate mode. 
	// TODO don't use fixed function pipeline.
	//
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 darkRed(0.6f, 0.3f, 0.3f, 1.f);
	Vec4 white(1.f, 1.f, 1.f, 1.f);
	float ph = 0.4;
	const float kGraphAmp = 4.0f;
	float displayScale = mpModel->getFloatProperty("display_scale")*10.f;
	
	
	// draw calibrated data
	
	const MLSignal viewSignal = mpModel->getCalibratedSignal();
	
	// draw line graph
	glLineWidth(mViewScale);
	for(int i=0; i<mSensorWidth; ++i)
	{
		float x = mSensorRangeX.convert(i);
		float x1 = mSensorRangeX.convert(i - ph);   
		float x2 = mSensorRangeX.convert(i + ph);   
		
		glBegin(GL_LINE_STRIP);
		
		for(int j=0; j<mSensorHeight; ++j)
		{
			float y = mSensorRangeY.convert(j);
			
			float amp = viewSignal(i, j)*displayScale*kGraphAmp;
			float xAmp = lerp(x1, x2, amp);
			glColor4fv(&darkRed[0]);
			glVertex2f(xAmp, y);
		}
		glEnd();
	}
	
	// draw vert pings as horiz lines
	auto pings = mpModel->getPingsVertRaw();
	int j = 0;
	for(auto col : pings.data)
	{
		for(auto p : col)
		{
			if(!p) break; // each array of spans is null-terminated
			
			float y1 = mSensorRangeY.convert(p.x());
			float y2 = y1;
			float x1 = mSensorRangeX.convert(j - ph);   
			float x2 = mSensorRangeX.convert(j + ph);   
			
			Vec4 dotColor = darkRed;
			dotColor[3] = 0.5f;
			glColor4fv(&dotColor[0]);
			
			MLGL::drawLine(x1, y1, x2, y2, 4.0f*mViewScale);
		}
		j++;
	}       
}


void SoundplaneGridView::renderPings()
{
	setupOrthoView();
	
	// draw stuff in immediate mode. 
	// TODO don't use fixed function pipeline.
	//
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 darkRed(0.6f, 0.3f, 0.3f, 1.f);
	Vec4 white(1.f, 1.f, 1.f, 1.f);
	
	float displayScale = mpModel->getFloatProperty("display_scale");
	glLineWidth(4.0*mViewScale);

	// draw horiz pings
	float kDotSize = 200.f;
	float dotSize = kDotSize*fabs(mKeyRangeY(0.10f) - mKeyRangeY(0.f));
	auto pings = mpModel->getPingsHorizRaw();
	int j = 0;
	for(auto row : pings.data)
	{
		for(auto p : row)
		{
			if(!p) break; // each array of spans is null-terminated
			
			// pings are in sensor coordinates
			float x = mSensorRangeX.convert(p.x());
			float y = mSensorRangeY.convert(j);
			float z = p.y();
			
			Vec4 dotColor = darkBlue;
			dotColor[3] = 0.5f;
			glColor4fv(&dotColor[0]);
			
			// draw dot on surface
			MLGL::drawDot(Vec2(x, y), z*dotSize*displayScale);
		}
		j++;
	}		
	
	// draw vert pings
	int colInt = 0;
	auto pingsVert = mpModel->getPingsVertRaw();
	for(auto col : pingsVert.data)
	{
		for(auto p : col)
		{
			if(!p) break; // each array of spans is null-terminated
			float x = mSensorRangeX.convert(colInt);
			float y = mSensorRangeY.convert(p.x());
			float z = p.y();
			
			Vec4 dotColor = darkRed;
			dotColor[3] = 0.5f;
			glColor4fv(&dotColor[0]);
			MLGL::drawDot(Vec2(x, y), z*dotSize*displayScale);
		}
		colInt++;
	}
}


void SoundplaneGridView::renderKeyStates()
{
	setupOrthoView();
	
	// draw stuff in immediate mode. 
	// TODO don't use fixed function pipeline.
	//
	Vec4 darkGreen(0.0f, 0.2f, 0.0f, 1.f);
	Vec4 lightGreen(0.2f, 1.0f, 0.2f, 1.f);
	Vec4 darkBlue(0.3f, 0.3f, 0.5f, 1.f);
	Vec4 darkRed(0.6f, 0.3f, 0.3f, 1.f);
	Vec4 white(1.f, 1.f, 1.f, 1.f);

	float displayScale = mpModel->getFloatProperty("display_scale");
	
	MLRange zRange(0.f, 0.01f, 0., 1.);
	
	
	auto keyStates = mpModel->getKeyStates();
	int j = 0;
	for (auto keyRow : keyStates.data)
	{
		int i = 0;
		for(auto key : keyRow)
		{
			// key states after filtering have x, y, x variance, y variance
			float x = key.x();
			float y = key.y();// * 20.f * displayScale;
			float z = key.z();
			float w = key.w();
						
			x = clamp(x, 0.f, 1.f);
			y = clamp(y, 0.f, 1.f);

				// get screen coords
			float sx0 = mKeyRangeX.convert(i);
			float sx1 = mKeyRangeX.convert(i + 1.f);

			float sy0 = mKeyRangeY.convert(j);
			float sy1 = mKeyRangeY.convert(j + 1.f);

			float sx = mKeyRangeX.convert(i + x);
			float sy = mKeyRangeY.convert(j + y);
			

			Vec4 varianceColor = vlerp(darkGreen, lightGreen, zRange.convertAndClip(z));
			glColor4fv(&varianceColor[0]);		
			MLGL::drawLine(sx, sy0, sx, sy1, 2.0f*mViewScale);
			MLGL::drawLine(sx0, sy, sx1, sy, 2.0f*mViewScale);
			
			i++;
		}
		j++;
	}
	
	
	
	/*
	 // draw fret dots
	 for(int i=0; i<=mKeyWidth; ++i)
	 {
		float x = mKeyRangeX.convert(i + 0.5);
		float y = mKeyRangeY.convert(2.5);
		int k = i%12;
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
*/

}



void SoundplaneGridView::renderTouches(std::array<Vec4, TouchTracker::kMaxTouches> newTouches)
{
	if (!mpModel) return;
		
	setupOrthoView();

	float dotSize = 100.f*fabs(mKeyRangeY(0.1f) - mKeyRangeY(0.f));
	float displayScale = mpModel->getFloatProperty("display_scale");
		
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glDisable(GL_LINE_SMOOTH);
	glLineWidth(1.0*mViewScale);

	// draw intersections colored by group
	auto xs = newTouches;  
	int rowInt = 0;
	int i = 0;
	for(auto inx : xs)
	{
		if(!inx) break;
		
		float x = mKeyRangeX.convert(inx.x());
		float y = mKeyRangeY.convert(inx.y());
		float z = inx.z();
		
		Vec4 dotColor(MLGL::getIndicatorColor(i));
		dotColor[3] = 0.5f;
		glColor4fv(&dotColor[0]);
		
		// draw dot on surface
		Vec2 pos(x, y);
		MLGL::drawDot(pos, z*dotSize*displayScale);
		
		// cross in center
		float k = dotSize*0.01f;
		dotColor[3] = 1.0f;
		glColor4fv(&dotColor[0]);
		MLGL::drawLine(x - k, y, x + k, y, 2.0f*mViewScale);
		MLGL::drawLine(x, y - k, x, y + k, 2.0f*mViewScale);
		
		rowInt++;
		i++;
	}
}


void SoundplaneGridView::renderRawTouches()
{
	renderTouches(mpModel->getRawTouches());
}

void SoundplaneGridView::renderFilteredTouches()
{
	renderTouches(mpModel->getTouches());
}

void SoundplaneGridView::renderZGrid()
{
	if (!mpModel) return;
	bool zeroClip = false;
	
	float myAspect = (float)mViewWidth / (float)mViewHeight;
	float soundplaneAspect = 4.f; // TEMP
	int state = mpModel->getDeviceState();
	
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(8.0, myAspect, 0.5, 50.0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	gluLookAt(0.0, -14.0, 6., // eyepoint x y z
			  0.0, 0.0, -0.25, // center x y z
			  0.0, 1.0, 0.0); // up vector
	
	glColor4f(1, 1, 1, 0.5);
	MLRange xSensorRange(0, mSensorWidth-1);
	float r = 0.95;
	xSensorRange.convertTo(MLRange(-myAspect*r, myAspect*r));
	MLRange ySensorRange(0, mSensorHeight-1);
	float sh = myAspect*r/soundplaneAspect;
	ySensorRange.convertTo(MLRange(-sh, sh));
	
	const std::string& viewMode = getStringProperty("viewmode");
	MLSignal viewSignal;
	if(viewMode == "raw data")
	{
		viewSignal = mpModel->getRawSignal();
	}
	else if(viewMode == "calibrated")
	{
		viewSignal = mpModel->getCalibratedSignal();
	}
	else
	{
		viewSignal = mpModel->getSmoothedSignal();
	}
	// should be one compare for Vec2 signal dims
	if((viewSignal.getHeight() != mSensorHeight) || (viewSignal.getWidth() != mSensorWidth)) return;
	
	float displayScale = mpModel->getFloatProperty("display_scale");
	float gridScale = displayScale * 100.f;
	
	float preOffset = 0.f;
	bool separateSurfaces = false;
	int leftEdge = 0;
	int rightEdge = mSensorWidth;
	
	if(viewMode == "raw data")
	{
		preOffset = -0.1;
		separateSurfaces = true;
		gridScale *= 0.1f;
	}
	
	// draw stuff in immediate mode. TODO vertex buffers and modern GL code in general.
	//
	Vec4 lineColor;
	Vec4 white(1.f, 1.f, 1.f, 1.f);
	Vec4 darkBlue(0.f, 0.f, 0.4f, 1.f);
	Vec4 green(0.f, 0.5f, 0.1f, 1.f);
	Vec4 blue(0.1f, 0.1f, 0.9f, 1.f);
	Vec4 purple(0.7f, 0.2f, 0.7f, 0.5f);
	
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_BLEND);
	glDisable(GL_LINE_SMOOTH);
	glLineWidth(1.0*mViewScale);
	
	if (separateSurfaces)
	{
		// draw lines
		for(int i=0; i<mSensorWidth; ++i)
		{
			// alternate colors every flex circuit
			lineColor = (i/16)&1 ? darkBlue : blue;
			if(state != kDeviceHasIsochSync)
			{
// MLTEST				lineColor[3] = 0.1f;
			}
			glColor4fv(&lineColor[0]);
			
			// vert
			glBegin(GL_LINE_STRIP);
			for(int j=0; j<mSensorHeight; ++j)
			{
				float x = xSensorRange.convert(i);
				float y = ySensorRange.convert(j);
				float z = viewSignal(i, j);
				if(zeroClip) { z = max(z, 0.f); }
				float zMean = (z + preOffset)*gridScale;
				glVertex3f(x, y, -zMean);
			}
			glEnd();
			
			// horiz
			if (i%16 != 15)
			{
				glBegin(GL_LINES);
				for(int j=0; j<mSensorHeight; ++j)
				{
					float x1 = xSensorRange.convert(i);
					float y1 = ySensorRange.convert(j);
					float z = viewSignal(i, j);
					if(zeroClip) { z = max(z, 0.f); }
					float z1 = (z + preOffset)*gridScale;
					glVertex3f(x1, y1, -z1);
					
					float x2 = xSensorRange.convert(i + 1);
					float y2 = ySensorRange.convert(j);
					z = viewSignal(i + 1, j);
					if(zeroClip) { z = max(z, 0.f); }
					float z2 = (z + preOffset)*gridScale;
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
		for(int j=0; j<mSensorHeight; ++j)
		{
			glBegin(GL_LINE_STRIP);
			for(int i=leftEdge; i<rightEdge; ++i)
			{
				float x = xSensorRange.convert(i);
				float y = ySensorRange.convert(j);
				float z0 = viewSignal(i, j);
				if(zeroClip) { z0 = max(z0, 0.f); }
				float z = (z0 + preOffset)*gridScale;
				glVertex3f(x, y, -z);
			}
			glEnd();
		}
		// vert lines
		for(int i=leftEdge; i<rightEdge; ++i)
		{
			glBegin(GL_LINE_STRIP);
			for(int j=0; j<mSensorHeight; ++j)
			{
				float x = xSensorRange.convert(i);
				float y = ySensorRange.convert(j);
				float z0 = viewSignal(i, j);
				if(zeroClip) { z0 = max(z0, 0.f); }
				float z = (z0 + preOffset)*gridScale;
				glVertex3f(x, y, -z);
			}
			glEnd();
		}
	}
	
	float dotSize = fabs(mKeyRangeY(0.08f) - mKeyRangeY(0.f));
	const int nt = mpModel->getFloatProperty("max_touches");
	const MLSignal& touches = mpModel->getTouchFrame();
	char strBuf[64] = {0};
	for(int t=0; t<nt; ++t)
	{
		int age = touches(ageColumn, t);
		if (age > 0)
		{
			float x = touches(xColumn, t);
			float y = touches(yColumn, t);
			
			Vec2 xyPos(x, y);
			Vec2 gridPos = mpModel->xyToKeyGrid(xyPos);
			float tx = mKeyRangeX.convert(gridPos.x());
			float ty = mKeyRangeY.convert(gridPos.y());
			float tz = touches(zColumn, t);
			
			Vec4 dataColor(MLGL::getIndicatorColor(t));
			dataColor[3] = 0.75;
			glColor4fv(&dataColor[0]);
			
			// draw dot on surface
			MLGL::drawDot(Vec2(tx, ty), dotSize*10.0*tz);
			sprintf(strBuf, "%5.3f", tz);			
			drawInfoBox(Vec3(tx, ty, 0.), strBuf, t);                
			
		}
	}
}

void SoundplaneGridView::resizeWidget(const MLRect& b, const int u)
{
	MLWidget::resizeWidget(b, u);
	doResize();
}

void SoundplaneGridView::doResize()
{
	mKeyWidth = 30;
	mKeyHeight = 5;
	
	mSensorWidth = mpModel->getWidth();
	mSensorHeight = mpModel->getHeight();
	
	// Soundplane A
	mLeftSensor = 2;
	mRightSensor = mSensorWidth - 2;
	
	mViewWidth = getBackingLayerWidth();
	mViewHeight = getBackingLayerHeight();
	mViewScale = getRenderingScale();
	int margin = mViewHeight / 30;

	// Soundplane A TODO get from model
	mKeyRect = MLRect(0, 0, mKeyWidth, mKeyHeight);
	mSensorRect = MLRect(1.5, -0.5, 60., 8.);
	
	// key drawing scales. an integer key position corresponds to the left edge of a key on the surface. 
	mKeyRangeX = MLRange (mKeyRect.left(), mKeyRect.left() + mKeyRect.width(), margin, mViewWidth - margin);
	mKeyRangeY = MLRange (mKeyRect.top(), mKeyRect.top() + mKeyRect.height(), margin, mViewHeight - margin);

	// sensors. an integer position is the middle of a sensor.
	mSensorRangeX = MLRange (mSensorRect.left(), mSensorRect.left() + mSensorRect.width(), margin, mViewWidth - margin);
	mSensorRangeY = MLRange (mSensorRect.top(), mSensorRect.top() + mSensorRect.height(), margin, mViewHeight - margin);

	mResized = true;
	repaint();
}

void SoundplaneGridView::renderOpenGL()
{
    jassert (OpenGLHelpers::isContextActive());
    if(!mpModel) return;    
	if(!mResized) return;
	
    glEnable(GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const Colour c = findColour(MLLookAndFeel::backgroundColor);	
	OpenGLHelpers::clear (c);
	const std::string& viewMode = getStringProperty("viewmode");
	
	if (viewMode == "xy")
	{
		renderXYGrid();
		drawSurfaceOverlay();
	}
	else if (viewMode == "pings")
	{
		renderPings();
		drawSurfaceOverlay();
	}
	else if (viewMode == "pings horiz")
	{
		renderPingsHoriz();
		drawSurfaceOverlay();
	}
	else if (viewMode == "pings vert")
	{
		renderPingsVert();
		drawSurfaceOverlay();
	}
	else if (viewMode == "key states")
	{
		renderKeyStates();
		drawSurfaceOverlay();
	}
	else if (viewMode == "raw touches")
	{
		renderRawTouches();
		drawSurfaceOverlay();
	}
	else if (viewMode == "touches")
	{
		renderFilteredTouches();
		drawSurfaceOverlay();
	}
	else // raw, calibrated or smoothed
	{
		renderZGrid();
	}

	
	if(mCount++ > 30) { mCount = 0; } // TEMP
}
