
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_CONTROLLER_H
#define __SOUNDPLANE_CONTROLLER_H

#include "JuceHeader.h"

#include "MLUIBinaryData.h"
#include "SoundplaneModel.h"
#include "SoundplaneView.h"
#include "MLReporter.h"

// widgets
#include "MLButton.h"
#include "MLDial.h"

#include "MLNetServiceHub.h"
#include "MLFileCollection.h"

extern const char *kUDPType;
extern const char *kLocalDotDomain;

class SoundplaneController  : 
	public MLWidget::Listener,
	public MLFileCollection::Listener,
    public MLReporter,
	public MLNetServiceHub,
	public juce::Timer
{
public:
    SoundplaneController(SoundplaneModel* pModel);
    ~SoundplaneController();

	// MLWidget::Listener
	void handleWidgetAction(MLWidget* w, MLSymbol action, MLSymbol target, const MLProperty& val = MLProperty());
	
	// MLFileCollection::Listener
	void processFileFromCollection (MLSymbol action, const MLFile& f, const MLFileCollection& collection, int idx, int size);
 
	// MLNetServiceHub
	void didResolveAddress(NetService *pNetService);
	
	// juce::Timer
	void timerCallback();

	void initialize();
	void shutdown();
	
	// menus
	void showMenu (MLSymbol menuName, MLSymbol instigatorName);
	void menuItemChosen(MLSymbol menuName, int result);	
	MLMenu* findMenuByName(MLSymbol menuName); // TODO move into new controller base class
	void setupMenus();
    void doZonePresetMenu(int result);
	void doOSCServicesMenu(int result);
    
	void formatServiceName(const std::string& inName, std::string& outName);
	const std::string& getServiceName(int idx);
	
	SoundplaneView* getView() { return mpSoundplaneView; }
	void setView(SoundplaneView* v);

	SoundplaneModel* getModel() { return mpSoundplaneModel; }
	
	// show nice message, run calibration, etc. if prefs are not found. 
	void doWelcomeTasks();
	
	bool confirmRestoreDefaults();
	
protected:
	WeakReference<SoundplaneController>::Master masterReference; // TODO -> base class
	friend class WeakReference<SoundplaneController>;
	
private:
	SoundplaneModel* mpSoundplaneModel;	
	SoundplaneView* mpSoundplaneView;

	MLMenuMapT mMenuMap; 	
	std::vector<std::string> mServiceNames;
	std::vector<String> mFormattedServiceNames; // for popup menu

    MLFileCollectionPtr mZonePresets;
    int mZoneMenuStartItems;
};


#endif // __SOUNDPLANE_CONTROLLER_H