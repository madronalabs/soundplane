
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __T3D_EXAMPLE_VIEW_H__
#define __T3D_EXAMPLE_VIEW_H__

#include "JuceHeader.h"

#include "T3DExampleModel.h"
#include "MLButton.h"
#include "MLDrawableButton.h"
#include "MLTextButton.h"
#include "MLMultiSlider.h"
#include "MLMultiButton.h"
#include "MLEnvelope.h"
#include "MLProgressBar.h"
#include "MLGraph.h"
#include "MLVector.h"
#include "MLAppView.h"
#include "MLPageView.h"
#include "MLResponder.h"

const int kViewGridUnitsX = 15;
const int kViewGridUnitsY = 10;

// --------------------------------------------------------------------------------
#pragma mark main view

class T3DExampleView : 
	public MLAppView,
	public MLModelListener,
	public Timer
{
public:
    T3DExampleView(T3DExampleModel* pModel, MLResponder* pResp, MLReporter* pRep);
    ~T3DExampleView();

    void initialize();
    void paint (Graphics& g);
	void timerCallback();

	// MLModelListener implementation
	void doParamChangeAction(MLSymbol param, const MLModelParam & oldVal, const MLModelParam & newVal);

private:
	T3DExampleModel* mpModel;
	MLDebugDisplay* mpDebugDisplay;
	
};


#endif // __T3D_EXAMPLE_VIEW_H__