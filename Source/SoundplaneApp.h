
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
#include "juce_ModifierKeys.h"

class SoundplaneApp : 
	public JUCEApplication
{
public:
    SoundplaneApp();
    ~SoundplaneApp(){}

    void initialise (const String& commandLine);
    void shutdown();
	void timerCallback();
	
    const String getApplicationName()
    {
		return ProjectInfo::projectName;
	}

    const String getApplicationVersion()
    {
        return ProjectInfo::versionString;
    }

    bool moreThanOneInstanceAllowed();
    void anotherInstanceStarted (const String& commandLine){}

private:
	
	SoundplaneModel* mpModel;
	SoundplaneView* mpView;
	SoundplaneController* mpController;	
	MLAppState* mpState;
	MLAppWindow mWindow;
	MLNetServiceHub mNetServices;
	
	
	
};

// This macro creates the application's main() function.
START_JUCE_APPLICATION (SoundplaneApp)


#endif // __SOUNDPLANE_APP__