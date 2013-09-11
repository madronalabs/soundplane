
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __T3D_EXAMPLE_CONTROLLER_H
#define __T3D_EXAMPLE_CONTROLLER_H

#include "JuceHeader.h"

#include "MLUIBinaryData.h"
#include "T3DExampleModel.h"
#include "T3DExampleView.h"

#include "MLResponder.h"
#include "MLReporter.h"

// widgets
#include "MLButton.h"
#include "MLDial.h"

#include "MLNetServiceHub.h"

extern const char *kUDPType;
extern const char *kLocalDotDomain;

class T3DExampleController  : 
	public MLResponder,
	public MLReporter,
	public Timer
{
public:
    T3DExampleController(T3DExampleModel* pModel);
    ~T3DExampleController();

	void initialize();
	void shutdown();
	void timerCallback();

	// from MLResponder
	void buttonClicked (MLButton*);
	void showMenu (MLSymbol menuName, MLSymbol instigatorName);	
	void menuItemChosen(MLSymbol menuName, int result);
	void dialDragStarted (MLDial*) {} // don’t care about drag start.
	void dialValueChanged (MLDial*);
	void dialDragEnded (MLDial*) {} // don’t care about drag end.
	void multiButtonValueChanged (MLMultiButton* , int ) {} // no multiButtons	
	void multiSliderDragStarted (MLMultiSlider* pSlider, int idx) {} // no multiSliders
	void multiSliderValueChanged (MLMultiSlider* , int ) {} // no multiSliders
	void multiSliderDragEnded (MLMultiSlider* pSlider, int idx) {} // no multiSliders
	
	// menus
	void setupMenus();
	
	T3DExampleView* getView() { return mpT3DExampleView; }
	void setView(T3DExampleView* v);

	T3DExampleModel* getModel() { return mpT3DExampleModel; }
	
private:
	T3DExampleModel* mpT3DExampleModel;	
	T3DExampleView* mpT3DExampleView;

	MLMenuMapT mMenuMap; 	

};


#endif // __T3D_EXAMPLE_CONTROLLER_H