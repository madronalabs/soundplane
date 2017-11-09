
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_GRID_VIEW__
#define __SOUNDPLANE_GRID_VIEW__

#include "MLApp/MLGL.h"
#include "SoundplaneModel.h"
#include "LookAndFeel/MLUI.h"
#include "MLJuceApp/MLWidget.h"
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
    SoundplaneGridView(MLWidget* pContainer);
    ~SoundplaneGridView();
	
	// MLModelListener implementation
	void doPropertyChangeAction(ml::Symbol , const MLProperty & );
	
	void setModel(SoundplaneModel* m);
	
private:
 
	void resizeWidget(const MLRect& b, const int);
	void doResize();
	
	void renderOpenGL();
	void setupOrthoView();
	void drawSurfaceOverlay();
	void renderXYGrid();

	void renderTouches(TouchTracker::TouchArray t);
	
	void renderZGrid();
 	
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
	
	int mCount; // TEMP
	int mMaxRawTouches;

};

#endif // __SOUNDPLANE_GRID_VIEW__

