
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneApp.h"


SoundplaneApp::SoundplaneApp() :
	mpModel(0),
	mpView(0),
	mpController(0),	
	mpState(nullptr)
{
	debug() << "Starting Soundplane...\n";
}

void SoundplaneApp::initialise (const String& commandLine)
{
	mWindow.setVisible(false);
	
	mpModel = new SoundplaneModel();	
	mpController = new SoundplaneController(mpModel);
	mpController->initialize();
	mpView = new SoundplaneView(mpModel, mpController, mpController);


#ifdef DEBUG	
	// DEBUG send output back to stdout
	debug().setListener(0);
#endif

	mpView->initialize();		
	
	// add view to window but retain ownership here	
	mWindow.setContent(mpView);
	mWindow.setGridUnits(kViewGridUnitsX, kViewGridUnitsY);
	mWindow.centreWithSize(800, 800*kViewGridUnitsY/kViewGridUnitsX);
	mWindow.setResizable (true, /*useBottomRightCornerResizer*/ true);

	ModifierKeys m = juce::ModifierKeys::getCurrentModifiersRealtime();
	bool forceSetup = m.isCtrlDown();
	bool foundState = false;
	bool confirmSetup = false;
	
	mpState = new MLAppState(mpModel, mpView, ProjectInfo::makerName, ProjectInfo::projectName, ProjectInfo::versionNumber);
	
	if(forceSetup)
	{
		// confirm trashing of prefs
//		mWindow.setVisible(true);
		confirmSetup = mpController->confirmForceSetup();
	}
	
	if(!confirmSetup)
	{
		foundState = mpState->loadSavedState();
		forceSetup = false;
	}
	
	mpController->setView(mpView);
	mpController->setupMenus(); 
	mpController->updateAllParams();
			
	mWindow.setVisible(true);
	mpModel->initialize();
	
	// do setup first time or after trashed prefs, or if control is held down
	if (!foundState || forceSetup) 
	{
		// make welcome / progress window
		mpController->doWelcomeTasks(forceSetup);
	}
}

void SoundplaneApp::shutdown()
{
	mpState->updateAllParams();
	mpState->saveState();
	
	if(mpView) delete mpView;
	if(mpController) delete mpController;
	if(mpModel) delete mpModel;
	
	delete mpState;
}

bool SoundplaneApp::moreThanOneInstanceAllowed()
{
	return false;
}
