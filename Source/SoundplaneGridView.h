
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_GRID_VIEW__
#define __SOUNDPLANE_GRID_VIEW__

#include "MLGL.h"
#include "SoundplaneModel.h"
#include "MLUI.h"
#include "MLWidget.h"
#include "MLLookAndFeel.h"

#ifndef GL_BGRA_EXT
 #define GL_BGRA_EXT 0x80e1
#endif

// This is the view of the Soundplane surface used on the "Touches" page. It can be a perspective view of the
// surface or an xy view from directly overhead. 

class SoundplaneGridView  : 
	public Component,
	public MLWidget
{
public:
    SoundplaneGridView();
    ~SoundplaneGridView();
	
	// MLModelListener implementation
	void doPropertyChangeAction(MLSymbol , const MLProperty & );
	
	void setModel(SoundplaneModel* m);
	
private:
 
	void resizeWidget(const MLRect& b, const int);
	void doResize();
	
	void renderOpenGL();
	void setupOrthoView();
	void drawSurfaceOverlay();
	void renderXYGrid();
	void renderRegions();
	void renderSpansHoriz();
	void renderSpansVert();
	void renderPings();
	void renderLineSegments();
	void renderGradient();
	void renderIntersections();
	void renderTouches();
	void renderZGrid();
	void renderBarChartRaw();
 	
	Vec2 worldToScreen(const Vec3& world); 
	
	void drawInfoBox(Vec3 pos, char* text, int colorIndex);

	SoundplaneModel* mpModel;
	bool mInitialized;
	bool mResized;
	
	Vec2 mBackingLayerSize; // a hack
	MLRange mKeyRangeX, mKeyRangeY;
	MLRange mSensorRangeX, mSensorRangeY;
	MLRect mKeyRect, mSensorRect;
	int mViewWidth, mViewHeight;
	float mViewScale;
	int mSensorHeight, mSensorWidth;
	int mLeftSensor, mRightSensor;
	int mKeyHeight, mKeyWidth;

};

#endif // __SOUNDPLANE_GRID_VIEW__

