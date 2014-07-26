
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_VIEW_H__
#define __SOUNDPLANE_VIEW_H__

#include "JuceHeader.h"
#include "SoundplaneGridView.h"
#include "SoundplaneTouchGraphView.h"
#include "SoundplaneZoneView.h"
#include "TrackerCalibrateView.h"
#include "SoundplaneModel.h"
#include "MLButton.h"
#include "MLDrawableButton.h"
#include "MLTextButton.h"
#include "MLMultiSlider.h"
#include "MLMultiButton.h"
#include "MLEnvelope.h"
#include "MLProgressBar.h"
#include "MLGraph.h"
#include "MLVector.h"
#include "MLAppView.h"
#include "MLPageView.h"
#include "MLResponder.h"
#include "SoundplaneBinaryData.h"

const int kViewGridUnitsX = 15;
const int kViewGridUnitsY = 10;

// --------------------------------------------------------------------------------
#pragma mark header view

class SoundplaneHeaderView : 
	public MLAppView
{
public:
    SoundplaneHeaderView(SoundplaneModel* pModel, MLResponder* pResp, MLReporter* pRep);
    ~SoundplaneHeaderView();
    void paint (Graphics& g);
 
private:
	SoundplaneModel* mpModel;
};

// --------------------------------------------------------------------------------
#pragma mark footer view

class SoundplaneFooterView : 
	public MLAppView
{
public:
    SoundplaneFooterView(SoundplaneModel* pModel, MLResponder* pResp, MLReporter* pRep);
    ~SoundplaneFooterView();
    void paint (Graphics& g);
 	void setStatus(const char* stat, const char* client);
	void setHardware(const char* s);
	void setCalibrateProgress(float p);
	void setCalibrateState(bool b);

private:
	SoundplaneModel* mpModel;
	
	float mCalibrateProgress;
	bool mCalibrateState;
	MLLabel* mpDevice;
	MLLabel* mpStatus;
	MLLabel* mpCalibrateText;
	MLProgressBar* mpCalibrateProgress; 
	String mStatusStr;
};

// --------------------------------------------------------------------------------
#pragma mark main view

class SoundplaneView : 
	public MLAppView,
	public MLModelListener,
	public Timer
{
public:
	const Colour bg1;
	const Colour bg2;

    SoundplaneView(SoundplaneModel* pModel, MLResponder* pResp, MLReporter* pRep);
    ~SoundplaneView();

    void initialize();
    void paint (Graphics& g);
	void timerCallback();

	// MLModelListener implementation
	void doPropertyChangeAction(MLSymbol p, const MLProperty & oldVal, const MLProperty & newVal);

	void makeCarrierTogglesVisible(int v);
	
	SoundplaneViewMode getViewMode();
	void setViewMode(SoundplaneViewMode mode);
	
	// to go away
	void setMIDIDeviceString(const std::string& str);
	void setOSCServicesString(const std::string& str);
	
	void prevPage();
	void nextPage();

	//SoundplaneHeaderView* mpHeader;	// TEMP
	void goToPage (int page);

private:

	SoundplaneFooterView* mpFooter;	

	SoundplaneModel* mpModel;
	
	MLPageView* mpPages;

	MLDrawableButton* mpPrevButton;
	MLDrawableButton* mpNextButton;
	
	// page 0
	SoundplaneGridView mGridView;
	SoundplaneTouchGraphView mTouchView;
	MLTextButton* mpCalibrateButton;
	MLTextButton* mpClearButton;
	MLMenuButton* mpViewModeButton;
	
	MLGraph* mpCurveGraph;
	
	// page 1
	SoundplaneZoneView mGLView3;
	MLMenuButton* mpMIDIDeviceButton;
	MLMenuButton* mpOSCServicesButton;

	// page 2
	TrackerCalibrateView mTrkCalView;
	
	// misc
	std::vector<MLWidget*> mpCarrierToggles; // TEMP TODO use getWidget()
	std::vector<MLWidget*> mpCarrierLabels; // TEMP TODO use getWidget()
	
	int mCalibrateState;
	int mSoundplaneClientState;
	int mSoundplaneDeviceState;
	SoundplaneViewMode mViewMode;
	
};


#endif // __SOUNDPLANE_VIEW_H__