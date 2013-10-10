
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneController.h"
	
const char *kUDPType      =   "_osc._udp";
const char *kLocalDotDomain   =   "local.";

SoundplaneController::SoundplaneController(SoundplaneModel* pModel) :
	MLReporter(pModel),
	mpSoundplaneModel(pModel),
	mpSoundplaneView(0),
//	mCurrMenuInstigator(nullptr),
	mNeedsLateInitialize(true)
{
	MLReporter::mpModel = pModel;
	startTimer(250);
}

SoundplaneController::~SoundplaneController()
{
}
	
static const std::string kOSCDefaultStr("localhost:3123 (default)");

void SoundplaneController::initialize()
{
	// prime MIDI device pump
	StringArray devices = MidiOutput::getDevices();
	
	// make OSC services list
//	mOSCServicesMenu.clear();
	mServiceNames.clear();
	services.clear();
	services.push_back(kOSCDefaultStr);
	Browse(kUDPType, kLocalDotDomain);
    
    /*
    MLFileCollection c(getDefaultFileLocation(kScaleFiles), "scl");// .getChildFile())
    c.findFilesImmediate();
     */
}
	
void SoundplaneController::shutdown()
{
	
}

void SoundplaneController::timerCallback()
{
	updateChangedParams();
	PollNetServices();
	debug().display();
	MLConsole().display();
}
	
void SoundplaneController::buttonClicked (MLButton* pButton)
{
	MLSymbol p (pButton->getParamName());
	MLParamValue t = pButton->getToggleState();

	mpModel->setModelParam(p, t);

	/*
	if (p == "carriers")
	{
		MLParamValue b = pButton->getToggleState();
		debug() << "buttonClicked: " << b << "\n";
        mpModel->enableCarriers(b ? 0xFFFFFFFF : 0); 
	}
	*/
	if (p == "clear")
	{
		mpSoundplaneModel->clear();
	}
	else if (p == "select_carriers")
	{		
		mpSoundplaneModel->beginSelectCarriers();
	}
	else if (p == "restore_defaults")
	{		
		if(confirmRestoreDefaults())
		{
			mpSoundplaneModel->setAllParamsToDefaults();
			doWelcomeTasks();
		}
	}
	else if (p == "default_carriers")
	{		
		mpSoundplaneModel->setDefaultCarriers();
	}
	else if (p == "calibrate")
	{
		mpSoundplaneModel->beginCalibrate();
	}
	else if (p == "preset")
	{
	
	}
	else if (p == "normalize")
	{
		mpSoundplaneModel->beginCalibrate();
		mpSoundplaneModel->beginNormalize();
		if(mpSoundplaneView)
		{
			MLWidget* pB = mpSoundplaneView->getWidget("normalize_cancel");
			pB->getComponent()->setEnabled(true);
		}
	}
	else if (p == "normalize_cancel")
	{
		mpSoundplaneModel->cancelNormalize();
	}
	else if (p == "normalize_default")
	{
		mpSoundplaneModel->setDefaultNormalize();
	}
	else if(p == "prev")
	{
	
		if(mpSoundplaneView)
		{
			mpSoundplaneView->prevPage();
		}
	}
	else if (p == "next")
	{
		if(mpSoundplaneView)
		{
			mpSoundplaneView->nextPage();
		}
	}	
}

void SoundplaneController::dialValueChanged (MLDial* pDial)
{
	if (!pDial) return;
	
	// mpModel->setParameter(pDial->getParamName(), pDial->getValue());
	
	MLSymbol p (pDial->getParamName());
	MLParamValue v = pDial->getValue();

	debug() << p << ": " << v << "\n";
	
	mpModel->setModelParam(p, v);
	
}

void SoundplaneController::setView(SoundplaneView* v) 
{ 
	mpSoundplaneView = v; 
}

static void menuItemChosenCallback (int result, SoundplaneController* pC, MLMenuPtr menu);

void SoundplaneController::setupMenus()
{
	if(!mpSoundplaneView) return;
	
	MLMenuPtr viewMenu = MLMenuPtr(new MLMenu("viewmode"));
	mMenuMap["viewmode"] = viewMenu;
	viewMenu->addItem("raw data");
	viewMenu->addItem("calibrated");
	viewMenu->addItem("cooked");
	viewMenu->addItem("xy");
	viewMenu->addItem("test");
	viewMenu->addItem("norm. map");
	
	// collect MIDI menu each time, not here
	MLMenuPtr midiMenu = MLMenuPtr(new MLMenu("midi_device"));
	mMenuMap["midi_device"] = midiMenu;
		
	// rebuild zone presets menu each time
	MLMenuPtr presetMenu = MLMenuPtr(new MLMenu("preset"));
	mMenuMap["preset"] = presetMenu;
	
	// rebuild touch presets menu each time
	MLMenuPtr touchMenu = MLMenuPtr(new MLMenu("touch_preset"));
	mMenuMap["touch_preset"] = touchMenu;
	
	MLMenuPtr oscMenu = MLMenuPtr(new MLMenu("osc_services"));
	mMenuMap["osc_services"] = oscMenu;
	
	// setup defaults 
	mpModel->setModelParam("osc_services", kOSCDefaultStr);	
}	

void SoundplaneController::showMenu (MLSymbol menuName, MLSymbol instigatorName)
{
	StringArray devices;
	if(!mpSoundplaneView) return;
	
	MLMenuMapT::iterator menuIter(mMenuMap.find(menuName));
	if (menuIter != mMenuMap.end())
	{
		MLMenuPtr menu = menuIter->second;
		menu->setInstigator(instigatorName);

		// find instigator widget and set value to 1 - this depresses menu buttons for example
		MLWidget* pInstigator = mpSoundplaneView->getWidget(instigatorName);
		if(pInstigator != nullptr)
		{
			pInstigator->setAttribute("value", 1);
		}

		MLLookAndFeel* myLookAndFeel = MLLookAndFeel::getInstance(); // should get from View
		int u = myLookAndFeel->getGridUnitSize();
		int height = ((float)u)*0.35f;
		height = clamp(height, 12, 128);
		
		// update menus that might change each time
		if (menuName == "preset")
		{
			// populate preset menu
			// TODO look at files in preset dir
			//
			menu->clear();
			menu->addItem("continuous pitch x");
			menu->addItem("rows in fourths");
		}
		else if (menuName == "touch_preset")
		{
			// populate touch preset menu
			// TODO look at files in preset dir
			//
			menu->clear();
			menu->addItem("touch 1");
			menu->addItem("touch preset 2");
		}
		else if (menuName == "midi_device")
		{
			// refresh device list
			menu->clear();		
			SoundplaneMIDIOutput& outs = getModel()->getMIDIOutput();
			outs.findMIDIDevices ();
			std::vector<std::string>& devices = outs.getDeviceList();
			menu->addItems(devices);
		}
		else if (menuName == "osc_services")
		{
			menu->clear();		
			mServiceNames.clear();
			std::vector<std::string>::iterator it;
			for(it = services.begin(); it != services.end(); it++)
			{
				const std::string& serviceName = *it;
				std::string formattedName;
				formatServiceName(serviceName, formattedName);
				mServiceNames.push_back(serviceName);
				menu->addItem(formattedName);
			}
		}	
		if(menu != MLMenuPtr())
		{
			if(pInstigator != nullptr)
			{
				Component* pInstComp = pInstigator->getComponent();
				if(pInstComp)
				{
					PopupMenu& juceMenu = menu->getJuceMenu();
					juceMenu.showMenuAsync (PopupMenu::Options().withTargetComponent(pInstComp).withStandardItemHeight(height),
						ModalCallbackFunction::withParam(menuItemChosenCallback, this, menu));
				}
			}
		}
	}
}

static void menuItemChosenCallback (int result, SoundplaneController* pC, MLMenuPtr menu)
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

void SoundplaneController::menuItemChosen(MLSymbol menuName, int result)
{
	SoundplaneModel* pModel = getModel();
	assert(pModel);

	if (result > 0)
	{
		int menuIdx = result - 1;
		MLMenuPtr menu = mMenuMap[menuName];
		if (menu != MLMenuPtr())
		{
			pModel->setModelParam(menuName, menu->getItemString(menuIdx));
		}

		if (menuName == "osc_services")
		{
			std::string name;
			if(result == 1) // set default
			{
				name = "default";
				SoundplaneOSCOutput& output = pModel->getOSCOutput();
				output.connect(kDefaultHostnameString, kDefaultUDPPort);
				pModel->setKymaMode(0);
			}
			else // resolve a service from list
			{
				name = getServiceName(menuIdx);
				Resolve(name.c_str(), kUDPType, kLocalDotDomain);
			}			
		}
	}
}

void SoundplaneController::formatServiceName(const std::string& inName, std::string& outName)
{
	const char* inStr = inName.c_str();
	if(!strncmp(inStr, "beslime", 7))
	{
		outName = inName + std::string(" (Kyma)");
	}
	else
	{
		outName = inName;
	}
}

const std::string& SoundplaneController::getServiceName(int idx)
{
	return mServiceNames[idx];
}

// called asynchronously after Resolve() when host and port are found
//
void SoundplaneController::didResolveAddress(NetService *pNetService)
{
	const std::string& serviceName = pNetService->getName();
	const std::string& hostName = pNetService->getHostName();
	const char* hostNameStr = hostName.c_str();
	int port = pNetService->getPort();
	
	debug() << "resolved net service to " << hostName << ", " << port << "\n";
	
	// TEMP todo don't access output directly
	if(mpSoundplaneModel)
	{
		SoundplaneOSCOutput& output = mpSoundplaneModel->getOSCOutput();
		output.connect(hostNameStr, port);
	}
	
	// if we are talking to a kyma, set kyma mode
	static const char* kymaStr = "beslime";
	int len = strlen(kymaStr);
	bool isProbablyKyma = !strncmp(serviceName.c_str(), kymaStr, len);
debug() << "kyma mode " << isProbablyKyma << "\n";
	mpSoundplaneModel->setKymaMode(isProbablyKyma);
	
}

class SoundplaneSetupThread  : public ThreadWithProgressWindow
{
public:
    SoundplaneSetupThread(SoundplaneModel* m, Component* parent)
        : ThreadWithProgressWindow (String::empty, true, true, 1000, "Cancel", parent),
		mpModel(m)
    {
        setStatusMessage ("Welcome to Soundplane!");
    }

    ~SoundplaneSetupThread()
    {
    }

    void run()
    {
		setProgress (-1.0); 
        wait (1000);	
			
		while(mpModel->getDeviceState() != kDeviceHasIsochSync)
		{
			setStatusMessage ("Looking for Soundplane. Please connect your Soundplane via USB.");
			if (threadShouldExit()) return;
			wait (1000);
		}
		
		setStatusMessage ("Selecting carrier frequencies...");
		mpModel->beginSelectCarriers();
		while(mpModel->isSelectingCarriers())
        {
            if (threadShouldExit()) return;					
            setProgress (mpModel->getSelectCarriersProgress());
        }
    }
	
	private:
		SoundplaneModel* mpModel;
};

void SoundplaneController::doWelcomeTasks()
{
	SoundplaneSetupThread demoThread(getModel(), getView());
	if (demoThread.runThread())
	{
		// thread finished normally..
		AlertWindow::showMessageBox (AlertWindow::NoIcon,
			String::empty, "Setup successful.", "OK");
	}
	else
	{
		// user pressed the cancel button..
		AlertWindow::showMessageBox (AlertWindow::NoIcon,
			String::empty, "Setup cancelled. Calibration not complete. ",
			"OK");
	}
}

bool SoundplaneController::confirmRestoreDefaults()
{
	bool doSetup = AlertWindow::showOkCancelBox(AlertWindow::NoIcon,
			String::empty,
			"Really restore all settings to defaults?\nCurrent settings will be lost." ,
			"OK",
			"Cancel"
			);
			
	return doSetup;
}

