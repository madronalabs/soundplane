// MadronaLib: a C++ framework for DSP applications.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __ML_JUCE_APP_WINDOW__
#define __ML_JUCE_APP_WINDOW__

#include "JuceHeader.h"
#include "MLAppView.h"
#include "MLAppBorder.h"
#include "MLBoundsConstrainer.h"

class MLAppWindow : public DocumentWindow
{
public:
	MLAppWindow(MLAppView* v);
	~MLAppWindow();
	
	void initialize();
	
	void mouseDown (const MouseEvent& e);
	void mouseDrag (const MouseEvent& e);
	void setGridUnits(int gx, int gy);
	
	void closeButtonPressed();
	void moved();
	void resized();
	
private:
	MLAppView* mpAppView;
	
	// the command manager object used to dispatch command events
	ApplicationCommandManager commandManager;
	
	ComponentDragger myDragger;
	
	int mGridUnitsX;
	int mGridUnitsY;
	
};


#endif // __ML_JUCE_APP_WINDOW__


