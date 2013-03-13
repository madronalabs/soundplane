
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
	
	mpState = new MLAppState(mpModel, mpView, ProjectInfo::makerName, ProjectInfo::projectName, ProjectInfo::versionNumber);
	mpState->loadSavedState();
	
	mpController->setView(mpView);
	mpController->setupMenus(); 
	mpController->updateAllParams();
			
	mWindow.setVisible(true);
	mpModel->initialize();

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
