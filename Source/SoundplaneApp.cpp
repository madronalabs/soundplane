
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneApp.h"

SoundplaneApp::SoundplaneApp()
{
}

SoundplaneApp::~SoundplaneApp()
{
	delete mpController;
	delete mpModel;
	delete mpView;
	delete mpBorder;
	delete mpWindow;
}

void SoundplaneApp::initialise (const String& commandLine)
{
	MLConsole() << "Starting Soundplane...\n";
	mpModel = new SoundplaneModel();
	
	mpController = new SoundplaneController(mpModel);
	mpView = new SoundplaneView(mpModel, mpController, mpController);
	
	mpBorder = new MLAppBorder(mpView);
	mpBorder->makeResizer(mpView);
	mpBorder->setGridUnits(kSoundplaneViewGridUnitsX, kSoundplaneViewGridUnitsY);
	mpBorder->setBounds(mpView->getBounds());
	
	mpWindow = new MLAppWindow(mpView);
	
	// add view to window but retain ownership here
	bool resizeToFit = true;
	
	mpWindow->setContentNonOwned(mpBorder, resizeToFit);
	mpWindow->setGridUnits(kSoundplaneViewGridUnitsX, kSoundplaneViewGridUnitsY);
	mpWindow->setVisible(true);
	
	mpWindow->setConstrainer (mpBorder->getConstrainer());
	
	mpBorder->addAndMakeVisible(mpView);
	mpController->setView(mpView);
	
	mpController->initialize();
	mpView->initialize();
	
	// generate a persistent state for the Model
	mpModelState = std::unique_ptr<MLAppState>(new MLAppState(mpModel, "", MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber));
	if (!mpModelState->loadStateFromAppStateFile())
	{
		// there is no app state saved, run "welcome to Soundplane" with carrier select
		setDefaultWindowSize();
		mpController->doWelcomeTasks();
	}
	mpModel->updateAllProperties();
	
	// generate a persistent state for the application's view.
	mpViewState = std::unique_ptr<MLAppState>(new MLAppState(mpView, "View", MLProjectInfo::makerName, MLProjectInfo::projectName, MLProjectInfo::versionNumber));
	
	ModifierKeys k = ModifierKeys::getCurrentModifiersRealtime();
	if((k.isCommandDown()) || (!mpViewState->loadStateFromAppStateFile()))
	{
		setDefaultWindowSize();
	}
	else
	{
		mpView->updateAllProperties();
	}
	
	mpController->fetchAllProperties();
	mpView->goToPage(0);
}

void SoundplaneApp::shutdown()
{
	mpModelState->updateAllProperties();
	mpModelState->saveStateToStateFile();
	
	mpViewState->updateAllProperties();
	mpViewState->saveStateToStateFile();
	
	mpWindow->setVisible(false);
	if(mpController) mpController->setView(0);
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

