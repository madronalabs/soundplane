
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "T3DExampleApp.h"

T3DExampleApp::T3DExampleApp() :
	mpModel(0),
	mpView(0),
	mpController(0)
{
	MLConsole() << "Starting T3D Example...\n";
}

void T3DExampleApp::initialise (const String& commandLine)
{
	mWindow.setVisible(false);
	
	mpModel = new T3DExampleModel();	
	mpController = new T3DExampleController(mpModel);
	mpController->initialize();
	mpView = new T3DExampleView(mpModel, mpController, mpController);

	mpView->initialize();		
	
	// add view to window but retain ownership here	
	mWindow.setContent(mpView);
	mWindow.setGridUnits(kViewGridUnitsX, kViewGridUnitsY);
	mWindow.centreWithSize(800, 800*kViewGridUnitsY/kViewGridUnitsX);
	
	mpController->setView(mpView);
	mpController->setupMenus(); 
	mpController->updateAllParams();
			
	mWindow.setVisible(true);
	mpModel->initialize();
}

void T3DExampleApp::shutdown()
{
	if(mpView) delete mpView;
	if(mpController) delete mpController;
	if(mpModel) delete mpModel;

    debug().display();
}

bool T3DExampleApp::moreThanOneInstanceAllowed()
{
	return false;
}
