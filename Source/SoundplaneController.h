
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_CONTROLLER_H
#define __SOUNDPLANE_CONTROLLER_H

#include "JuceHeader.h"

#include "MLUIBinaryData.h"
#include "SoundplaneModel.h"
#include "SoundplaneView.h"

#include "MLResponder.h"
#include "MLReporter.h"

// widgets
#include "MLButton.h"
#include "MLDial.h"

#include "MLNetServiceHub.h"
#include "MLFileCollection.h"

extern const char *kUDPType;
extern const char *kLocalDotDomain;

class SoundplaneController  : 
	public MLResponder,
	public MLReporter,
	public MLNetServiceHub,
    public MLFileCollection::Listener,
	public Timer
{
public:
    SoundplaneController(SoundplaneModel* pModel);
    ~SoundplaneController();

	void initialize();
	void shutdown();
	void timerCallback();

	// from MLResponder
	void buttonClicked (MLButton*);
	void showMenu (MLSymbol menuName, MLSymbol instigatorName);	
	void menuItemChosen(MLSymbol menuName, int result);
	void dialDragStarted (MLDial*) {} // don’t care about drag start.
	void dialValueChanged (MLDial*);
	void dialDragEnded (MLDial*) {} // don’t care about drag end.
	void multiButtonValueChanged (MLMultiButton* , int ) {} // no multiButtons	
	void multiSliderDragStarted (MLMultiSlider* pSlider, int idx) {} // no multiSliders
	void multiSliderValueChanged (MLMultiSlider* , int ) {} // no multiSliders
	void multiSliderDragEnded (MLMultiSlider* pSlider, int idx) {} // no multiSliders
	
    // from MLFileCollection::Listener
    void processFile (const MLSymbol collection, const File& f, int idx);
    
	// menus
	void setupMenus();
    void doZonePresetMenu(int result);
	void doOSCServicesMenu(int result);
    
	void formatServiceName(const std::string& inName, std::string& outName);
	const std::string& getServiceName(int idx);
	void didResolveAddress(NetService *pNetService);

	SoundplaneView* getView() { return mpSoundplaneView; }
	void setView(SoundplaneView* v);

	SoundplaneModel* getModel() { return mpSoundplaneModel; }
	
	// show nice message, run calibration, etc. if prefs are not found. 
	void doWelcomeTasks();
	bool confirmRestoreDefaults();
    
	
private:
	bool mNeedsLateInitialize;
	SoundplaneModel* mpSoundplaneModel;	
	SoundplaneView* mpSoundplaneView;

	MLMenuMapT mMenuMap; 	
	std::vector<std::string> mServiceNames;
	std::vector<String> mFormattedServiceNames; // for popup menu

    MLFileCollectionPtr mZonePresets;
    int mZoneMenuStartItems;

};


#endif // __SOUNDPLANE_CONTROLLER_H