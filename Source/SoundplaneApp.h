
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_APP__
#define __SOUNDPLANE_APP__

#include "JuceHeader.h"
#include "SoundplaneModel.h"
#include "SoundplaneView.h"
#include "SoundplaneController.h"
#include "MLDebug.h"
#include "MLAppState.h"
#include "MLAppWindow.h"
#include "modules/juce_gui_basics/keyboard/juce_ModifierKeys.h"

class SoundplaneApp : 
	public JUCEApplication
{
public:
    SoundplaneApp();
    ~SoundplaneApp(){}

    void initialise (const String& commandLine);
	
    void shutdown();
	
    const String getApplicationName()
    {
		return MLProjectInfo::projectName;
	}

    const String getApplicationVersion()
    {
        return MLProjectInfo::versionString;
    }

    bool moreThanOneInstanceAllowed();
    void anotherInstanceStarted (const String& commandLine){}

private:	
	
	void setDefaultWindowSize();
	
	SoundplaneModel* mpModel;
	SoundplaneView* mpView;
	SoundplaneController* mpController;	
	std::unique_ptr<MLAppState> mpModelState;
	std::unique_ptr<MLAppState> mpViewState;
	std::unique_ptr<MLAppWindow> mpWindow;
};

// This macro creates the application's main() function.
START_JUCE_APPLICATION (SoundplaneApp)


#endif // __SOUNDPLANE_APP__