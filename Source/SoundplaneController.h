
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_CONTROLLER_H
#define __SOUNDPLANE_CONTROLLER_H

#include "JuceHeader.h"

#include "LookAndFeel/MLUIBinaryData.h"
#include "SoundplaneModel.h"
#include "SoundplaneView.h"
#include "MLReporter.h"

// widgets
#include "MLButton.h"
#include "MLDial.h"

#include "MLApp/MLNetServiceHub.h"
#include "MLFileCollection.h"

extern const char *kUDPType;
extern const char *kLocalDotDomain;

class SoundplaneController  : 
	public MLWidget::Listener,
	public MLFileCollection::Listener,
    public MLReporter,
	public juce::Timer
{
public:
	friend class SoundplaneApp;
	
    SoundplaneController(SoundplaneModel* pModel);
    ~SoundplaneController();

	// MLWidget::Listener
	void handleWidgetAction(MLWidget* w, ml::Symbol action, ml::Symbol target, const MLProperty& val = MLProperty());
	
	// MLFileCollection::Listener
	void processFileFromCollection (ml::Symbol action, const MLFile f, const MLFileCollection& collection, int idx, int size) override;
	
	// juce::Timer
	void timerCallback();

	void initialize();
	void shutdown();
	
	// menus
	void showMenu (ml::Symbol menuName, ml::Symbol instigatorName);
	void menuItemChosen(ml::Symbol menuName, int result);	
	MLMenu* findMenuByName(ml::Symbol menuName); // TODO move into new controller base class
	void setupMenus();
    void doZonePresetMenu(int result);
	void doOSCServicesMenu(int result);
 	
	SoundplaneView* getView() { return mpSoundplaneView; }
	
	// show nice message, run calibration, etc. if prefs are not found. 
	void doWelcomeTasks();
	
	bool confirmRestoreDefaults();
	
protected:
	WeakReference<SoundplaneController>::Master masterReference; // TODO -> base class
	friend class WeakReference<SoundplaneController>;
	
	// for application setup
	void setView(SoundplaneView* v);
private:
	SoundplaneModel* mpSoundplaneModel;	
	SoundplaneView* mpSoundplaneView;

	MLMenuMapT mMenuMap; 	

    int mZoneMenuStartItems;
};


#endif // __SOUNDPLANE_CONTROLLER_H
