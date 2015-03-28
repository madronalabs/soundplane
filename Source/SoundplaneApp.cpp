
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneApp.h"

SoundplaneApp::SoundplaneApp() :
	mpModel(0),
	mpView(0),
	mpController(0)
{
}

void SoundplaneApp::initialise (const String& commandLine)
{	
	mpWindow = std::unique_ptr<MLAppWindow>(new MLAppWindow());
	mpWindow->setGridUnits(kSoundplaneViewGridUnitsX, kSoundplaneViewGridUnitsY);
	// mpWindow->centreWithSize(800, 800*kSoundplaneViewGridUnitsY/kSoundplaneViewGridUnitsX);
	
	mpModel = new SoundplaneModel();
	mpModel->initialize();
	
	mpController = new SoundplaneController(mpModel);
	mpController->initialize();
	
	mpView = new SoundplaneView(mpModel, mpController, mpController);
	mpView->initialize();		
    
	// add view to window but retain ownership here
	mpWindow->setContent(mpView);
	
	mpModelState = std::unique_ptr<MLAppState>(new MLAppState(mpModel, "", MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber));
	bool foundState = mpModelState->loadStateFromAppStateFile();
    
	mpController->setView(mpView);

	debug() << "Starting Soundplane...\n";
    
#if GLX
    mpWindow->setUsingOpenGL(true);
#endif
    mpWindow->setVisible(true);
	
	// do setup first time or after trashed prefs, or if control is held down
	if (!foundState) 
	{
		mpController->doWelcomeTasks(); 
	}
	
	mpController->fetchAllProperties();
	mpView->goToPage(0);
	
	// generate a persistent state for the application's view. 
	mpViewState = std::unique_ptr<MLAppState>(new MLAppState(mpView, "View", MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber));
	bool foundViewState = mpViewState->loadStateFromAppStateFile();
	
	if(!foundViewState)
	{
		// set default size
		mpWindow->centreWithSize(800, 800*kSoundplaneViewGridUnitsY/kSoundplaneViewGridUnitsX);
	}
	else
	{
		mpView->updateAllProperties();	
	}
}

void SoundplaneApp::shutdown()
{
	mpModelState->updateAllProperties();
	mpModelState->saveStateToStateFile();
	
	mpViewState->updateAllProperties();
	mpViewState->saveStateToStateFile();
	
	if(mpView) delete mpView;
	if(mpController) delete mpController;
	if(mpModel) delete mpModel;
	
//    debug().display();
}

bool SoundplaneApp::moreThanOneInstanceAllowed()
{
	return false;
}
