
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

SoundplaneApp::~SoundplaneApp()
{
}

void SoundplaneApp::initialise (const String& commandLine)
{
	mpModel = new SoundplaneModel();

	mpController = new SoundplaneController(mpModel);
	mpView = new SoundplaneView(mpModel, mpController, mpController);
	mpController->setView(mpView);

	mpController->initialize();	
	mpView->initialize();		// ?

	mpWindow = std::unique_ptr<MLAppWindow>(new MLAppWindow());	
	mpWindow->setGridUnits(kSoundplaneViewGridUnitsX, kSoundplaneViewGridUnitsY);

	// add view to window but retain ownership here
	mpWindow->setContent(mpView);

	// generate a persistent state for the Model
	mpModelState = std::unique_ptr<MLAppState>(new MLAppState(mpModel, "", MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber));

	// generate a persistent state for the application's view. 
	mpViewState = std::unique_ptr<MLAppState>(new MLAppState(mpView, "View", MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber));

	MLConsole() << "Starting Soundplane...\n";

	#if GLX
	mpWindow->setUsingOpenGL(true);
	#endif
	mpWindow->setVisible(true);

	// do setup first time or after trashed prefs, or if control is held down
	if (!mpModelState->loadStateFromAppStateFile()) 
	{
		setDefaultWindowSize();
		mpController->doWelcomeTasks(); 
	}
	mpController->fetchAllProperties();
	mpView->goToPage(0);

	if(mpViewState->loadStateFromAppStateFile())
	{
		mpView->updateAllProperties();	
	}
	else
	{
		setDefaultWindowSize();
	}
	
	// separate thread needed for JUCE-based app
	mpModel->startProcessThread();
}
	
void SoundplaneApp::shutdown()
{
	mpModelState->updateAllProperties();
	mpModelState->saveStateToStateFile();
	
	mpViewState->updateAllProperties();
	mpViewState->saveStateToStateFile();

	delete mpView;
	delete mpController;
	delete mpModel;
}

void SoundplaneApp::setDefaultWindowSize()
{
	if(mpWindow)
	{
		mpWindow->centreWithSize(800, 800*kSoundplaneViewGridUnitsY/kSoundplaneViewGridUnitsX);
	}
}

bool SoundplaneApp::moreThanOneInstanceAllowed()
{
	return false;
}
