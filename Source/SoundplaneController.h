
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

extern const char *kUDPType;
extern const char *kLocalDotDomain;

class SoundplaneController  : 
	public MLResponder,
	public MLReporter,
	public MLNetServiceHub,
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
	void showMenu (MLSymbol menuName, MLMenuButton* instigator);	
	void menuItemChosen(MLSymbol menuName, int result);
	void dialValueChanged (MLDial*);
	void multiSliderValueChanged (MLMultiSlider* , int ) {} // no multiSliders
	void multiButtonValueChanged (MLMultiButton* , int ) {} // no multiButtons	
	
	// menus
	void setupMenus();
	
	MLSymbol getCurrMenuName() { return mCurrMenuName; }
	void setCurrMenuInstigator(MLMenuButton* pI) { mCurrMenuInstigator = pI; }
	MLMenuButton* getCurrMenuInstigator() { return mCurrMenuInstigator; } 
	
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
		
	MLSymbol mCurrMenuName;
	MLMenuButton* mCurrMenuInstigator;

	PopupMenu mOSCServicesMenu;
	
	std::map<MLSymbol, MLMenuPtr> mMenuMap; 	
	std::vector<std::string> mServiceNames;
	std::vector<String> mFormattedServiceNames; // for popup menu

};


#endif // __SOUNDPLANE_CONTROLLER_H