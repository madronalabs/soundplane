
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneController.h"

static void menuItemChosenCallback (int result, WeakReference<SoundplaneController> wpC, ml::Symbol menuName);

SoundplaneController::SoundplaneController(SoundplaneModel* pModel) :
	mpSoundplaneModel(pModel),
	mpSoundplaneView(0),
	MLReporter()
{
	MLReporter::listenTo(mpSoundplaneModel);
	startTimer(250);
}

SoundplaneController::~SoundplaneController()
{
	stopTimer();
	masterReference.clear();
}
	
#pragma mark MLWidget::Listener

void SoundplaneController::handleWidgetAction(MLWidget* w, ml::Symbol action, ml::Symbol p, const MLProperty& val)
{
	if(action == "click") // handle momentary buttons.
	{
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
				mpSoundplaneModel->setAllPropertiesToDefaults();
				doWelcomeTasks();
				mpSoundplaneModel->updateAllProperties();
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

		else if(p == "prev")
		{
			if(mpSoundplaneView)
			{
				mpSoundplaneView->prevPage();
			}
			mpSoundplaneModel->setProperty("view_page", mpSoundplaneView->getCurrentPage());
		}
		else if (p == "next")
		{
			if(mpSoundplaneView)
			{
				mpSoundplaneView->nextPage();
			}
			mpSoundplaneModel->setProperty("view_page", mpSoundplaneView->getCurrentPage());
		}
	}
	else if(action == "show_menu")
	{
		showMenu(p, w->getWidgetName());
	}
	else if(action == "change_property") // handle property changes.
	{
		mpSoundplaneModel->setProperty(p, val);
		/*
		 if (p == "carriers")
		 {
		 MLParamValue b = pButton->getToggleState();
		 debug() << "buttonClicked: " << b << "\n";
		 mpModel->enableCarriers(b ? 0xFFFFFFFF : 0);
		 }
		 */
	}
}

void SoundplaneController::initialize()
{
	// prime MIDI device pump
	StringArray devices = MidiOutput::getDevices();
   
	setupMenus(); 
}
	
void SoundplaneController::shutdown()
{
	
}

void SoundplaneController::timerCallback()
{
	fetchChangedProperties();
	MLConsole().display();
}
	
// process a file from one of the Model's collections. Currenlty unused but will be used when file
// collections update menus constantly in the background. 
void SoundplaneController::processFileFromCollection (ml::Symbol action, const MLFile& file, const MLFileCollection& collection, int idx, size_t size)
{
	ml::Symbol collName = collection.getName();
    if(collName == "touch_preset")
    {

    }
    else if(collName == "zone_preset")
    {

    }
}

void SoundplaneController::setView(SoundplaneView* v)
{ 
	mpSoundplaneView = v; 
}

static void menuItemChosenCallback (int result, WeakReference<SoundplaneController> wpC, ml::Symbol menuName)
{
	SoundplaneController* pC = wpC;
	
	// get Controller ptr from weak reference
	if(pC == nullptr)
	{
		debug() << "    null SoundplaneController ref!\n";
		return;
	}
	
	//debug() << "    SoundplaneController:" << std::hex << (void *)pC << std::dec << "\n";
	const MLMenu* pMenu = pC->findMenuByName(menuName);
	if (pMenu == nullptr)
	{
		debug() << "    SoundplaneController::menuItemChosenCallback(): menu not found!\n";
	}
	else
	{
		MLWidgetContainer* pView = pC->getView();
		
		//debug() << "    pView:" << std::hex << (void *)pView << std::dec << "\n";
		if(pView != nullptr)
		{
			//debug() << "        pView widget name:" << pView->getWidgetName() << "\n";
			
			MLWidget* pInstigator = pView->getWidget(pMenu->getInstigator());
			if(pInstigator != nullptr)
			{
				// turn instigator Widget off
				pInstigator->setPropertyImmediate("value", 0);
			}
		}
		
		pC->menuItemChosen(menuName, result);
	}
}


void SoundplaneController::setupMenus()
{
	MLMenuPtr viewMenu(new MLMenu("viewmode"));
	mMenuMap["viewmode"] = viewMenu;
	viewMenu->addItem("raw data");
	viewMenu->addItem("calibrated");
	viewMenu->addItem("xy");
	viewMenu->addItem("touches");

	mMenuMap["midi_device"] = MLMenuPtr(new MLMenu("midi_device"));
    
	MLMenuPtr zoneMenu(new MLMenu("zone_preset"));
	mMenuMap["zone_preset"] = zoneMenu;
    
 	mMenuMap["touch_preset"] = MLMenuPtr(new MLMenu("touch_preset"));
	mMenuMap["osc_service_name"] = MLMenuPtr(new MLMenu("osc_service_name"));
}	


MLMenu* SoundplaneController::findMenuByName(ml::Symbol menuName)
{
	MLMenu* r = nullptr;
	MLMenuMapT::iterator menuIter(mMenuMap.find(menuName));
	if (menuIter != mMenuMap.end())
	{
		MLMenuPtr menuPtr = menuIter->second;
		r = menuPtr.get();
	}
	return r;
}

void SoundplaneController::showMenu (ml::Symbol menuName, ml::Symbol instigatorName)
{
	if(!mpSoundplaneView) return;
	if(!mpSoundplaneModel) return;
	
	MLMenu* menu = findMenuByName(menuName);
	if (menu != nullptr)
	{
		menu->setInstigator(instigatorName);
		
		// update menus that are rebuilt each time
		if (menuName == "midi_device")
		{
			// refresh device list
			menu->clear();
            
            // TODO this should not be the interface.
            // instead return a list.
			SoundplaneMIDIOutput& outs = mpSoundplaneModel->getMIDIOutput();
			outs.findMIDIDevices ();
			const std::vector<std::string>& devices = outs.getDeviceList();
			menu->addItems(devices);
		}
		else if (menuName == "osc_service_name")
		{
			menu->clear();
			const std::vector<std::string>& services = mpSoundplaneModel->getServicesList();
			menu->addItems(services);
		}
		else if (menuName == "zone_preset")
		{
			menu->clear();

			// set up built-in zone maps
			menu->addItem("chromatic");
			menu->addItem("rows in fourths");
			menu->addItem("rows in octaves");
			menu->addSeparator();
			
			// add zone presets from disk
			mZoneMenuStartItems = menu->getSize();
			menu->appendMenu(mpSoundplaneModel->getZonePresetsCollection().buildMenu());
		}
		
		// find instigator widget and show menu beside it
		MLWidget* pInstigator = mpSoundplaneView->getWidget(instigatorName);
		if(pInstigator != nullptr)
		{
			Component* pInstComp = pInstigator->getComponent();
			if(pInstComp)
			{
                const int u = pInstigator->getWidgetGridUnitSize();
                int height = ((float)u)*0.35f;
				height = ml::clamp(height, 12, 128);
				JuceMenuPtr juceMenu = menu->getJuceMenu();
				juceMenu->showMenuAsync (PopupMenu::Options().withTargetComponent(pInstComp).withStandardItemHeight(height),
										 ModalCallbackFunction::withParam(menuItemChosenCallback,
																		  WeakReference<SoundplaneController>(this), menuName)
										 );
			}
		}
	}
}

void SoundplaneController::menuItemChosen(ml::Symbol menuName, int result)
{
	if (result > 0)
	{
		if (menuName == "zone_preset")
		{
			doZonePresetMenu(result);
		}
		else if (menuName == "osc_service_name")
		{
			doOSCServicesMenu(result);
		}
		else
		{
			MLMenu* menu = findMenuByName(menuName);
			if (menu != nullptr)
			{
				// TODO getMenuItemPath returns Text
				const std::string fullName = menu->getMenuItemPath(result);
				mpSoundplaneModel->setProperty(menuName, ml::TextFragment(fullName.c_str()));				
			}
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
    std::string zoneStr;
	std::string fullName ("error: preset not found!");
	MLMenuPtr menu = mMenuMap["zone_preset"];
	
	// TODO text item paths
	std::string itemStr = menu->getMenuItemPath(result);
	mpSoundplaneModel->setPropertyImmediate("zone_preset", itemStr.c_str());            
}

void SoundplaneController::doOSCServicesMenu(int result)
{    
	if(!mpSoundplaneModel) return;
	std::string fullName ("OSC service not found.");

	if(result > 0)
	{
		fullName = mMenuMap["osc_service_name"]->getMenuItemPath(result);	
	}

	mpSoundplaneModel->setPropertyImmediate("osc_service_name", fullName.c_str());
}

class SoundplaneSetupThread : public ThreadWithProgressWindow
{
public:
    SoundplaneSetupThread(SoundplaneModel* m, Component* parent)
        : ThreadWithProgressWindow (" ", true, true, 1000, "Cancel", parent),
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
	if(!mpSoundplaneModel) return;
	SoundplaneSetupThread demoThread(mpSoundplaneModel, getView());
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

