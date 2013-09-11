
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __T3D_EXAMPLE_APP__
#define __T3D_EXAMPLE_APP__

#include "JuceHeader.h"
#include "T3DExampleModel.h"
#include "T3DExampleView.h"
#include "T3DExampleController.h"
#include "MLDebug.h"
#include "MLAppState.h"
#include "MLAppWindow.h"
#include "juce_ModifierKeys.h"

class T3DExampleApp : 
	public JUCEApplication
{
public:
    T3DExampleApp();
    ~T3DExampleApp(){}

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
	T3DExampleModel* mpModel;
	T3DExampleView* mpView;
	T3DExampleController* mpController;	
	MLAppWindow mWindow;
	MLNetServiceHub mNetServices;
};

// This macro creates the application's main() function.
START_JUCE_APPLICATION (T3DExampleApp)


#endif // __T3D_EXAMPLE_APP__