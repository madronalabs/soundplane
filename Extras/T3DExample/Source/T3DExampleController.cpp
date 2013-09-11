
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "T3DExampleController.h"
	
T3DExampleController::T3DExampleController(T3DExampleModel* pModel) :
	MLReporter(pModel),
	mpT3DExampleModel(pModel),
	mpT3DExampleView(0)
{
	MLReporter::mpModel = pModel;
	initialize();
	startTimer(250);
}

T3DExampleController::~T3DExampleController()
{
}
	
static const std::string kOSCDefaultStr("localhost:3123 (default)");

void T3DExampleController::initialize()
{
}
	
void T3DExampleController::shutdown()
{
}

void T3DExampleController::timerCallback()
{
	updateChangedParams();
	debug().display();
	MLConsole().display();
}
	
void T3DExampleController::buttonClicked (MLButton* pButton)
{
	MLSymbol p (pButton->getParamName());
	MLParamValue t = pButton->getToggleState();

	mpModel->setModelParam(p, t);

	if (p == "clear")
	{
		mpT3DExampleModel->clear();
	}
}

void T3DExampleController::dialValueChanged (MLDial* pDial)
{
	if (!pDial) return;
	
	// mpModel->setParameter(pDial->getParamName(), pDial->getValue());
	
	MLSymbol p (pDial->getParamName());
	MLParamValue v = pDial->getValue();

	debug() << p << ": " << v << "\n";
	
	mpModel->setModelParam(p, v);
}

void T3DExampleController::setView(T3DExampleView* v) 
{ 
	mpT3DExampleView = v; 
}

static void menuItemChosenCallback (int result, T3DExampleController* pC, MLMenuPtr menu);

void T3DExampleController::setupMenus()
{
	if(!mpT3DExampleView) return;
}	

void T3DExampleController::showMenu (MLSymbol menuName, MLSymbol instigatorName)
{
	StringArray devices;
	if(!mpT3DExampleView) return;
}

static void menuItemChosenCallback (int result, T3DExampleController* pC, MLMenuPtr menu)
{
	MLWidgetContainer* pView = pC->getView();
	if(pView)
	{	
		MLWidget* pInstigator = pView->getWidget(menu->getInstigator());
		if(pInstigator != nullptr)
		{
			pInstigator->setAttribute("value", 0);
		}
	}
	pC->menuItemChosen(menu->getName(), result);
}

void T3DExampleController::menuItemChosen(MLSymbol menuName, int result)
{
	T3DExampleModel* pModel = getModel();
	assert(pModel);

	if (result > 0)
	{

	}
}