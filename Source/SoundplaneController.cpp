
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneController.h"
	
const char *kUDPType      =   "_osc._udp";
const char *kLocalDotDomain   =   "local.";

static void menuItemChosenCallback (int result, SoundplaneController* pC, MLMenuPtr menu);

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
	mServiceNames.clear();
	services.clear();
	services.push_back(kOSCDefaultStr);
	Browse(kUDPType, kLocalDotDomain);
    
    // get zone presets
    mZonePresets = MLFileCollectionPtr(new MLFileCollection("zone_preset", getDefaultFileLocation(kPresetFiles).getChildFile("ZonePresets"), "json"));
    mZonePresets->setListener(this);
    mZonePresets->searchForFilesNow();
    
    mZonePresets->dump();
    
    setupMenus(); 
}
	
void SoundplaneController::shutdown()
{
	
}

void SoundplaneController::timerCallback()
{
	updateChangedProperties();
	PollNetServices();
	debug().display();
	MLConsole().display();
}
	
void SoundplaneController::buttonClicked (MLButton* pButton)
{
	MLSymbol p (pButton->getTargetPropertyName());
	MLParamValue t = pButton->getToggleState();

	mpModel->setProperty(p, t);

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
	else if (p == "zone_preset")
	{

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
	
	MLSymbol p (pDial->getTargetPropertyName());
	MLParamValue v = pDial->getValue();

	debug() << p << ": " << v << "\n";
	
	mpModel->setProperty(p, v);
	
}

void SoundplaneController::processFile (const MLSymbol collection, const File& f, int idx)
{
    debug() << "got file " << f.getFileNameWithoutExtension() << " from coll " << collection << "\n";
    if(collection == "touch_preset")
    {

    }
    else if(collection == "zone_preset")
    {

    }
}

void SoundplaneController::setView(SoundplaneView* v)
{ 
	mpSoundplaneView = v; 
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

void SoundplaneController::setupMenus()
{
	MLMenuPtr viewMenu(new MLMenu("viewmode"));
	mMenuMap["viewmode"] = viewMenu;
	viewMenu->addItem("raw data");
	viewMenu->addItem("calibrated");
	viewMenu->addItem("cooked");
	viewMenu->addItem("xy");
	viewMenu->addItem("test1");
	viewMenu->addItem("test2");
	viewMenu->addItem("norm. map");
	
	mMenuMap["midi_device"] = MLMenuPtr(new MLMenu("midi_device"));
    
	MLMenuPtr zoneMenu(new MLMenu("zone_preset"));
	mMenuMap["zone_preset"] = zoneMenu;
    
    // set up built-in zone maps
    zoneMenu->addItem("chromatic");
	zoneMenu->addItem("rows in fourths");
	zoneMenu->addItem("rows in octaves");
	zoneMenu->addSeparator();
    
    // add zone presets from disk
    mZoneMenuStartItems = zoneMenu->getSize();
    MLMenuPtr p = mZonePresets->buildMenu();
    zoneMenu->appendMenu(p);
    
 	mMenuMap["touch_preset"] = MLMenuPtr(new MLMenu("touch_preset"));
	mMenuMap["osc_services"] = MLMenuPtr(new MLMenu("osc_services"));
	
	// setup OSC defaults 
	mpModel->setProperty("osc_services", kOSCDefaultStr);	
}	

void SoundplaneController::showMenu(MLSymbol menuName, MLSymbol instigatorName)
{
	StringArray devices;
	if(!mpSoundplaneView) return;
 	
    // dump menu map
    if(0)
    {
        debug() << "LOOKING for MENU " << menuName << "\n";
        MLMenuMapT::iterator it;
        debug() << mMenuMap.size() << "menus:\n";
        for(it=mMenuMap.begin(); it != mMenuMap.end(); ++it)
        {
            MLSymbol name = it->first;
            debug() << "    " << name << "\n";
        }
    }
 
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
		
		// update menus that are rebuilt each time
		if (menuName == "midi_device")
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
        
        // show menu
        if(menu != MLMenuPtr())
		{
                // TEMP
            menu->dump();
            
			if(pInstigator != nullptr)
			{
				Component* pInstComp = pInstigator->getComponent();
				if(pInstComp)
				{
                    const int u = pInstigator->getWidgetGridUnitSize();
                    int height = ((float)u)*0.35f;
                    height = clamp(height, 12, 128);
					JuceMenuPtr juceMenu = menu->getJuceMenu();
					juceMenu->showMenuAsync (PopupMenu::Options().withTargetComponent(pInstComp).withStandardItemHeight(height),
						ModalCallbackFunction::withParam(menuItemChosenCallback, this, menu));
				}
			}
		}
	}
}

void SoundplaneController::menuItemChosen(MLSymbol menuName, int result)
{
	if (result > 0)
	{
		MLMenuPtr menu = mMenuMap[menuName];        
		if (menu != MLMenuPtr())
		{
            SoundplaneModel* pModel = getModel();
            assert(pModel);
            const std::string& fullName = menu->getItemFullName(result);
			pModel->setProperty(menuName, fullName);
		}
        
		if (menuName == "zone_preset")
        {
            doZonePresetMenu(result);
        }
		else if (menuName == "osc_services")
		{
            doOSCServicesMenu(result);
        }
 	}
}

void SoundplaneController::doZonePresetMenu(int result)
{
    // the Model's zone_preset parameter contains only the name of the menu choice.
    // the Model's zone_JSON parameter contains all the zone data in JSON format.
    // the preset parameter will not trigger loading of the zone JSON file when the
    // app is re-opened, rather, the JSON is stored in the app state as a string parameter.
    //
    // TODO mark the zone_preset parameter as changed from saved version, when some
    // zone info changes. Display so the user can see that it's changed, and also
    // ask to verify overwrite when loading a new preset.
	SoundplaneModel* pModel = getModel();
	assert(pModel);    
    std::string zoneStr;
    switch(result)
    {
        // get built-in JSON string for first menu items
        case 1:
            zoneStr = std::string(SoundplaneBinaryData::continuous_json);
            break;
        case 2:
            zoneStr = std::string(SoundplaneBinaryData::chromaticfourths_json);
            break;
        case 3:
            zoneStr = std::string(SoundplaneBinaryData::chromaticoctaves_json);
            break;
        // get JSON from file
        default:          
            MLMenuPtr menu = mMenuMap["zone_preset"];
            const std::string& fullName = menu->getItemFullName(result);
            MLFilePtr f = mZonePresets->getFileByName(fullName);
            File zoneFile = f->mFile;
            String stateStr(zoneFile.loadFileAsString());
            zoneStr = (stateStr.toUTF8());
            break;
    }

    pModel->setProperty("zone_JSON", zoneStr);
}

void SoundplaneController::doOSCServicesMenu(int result)
{    
 	SoundplaneModel* pModel = getModel();
	assert(pModel);

    // TODO should this not be in Model::setProperty?
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
        name = getServiceName(result - 1);
        Resolve(name.c_str(), kUDPType, kLocalDotDomain);
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
    if(mpSoundplaneView)
    {
        // quick hack to refresh grid.
        // otherwise it's blank.
        // TODO find out why grid is blank after welcome! 
        mpSoundplaneView->goToPage(1);
        mpSoundplaneView->goToPage(0);
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

