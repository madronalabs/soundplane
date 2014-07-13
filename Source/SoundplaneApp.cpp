
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
}

void SoundplaneApp::initialise (const String& commandLine)
{	
	mWindow.centreWithSize(800, 800*kViewGridUnitsY/kViewGridUnitsX);
	mWindow.setGridUnits(kViewGridUnitsX, kViewGridUnitsY);

	mpModel = new SoundplaneModel();
	mpController = new SoundplaneController(mpModel);
	mpController->initialize();
	mpView = new SoundplaneView(mpModel, mpController, mpController);
	mpView->initialize();		
    
	// add view to window but retain ownership here
	mWindow.setContent(mpView);
	
	mpState = new MLAppState(mpModel, mpView, MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber);
	bool foundState = mpState->loadSavedState();
    
	mpController->setView(mpView);
    mpView->goToPage(0);
	mpController->updateAllProperties();
			
	mpModel->initialize();
	MLConsole() << "Starting Soundplane...\n";
    
#if GLX
    mWindow.setUsingOpenGL(true);
#endif
    mWindow.setVisible(true);
	
	// do setup first time or after trashed prefs, or if control is held down
	if (!foundState) 
	{
		mpController->doWelcomeTasks(); 
	}
}

void SoundplaneApp::shutdown()
{
	mpState->updateAllProperties();
	mpState->saveState();
	
	if(mpView) delete mpView;
	if(mpController) delete mpController;
	if(mpModel) delete mpModel;
	
	delete mpState;
    debug().display();
}

bool SoundplaneApp::moreThanOneInstanceAllowed()
{
	return false;
}
