
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "T3DExampleView.h"


// --------------------------------------------------------------------------------
T3DExampleView::T3DExampleView (T3DExampleModel* pModel, MLResponder* pResp, MLReporter* pRep) :
	MLAppView(pResp, pRep),
	mpModel(pModel),
	MLModelListener(pModel),
    mpDebugDisplay(0)

{
    setWidgetName("example_view");
    
	// setup application's look and feel 
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	LookAndFeel::setDefaultLookAndFeel (myLookAndFeel);	
	myLookAndFeel->setGradientMode(1); // A->B->A
	myLookAndFeel->setGlobalTextScale(1.0f);

	MLDial* pD;

	// make controls
	//	
	MLRect dialRect (0, 0, 1.25, 1.0);	

	pD = addDial("data rate", dialRect.withCenter(13.5, 1.75), "data_rate");
	pD->setRange(1, 1000., 1);
	pD->setDefault(100);
	
    mpDebugDisplay = addDebugDisplay(MLRect(1., 4.0, 13., 6.));
	debug().sendOutputToListener(mpDebugDisplay);
    
	pModel->addParamListener(this); 
}

T3DExampleView::~T3DExampleView()
{
    debug().sendOutputToListener(0);
	stopTimer();
}

void T3DExampleView::initialize()
{		
	startTimer(50);	
	setAnimationsActive(true);	
	updateAllParams();
}

void T3DExampleView::paint (Graphics& g)
{
	MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance();
	myLookAndFeel->drawBackground(g, this);
    
	// TEST paint grid
	myLookAndFeel->drawUnitGrid(g);
}

void T3DExampleView::timerCallback()
{
	updateChangedParams();
}


// --------------------------------------------------------------------------------
// MLModelListener implementation
// an updateChangedParams() is needed to get these actions sent by the Model.
//
void T3DExampleView::doParamChangeAction(MLSymbol param, const MLModelParam & oldVal, const MLModelParam & newVal)
{
	debug() << "T3DExampleView::doParamChangeAction: " << param << " from " << oldVal << " to " << newVal << "\n";	
}
