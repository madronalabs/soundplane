	
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneModel.h"
#include "ThreadUtility.h"
#include "MLProjectInfo.h"

static const ml::Text kOSCDefaultStr("default");
const char *kUDPType      =   "_osc._udp";
const char *kLocalDotDomain   =   "local.";

const int kModelDefaultCarriersSize = 40;
const unsigned char kModelDefaultCarriers[kModelDefaultCarriersSize] =
{
	// 40 default carriers.  avoiding 32 (gets aliasing from 16)
	3, 4, 5, 6, 7,
	8, 9, 10, 11, 12,
	13, 14, 15, 16, 17, 
	18, 19, 20, 21, 22, 
	23, 24, 25, 26, 27, 
	28, 29, 30, 31, 33, 
	34, 35, 36, 37, 38, 
	39, 40, 41, 42, 43
};

// make one of the possible standard carrier sets, skipping a range of carriers out of the
// middle of the 40 defaults.
//
static const int kStandardCarrierSets = 16;
static void makeStandardCarrierSet(SoundplaneDriver::Carriers &carriers, int set)
{
	int startOffset = 2;
	int skipSize = 2;
	int gapSize = 4;
	int gapStart = set*skipSize + startOffset;
	carriers[0] = carriers[1] = 0;
	for(int i=startOffset; i<gapStart; ++i)
	{
		carriers[i] = kModelDefaultCarriers[i];
	}
	for(int i=gapStart; i<kSoundplaneNumCarriers; ++i)
	{
		carriers[i] = kModelDefaultCarriers[i + gapSize];
	}
}

void touchArrayToFrame(TouchTracker::TouchArray* pArray, MLSignal* pFrame)
{
	// get references for syntax
	TouchTracker::TouchArray& array = *pArray;
	MLSignal& frame = *pFrame;
	
	for(int i = 0; i < TouchTracker::kMaxTouches; ++i)
	{
		TouchTracker::Touch t = array[i];
		frame(xColumn, i) = t.x;
		frame(yColumn, i) = t.y;
		frame(zColumn, i) = t.z;
		frame(dzColumn, i) = t.dz;
		frame(ageColumn, i) = t.age;
	}	
}

MLSignal sensorFrameToSignal(const SensorFrame &f)
{
	MLSignal out(SensorGeometry::width, SensorGeometry::height);
    for(int j = 0; j < SensorGeometry::height; ++j)
    {
        const float* srcStart = f.data() + SensorGeometry::width*j;
        const float* srcEnd = srcStart + SensorGeometry::width;
        std::copy(srcStart, srcEnd, out.getBuffer() + out.row(j));
    }
	return out;
}

SensorFrame signalToSensorFrame(const MLSignal& in)
{
	SensorFrame out;
    for(int j = 0; j < SensorGeometry::height; ++j)
    {
        const float* srcStart = in.getConstBuffer() + (j << in.getWidthBits());
        const float* srcEnd = srcStart + in.getWidth();
        std::copy(srcStart, srcEnd, out.data() + SensorGeometry::width*j);
    }
	return out;
}

// --------------------------------------------------------------------------------
//
#pragma mark SoundplaneModel

SoundplaneModel::SoundplaneModel() :
	mOutputEnabled(false),
	mSurface(SensorGeometry::width, SensorGeometry::height),
	mRawSignal(SensorGeometry::width, SensorGeometry::height),
	mCalibratedSignal(SensorGeometry::width, SensorGeometry::height),
	mSmoothedSignal(SensorGeometry::width, SensorGeometry::height),
	mCalibrating(false),
	mSelectingCarriers(false),
	mDynamicCarriers(true),
	mHasCalibration(false),
	mZoneMap(kSoundplaneAKeyWidth, kSoundplaneAKeyHeight),
	mHistoryCtr(0),
	mCarrierMaskDirty(false),
	mNeedsCarriersSet(false),
	mNeedsCalibrate(false),
	mLastInfrequentTaskTime(0),
	mCarriersMask(0xFFFFFFFF),
	mDoOverrideCarriers(false),
	mKymaIsConnected(0),
	mKymaMode(false)
{	
	mpDriver = SoundplaneDriver::create(*this);

	for(int i=0; i<kSoundplaneMaxTouches; ++i)
	{
		mCurrentKeyX[i] = -1;
		mCurrentKeyY[i] = -1;
	}

	// setup default carriers in case there are no saved carriers
	for (int car=0; car<kSoundplaneNumCarriers; ++car)
	{
		mCarriers[car] = kModelDefaultCarriers[car];
	}

    clearZones();
	setAllPropertiesToDefaults();
	
	// setup OSC default
	setProperty("osc_service_name", kOSCDefaultStr);

	// start Browsing OSC services
	mServiceNames.clear();
	mServices.clear();
	mServices.push_back(std::string(kOSCDefaultStr.getText()));
	Browse(kLocalDotDomain, kUDPType);
	MLConsole() << "SoundplaneModel: listening for OSC on port " << kDefaultUDPReceivePort << "...\n";
	listenToOSC(kDefaultUDPReceivePort);
	
	mMIDIOutput.initialize();
	addListener(&mMIDIOutput);
	addListener(&mOSCOutput);
	
	mTouchFrame.setDims(kSoundplaneTouchWidth, TouchTracker::kMaxTouches);
	mTouchHistory.setDims(kSoundplaneTouchWidth, TouchTracker::kMaxTouches, kSoundplaneHistorySize);
	
	// make zone presets collection
	File zoneDir = getDefaultFileLocation(kPresetFiles, MLProjectInfo::makerName, MLProjectInfo::projectName).getChildFile("ZonePresets");
	debug() << "LOOKING for zones in " << zoneDir.getFileName() << "\n";
	mZonePresets = std::unique_ptr<MLFileCollection>(new MLFileCollection("zone_preset", zoneDir, "json"));
	mZonePresets->processFilesImmediate();
	//mZonePresets->dump();
    
    // now that the driver is active, start polling for changes in properties
    mTerminating = false;
    
    startModelTimer();
    
    mSensorFrameQueue = std::unique_ptr< Queue<SensorFrame> >(new Queue<SensorFrame>(kSensorFrameQueueSize));
    
    mProcessThread = std::thread(&SoundplaneModel::processThread, this);
    SetPriorityRealtimeAudio(mProcessThread.native_handle());
    
    mpDriver->start();
}

SoundplaneModel::~SoundplaneModel()
{
    // signal threads to shut down
    mTerminating = true;
    
    if (mProcessThread.joinable())
    {
        mProcessThread.join();
        printf("SoundplaneModel: mProcessThread terminated.\n");
    }
    
    listenToOSC(0);
    
    mpDriver = nullptr;
}

void SoundplaneModel::onStartup()
{
	// get serial number and auto calibrate noise on sync detect
	const unsigned long instrumentModel = 1; // Soundplane A
    mOSCOutput.setSerialNumber((instrumentModel << 16) | mpDriver->getSerialNumber());
    
 	// connected but not calibrated -- disable output.
    enableOutput(false);
    // output will be enabled at end of calibration.
    mNeedsCalibrate = true;
 }

// we need to return as quickly as possible from driver callback.
// just put the new frame in the queue.
void SoundplaneModel::onFrame(const SensorFrame& frame)
{
    mSensorFrameQueue->push(frame);
}

void SoundplaneModel::onError(int error, const char* errStr)
{
    switch(error)
    {
        case kDevDataDiffTooLarge:
            MLConsole() << "error: frame difference too large: " << errStr << "\n";
            beginCalibrate();
            break;
        case kDevGapInSequence:
            if(mVerbose)
            {
                MLConsole() << "note: gap in sequence " << errStr << "\n";
            }
            break;
        case kDevReset:
            // a reset is a bad thing but should only happen when we are switching apps
            if(mVerbose)
            {
                MLConsole() << "isoch stalled, resetting " << errStr << "\n";
            }
            break;
    }
}

void SoundplaneModel::onClose()
{
    enableOutput(false);
}

void SoundplaneModel::process()
{
    if (mSensorFrameQueue->pop(mSensorFrame))
    {
        mSurface = sensorFrameToSignal(mSensorFrame);
        
        // store surface for raw output
        {
            std::lock_guard<std::mutex> lock(mRawSignalMutex);
            mRawSignal.copy(mSurface);
        }
        
        if (mCalibrating)
        {
            mStats.accumulate(mSensorFrame);
            if (mStats.getCount() >= kSoundplaneCalibrateSize)
            {
                endCalibrate();
            }
        }
        else if (mSelectingCarriers)
        {
            mStats.accumulate(mSensorFrame);
            
            if (mStats.getCount() >= kSoundplaneCalibrateSize)
            {
                nextSelectCarriersStep();
            }
        }
        else if(mOutputEnabled)
        {
            if (mHasCalibration)
            {
                SensorFrame calibratedFrame = subtract(multiply(mSensorFrame, mCalibrateMeanInv), 1.0f);
                
                // store calibrated output
                // TODO less often! only needed for display
                {
                    std::lock_guard<std::mutex> lock(mCalibratedSignalMutex);
                    mCalibratedSignal = sensorFrameToSignal(calibratedFrame);
                }
                
                trackTouches(calibratedFrame);
                sendTouchDataToZones();
            }
        }
    }
}

void SoundplaneModel::doPropertyChangeAction(ml::Symbol p, const MLProperty & newVal)
{
	// debug() << "SoundplaneModel::doPropertyChangeAction: " << p << " -> " << newVal << "\n";

	int propertyType = newVal.getType();
	switch(propertyType)
	{
		case MLProperty::kFloatProperty:
		{
			float v = newVal.getFloatValue();
			if (ml::textUtils::stripFinalNumber(p) == ml::Symbol("carrier_toggle"))
			{
				// toggles changed -- mute carriers
				unsigned long mask = 0;
				for(int i=0; i<32; ++i)
				{
					ml::Symbol tSym = ml::textUtils::addFinalNumber(ml::Symbol("carrier_toggle"), i);
					bool on = (int)(getFloatProperty(tSym));
					mask = mask | (on << i);
				}

				mCarriersMask = mask;
				mCarrierMaskDirty = true; // trigger carriers set in a second or so
			}

			else if (p == "all_toggle")
			{
				bool on = (bool)(v);
				for(int i=0; i<32; ++i)
				{
					ml::Symbol tSym = ml::textUtils::addFinalNumber(ml::Symbol("carrier_toggle"), i);
					setProperty(tSym, on);
				}
				mCarriersMask = on ? ~0 : 0;
				mCarrierMaskDirty = true; // trigger carriers set in a second or so
			}
			else if (p == "max_touches")
			{
				mMaxTouches = v;
				mMIDIOutput.setMaxTouches(v);
				mOSCOutput.setMaxTouches(v);
			}
			else if (p == "lopass_z")
			{
				mTracker.setLopassZ(v);
			}
			else if (p == "z_thresh")
			{
				mTracker.setThresh(v);
			}
			else if (p == "snap")
			{
				sendParametersToZones();
			}
			else if (p == "vibrato")
			{
				sendParametersToZones();
			}
			else if (p == "lock")
			{
				sendParametersToZones();
			}
			else if (p == "data_freq_midi")
			{
				// TODO attribute
				mMIDIOutput.setDataFreq(v);
			}
			else if (p == "data_freq_osc")
			{
				// TODO attribute
				mOSCOutput.setDataFreq(v);
			}
			else if (p == "midi_active")
			{
				mMIDIOutput.setActive(bool(v));
			}
			else if (p == "midi_mpe")
			{
				mMIDIOutput.setMPE(bool(v));
			}
			else if (p == "midi_mpe_extended")
			{
				mMIDIOutput.setMPEExtended(bool(v));
			}
			else if (p == "midi_channel")
			{
				mMIDIOutput.setStartChannel(int(v));
			}
			else if (p == "midi_pressure_active")
			{
				mMIDIOutput.setPressureActive(bool(v));
			}
			else if (p == "osc_active")
			{
				bool b = v;
				mOSCOutput.setActive(b);
			}
			else if (p == "osc_send_matrix")
			{
				bool b = v;
				mSendMatrixData = b;
			}
			else if (p == "quantize")
			{
				sendParametersToZones();
			}
			else if (p == "rotate")
			{
				bool b = v;
				mTracker.setRotate(b);
			}
			else if (p == "glissando")
			{
				mMIDIOutput.setGlissando(bool(v));
				sendParametersToZones();
			}
			else if (p == "hysteresis")
			{
				mMIDIOutput.setHysteresis(v);
				sendParametersToZones();
			}
			else if (p == "transpose")
			{
				sendParametersToZones();
			}
            else if (p == "bend_range")
            {
                mMIDIOutput.setBendRange(v);
                sendParametersToZones();
            }
            
            else if (p == "verbose")
            {
                bool b = v;
                mVerbose = b;
            }
            
			/*
			 // MLTEST no manual connect
			else if (p == "kyma")
			{
				bool b = v;
				
				// MLTEST
				MLConsole() << "SoundplaneModel: kyma active " << b << "\n";
				
				if(b)
				{
					MLConsole() << "     listening for OSC on port " << kDefaultUDPReceivePort << "...\n";
				}
				
				listenToOSC(b ? kDefaultUDPReceivePort : 0);
			}
			*/
			
			else if (p == "override_carriers")
			{
				bool b = v;				
				mDoOverrideCarriers = b;
		//		mNeedsCarriersSet = true;
			}
			else if (p == "override_carrier_set")
			{
				makeStandardCarrierSet(mOverrideCarriers, v);
		//		mNeedsCarriersSet = true;
			}
		}
		break;
		case MLProperty::kTextProperty:
		{
			// TODO clean up, use text for everything
			ml::Text strText = newVal.getTextValue();
			std::string str (strText.getText());
			
			if(p == "osc_service_name")
			{
				if(strText == ml::Text(kOSCDefaultStr))
				{
					mOSCOutput.connect();
				}
				else
				{
					// resolve service for named port
					Resolve(kLocalDotDomain, kUDPType, str.c_str());
				}
			}
			if (p == "viewmode")
			{
				// nothing to do for Model
			}
			else if (p == "midi_device")
			{
				mMIDIOutput.setDevice(str);
			}
			else if (p == "zone_JSON")
			{
				loadZonesFromString(str);
			}
			else if (p == "zone_preset")
			{
				// look for built in zone map names.
				if(str == "chromatic")
				{
					setProperty("zone_JSON", (SoundplaneBinaryData::chromatic_json));
				}
				else if(str == "rows in fourths")
				{
					setProperty("zone_JSON", (SoundplaneBinaryData::rows_in_fourths_json));
				}
				else if(str == "rows in octaves")
				{
					setProperty("zone_JSON", (SoundplaneBinaryData::rows_in_octaves_json));
				}
				// if not built in, load a zone map file.
				else
				{
					const MLFile& f = mZonePresets->getFileByPath(str);
					if(f.exists())
					{
						File zoneFile = f.getJuceFile();
						String stateStr(zoneFile.loadFileAsString());
						setPropertyImmediate("zone_JSON", (stateStr.toUTF8()));
					}
				}
			}
		}
		break;
		case MLProperty::kSignalProperty:
		{
			const MLSignal& sig = newVal.getSignalValue();
			if(p == ml::Symbol("carriers"))
			{
				// get carriers from signal
				assert(sig.getSize() == kSoundplaneNumCarriers);
				for(int i=0; i<kSoundplaneNumCarriers; ++i)
				{
					if(mCarriers[i] != sig[i])
					{
						mCarriers[i] = sig[i];
                        mNeedsCarriersSet = true;
					}
				}
			}
		}
			break;
		default:
			break;
	}
}

void SoundplaneModel::setAllPropertiesToDefaults()
{
	// parameter defaults and creation
	setProperty("max_touches", 4);
	setProperty("lopass_z", 100.);

	setProperty("z_thresh", 0.05);
	setProperty("z_scale", 1.);
	setProperty("z_curve", 0.5);
	setProperty("display_scale", 1.);

	setProperty("pairs", 0.);
	setProperty("quantize", 1.);
	setProperty("lock", 0.);
	setProperty("abs_rel", 0.);
	setProperty("snap", 250.);
	setProperty("vibrato", 0.5);

	setProperty("midi_active", 0);
	setProperty("midi_mpe", 1);
	setProperty("midi_mpe_extended", 0);
	setProperty("midi_channel", 1);
	setProperty("data_freq_midi", 250.);

	setProperty("kyma_poll", 0);

	setProperty("osc_active", 1);
	setProperty("osc_raw", 0);
	setProperty("data_freq_osc", 250.);

	setProperty("bend_range", 48);
	setProperty("transpose", 0);
	setProperty("bg_filter", 0.05);

	setProperty("hysteresis", 0.5);
	setProperty("lo_thresh", 0.1);

	// menu param defaults
	setProperty("viewmode", "calibrated");

    // preset menu defaults (TODO use first choices?)
	setProperty("zone_preset", "rows in fourths");
	setProperty("touch_preset", "touch default");

	setProperty("view_page", 0);

	for(int i=0; i<32; ++i)
	{
		setProperty(ml::textUtils::addFinalNumber("carrier_toggle", i), 1);
	}
}

// Process incoming OSC.  Used for Kyma communication.
//
void SoundplaneModel::ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint)
{
	// MLTEST - kyma debugging
	char endpointStr[256];
	remoteEndpoint.AddressAndPortAsString(endpointStr);
	MLConsole() << "OSC: " << m.AddressPattern() << " from " << endpointStr << "\n";
	
	osc::ReceivedMessageArgumentStream args = m.ArgumentStream();
	osc::int32 a1;
	try
	{
		if( std::strcmp( m.AddressPattern(), "/osc/response_from" ) == 0 )
		{
			args >> a1 >> osc::EndMessage;
			
			// MLTEST
			MLConsole() << " arg = " << a1 << "\n";

			mKymaIsConnected = true;
		}
		else if (std::strcmp( m.AddressPattern(), "/osc/notify/midi/Soundplane" ) == 0 )
		{
			args >> a1 >> osc::EndMessage;

			// MLTEST
			MLConsole() << " arg = " << a1 << "\n";
						
			// set voice count to a1
			int newTouches = ml::clamp((int)a1, 0, kSoundplaneMaxTouches);

			// Kyma is sending 0 sometimes, which there is probably
			// no reason to respond to
			if(newTouches > 0)
			{
				setProperty("max_touches", newTouches);
			}
		}
	}
	catch( osc::Exception& e )
	{
		MLConsole() << "oscpack error while parsing message: "
			<< m.AddressPattern() << ": " << e.what() << "\n";
	}
}

void SoundplaneModel::ProcessBundle(const osc::ReceivedBundle &b, const IpEndpointName& remoteEndpoint)
{

}

// called asynchronously after Resolve() when host and port are found by the resolver.
// requires that PollNetServices() be called periodically.
//
void SoundplaneModel::didResolveAddress(NetService *pNetService)
{
	const std::string& serviceName = pNetService->getName();
	const std::string& hostName = pNetService->getHostName();
//	const char* hostNameStr = hostName.c_str();
	int port = pNetService->getPort();

	// MLTEST
	MLConsole() << "SoundplaneModel::didResolveAddress: RESOLVED net service to " << hostName << ", service " << serviceName << ", port " << port << "\n";

	// if we are talking to a kyma, set kyma mode
	static const char* kymaStr = "beslime";
	int len = strlen(kymaStr);
	bool isProbablyKyma = !strncmp(serviceName.c_str(), kymaStr, len);
	
	if(isProbablyKyma)
	{
		MLConsole() << "    setting Kyma mode.\n";

		mOSCOutput.setKymaMode(true);
		mOSCOutput.setKymaPort(port);
		mMIDIOutput.setKymaMode(true);
	}
	
	mOSCOutput.connect();
}

void SoundplaneModel::formatServiceName(const std::string& inName, std::string& outName)
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

void SoundplaneModel::refreshServices()
{
	mServiceNames.clear();
	std::vector<std::string>::iterator it;
	for(it = mServices.begin(); it != mServices.end(); it++)
	{
		const std::string& serviceName = *it;
		mServiceNames.push_back(serviceName);
	}
}

const std::vector<std::string>& SoundplaneModel::getServicesList()
{
	return mServiceNames;
}

void SoundplaneModel::initialize()
{
}

int SoundplaneModel::getClientState(void)
{
	return mKymaIsConnected;
}

int SoundplaneModel::getDeviceState(void)
{
	if(!mpDriver.get())
	{
		return kNoDevice;
	}
		
	return mpDriver->getDeviceState();
}

// get a string that explains what Soundplane hardware and firmware and client versions are running.
const char* SoundplaneModel::getHardwareStr()
{
	long v;
	unsigned char a, b, c;
	std::string serial_number;
	switch(getDeviceState())
	{
		case kNoDevice:
			snprintf(mHardwareStr, miscStrSize, "no device");
			break;
		case kDeviceConnected:
		case kDeviceHasIsochSync:
			serial_number = mpDriver->getSerialNumberString();
			v = mpDriver->getFirmwareVersion();
			a = v >> 8 & 0x0F;
			b = v >> 4 & 0x0F,
			c = v & 0x0F;
			snprintf(mHardwareStr, miscStrSize, "%s #%s, firmware %d.%d.%d", kSoundplaneAName, serial_number.c_str(), a, b, c);
			break;
		default:
			snprintf(mHardwareStr, miscStrSize, "?");
			break;
	}
	return mHardwareStr;
}

// get the string to report general connection status.
const char* SoundplaneModel::getStatusStr()
{
	switch(getDeviceState())
	{
		case kNoDevice:
			snprintf(mStatusStr, miscStrSize, "waiting for Soundplane...");
			break;

		case kDeviceConnected:
			snprintf(mStatusStr, miscStrSize, "waiting for isochronous data...");
			break;

		case kDeviceHasIsochSync:
			snprintf(mStatusStr, miscStrSize, "synchronized");
			break;

		default:
			snprintf(mStatusStr, miscStrSize, "unknown status.");
			break;
	}
	return mStatusStr;
}

// get the string to report a specific client connection above and beyond the usual
// OSC / MIDI communication.
const char* SoundplaneModel::getClientStr()
{
	switch(mKymaIsConnected)
	{
		case 0:
			snprintf(mClientStr, miscStrSize, "");
			break;

		case 1:
			snprintf(mClientStr, miscStrSize, "connected to Kyma");
			break;

		default:
			snprintf(mClientStr, miscStrSize, "?");
			break;
	}
	return mClientStr;
}

// remove all zones from the zone list.
void SoundplaneModel::clearZones()
{
    const ScopedLock lock (mZoneLock);
    mZones.clear();
    mZoneMap.fill(-1);
}

// add a zone to the zone list and color in its boundary on the map.
void SoundplaneModel::addZone(ZonePtr pz)
{
    const ScopedLock lock (mZoneLock);
    // TODO prevent overlapping zones
    int zoneIdx = mZones.size();
    if(zoneIdx < kSoundplaneAMaxZones)
    {
        pz->setZoneID(zoneIdx);
        mZones.push_back(pz);
        MLRect b(pz->getBounds());
        int x = b.x();
        int y = b.y();
        int w = b.width();
        int h = b.height();

        for(int j=y; j < y + h; ++j)
        {
            for(int i=x; i < x + w; ++i)
            {
                mZoneMap(i, j) = zoneIdx;
            }
        }
    }
    else
    {
        MLConsole() << "SoundplaneModel::addZone: out of zones!\n";
    }
}

void SoundplaneModel::loadZonesFromString(const std::string& zoneStr)
{
    clearZones();
    cJSON* root = cJSON_Parse(zoneStr.c_str());
    if(!root)
    {
        MLConsole() << "zone file parse failed!\n";
        const char* errStr = cJSON_GetErrorPtr();
        MLConsole() << "    error at: " << errStr << "\n";
        return;
    }
    cJSON* pNode = root->child;
    while(pNode)
    {
        if(!strcmp(pNode->string, "zone"))
        {
            Zone* pz = new Zone(mListeners);
            cJSON* pZoneType = cJSON_GetObjectItem(pNode, "type");
            if(pZoneType)
            {
                // get zone type and type specific attributes
                ml::Symbol typeSym(pZoneType->valuestring);
                int zoneTypeNum = Zone::symbolToZoneType(typeSym);
                if(zoneTypeNum >= 0)
                {
                    pz->mType = zoneTypeNum;
                }
                else
                {
                    MLConsole() << "Unknown type " << typeSym << " for zone!\n";
                }
            }
            else
            {
                MLConsole() << "No type for zone!\n";
            }

            // get zone rect
            cJSON* pZoneRect = cJSON_GetObjectItem(pNode, "rect");
            if(pZoneRect)
            {
                int size = cJSON_GetArraySize(pZoneRect);
                if(size == 4)
                {
                    int x = cJSON_GetArrayItem(pZoneRect, 0)->valueint;
                    int y = cJSON_GetArrayItem(pZoneRect, 1)->valueint;
                    int w = cJSON_GetArrayItem(pZoneRect, 2)->valueint;
                    int h = cJSON_GetArrayItem(pZoneRect, 3)->valueint;
                    pz->setBounds(MLRect(x, y, w, h));
                }
                else
                {
                    MLConsole() << "Bad rect for zone!\n";
                }
            }
            else
            {
                MLConsole() << "No rect for zone\n";
            }

            pz->mName = TextFragment(getJSONString(pNode, "name"));
            pz->mStartNote = getJSONInt(pNode, "note");
            pz->mOffset = getJSONInt(pNode, "offset");
            pz->mControllerNum1 = getJSONInt(pNode, "ctrl1");
            pz->mControllerNum2 = getJSONInt(pNode, "ctrl2");
            pz->mControllerNum3 = getJSONInt(pNode, "ctrl3");

            addZone(ZonePtr(pz));
            
           //  mZoneMap.dump(mZoneMap.getBoundsRect());
        }
		pNode = pNode->next;
    }
    sendParametersToZones();
}

void SoundplaneModel::clearTouchData()
{
	for(int i=0; i<TouchTracker::kMaxTouches; ++i)
	{
		mTouchArray[i] = TouchTracker::Touch();
	}
}

// copy relevant parameters from Model to zones
void SoundplaneModel::sendParametersToZones()
{
    // TODO zones should have parameters (really attributes) too, so they can be inspected.
    int zones = mZones.size();
	const float v = getFloatProperty("vibrato");
    const float h = getFloatProperty("hysteresis");
    bool q = getFloatProperty("quantize");
    bool nl = getFloatProperty("lock");
    int t = getFloatProperty("transpose");
    float sf = getFloatProperty("snap");

    for(int i=0; i<zones; ++i)
	{
        mZones[i]->mVibrato = v;
        mZones[i]->mHysteresis = h;
        mZones[i]->mQuantize = q;
        mZones[i]->mNoteLock = nl;
        mZones[i]->mTranspose = t;
        mZones[i]->setSnapFreq(sf);
    }
}

// c over [0 - 1] fades response from sqrt(x) -> x -> x^2
//
float responseCurve(float x, float c)
{
	float y;
	if(c < 0.5f)
	{
		y = lerp(x*x, x, c*2.f);
	}
	else
	{
		y = lerp(x, sqrtf(x), c*2.f - 1.f);
	}
	return y;
}

void SoundplaneModel::scaleTouchPressureData()
{
	const float zscale = getFloatProperty("z_scale");
	const float zcurve = getFloatProperty("z_curve");
	const float dzScale = 0.125f;

	for(int i=0; i<TouchTracker::kMaxTouches; ++i)
	{
		float z = mTouchArray[i].z;
		z *= zscale;
		z = ml::clamp(z, 0.f, 4.f);
		z = responseCurve(z, zcurve);
		mTouchArray[i].z = z;
		
		// for note-ons, use same z scale controls as pressure
		float dz = mTouchArray[i].dz*dzScale;
		dz *= zscale;		
		dz = ml::clamp(dz, 0.f, 1.f);
		dz = responseCurve(dz, zcurve);
		mTouchArray[i].dz = dz;
	}
}

// send raw touches to zones in order to generate note and controller events.
void SoundplaneModel::sendTouchDataToZones()
{
	float x, y, z, dz;
	int age;

    
    // someting is wrong
    
	const int maxTouches = getFloatProperty("max_touches");
	const float hysteresis = getFloatProperty("hysteresis");

	MLRange yRange(0.05, 0.8);
	yRange.convertTo(MLRange(0., 1.));

    for(int i=0; i<maxTouches; ++i)
	{		
		x = mTouchArray[i].x;
		y = mTouchArray[i].y;
		z = mTouchArray[i].z;
		dz = mTouchArray[i].dz;
		age = mTouchArray[i].age;
		
		if(age > 0)
		{
			// get fractional key grid position (Soundplane A)
			Vec2 keyXY (x, y);

            // get integer key
			int ix = (int)x;
			int iy = (int)y;
			
            // apply hysteresis to raw position to get current key
            // hysteresis: make it harder to move out of current key
            if(age == 1)
            {
                mCurrentKeyX[i] = ix;
                mCurrentKeyY[i] = iy;
            }
            else
            {
                float hystWidth = hysteresis*0.25f;
                MLRect currentKeyRect(mCurrentKeyX[i], mCurrentKeyY[i], 1, 1);
                currentKeyRect.expand(hystWidth);
                if(!currentKeyRect.contains(keyXY))
                {
                    mCurrentKeyX[i] = ix;
                    mCurrentKeyY[i] = iy;
                }
            }

            // send index, xyz, dz to zone
            int zoneIdx = mZoneMap(mCurrentKeyX[i], mCurrentKeyY[i]);
            if(zoneIdx >= 0)
            {
                ZonePtr zone = mZones[zoneIdx];
                zone->addTouchToFrame(i, x, y, mCurrentKeyX[i], mCurrentKeyY[i], z, dz);
            }
        }
	}

    // tell listeners we are starting this frame.
    sendMessageToListeners(SoundplaneDataMessage{"start_frame"});

    // process note offs for each zone
	// this happens before processTouches() to allow voices to be freed
    int zones = mZones.size();
	std::vector<bool> freedTouches;
	freedTouches.resize(kSoundplaneMaxTouches);
	// generates freedTouches array, syntax should be different
    for(int i=0; i<zones; ++i)
	{
        if(mZones[i]->getType() == kNoteRow) // MLTEST, this looks like a fix
        {
            sendMessageToListeners(mZones[i]->processTouchesNoteOffs(freedTouches));
        }
    }

    // process touches for each zone
	for(int i=0; i<zones; ++i)
	{
        sendMessageToListeners(mZones[i]->processTouches(freedTouches));
    }

    // send optional calibrated matrix
    /*
    if(mSendMatrixData)
    {		
		MLSignal calibratedPressure = getCalibratedSignal();
		if(calibratedPressure.getHeight() == SensorGeometry::height)
		{
			
			mMessage.mType = ml::Symbol("matrix");
			for(int j = 0; j < SensorGeometry::height; ++j)
			{
				for(int i = 0; i < SensorGeometry::width; ++i)
				{
					mMessage.mMatrix[j*SensorGeometry::width + i] = calibratedPressure(i, j); // MLTEST
				}
			}
			sendMessageToListeners();
		}
    }*/
    
    // tell listeners we are done with this frame.
	sendMessageToListeners(SoundplaneDataMessage{"end_frame"});
}

void SoundplaneModel::sendMessageToListeners(const SoundplaneDataMessage m)
{
    if(m.mType)
    {
        for(SoundplaneListenerList::iterator it = mListeners.begin(); it != mListeners.end(); it++)
        if((*it)->isActive())
        {
            (*it)->processSoundplaneMessage(m);
        }
    }
}

void SoundplaneModel::trackTouches(const SensorFrame& frame)
{
	mTracker.process(frame, mMaxTouches, &mTouchArray, &mSmoothedFrame);
	mSmoothedSignal = sensorFrameToSignal(mSmoothedFrame);
	scaleTouchPressureData();

	// convert array of touches to Signal for display, history
	{
		std::lock_guard<std::mutex> lock(mTouchFrameMutex); 
		touchArrayToFrame(&mTouchArray, &mTouchFrame);
	}
	
	mHistoryCtr++;
	if (mHistoryCtr >= kSoundplaneHistorySize) mHistoryCtr = 0;
	mTouchHistory.setFrame(mHistoryCtr, mTouchFrame);
}

void SoundplaneModel::doInfrequentTasks()
{
	PollNetServices();
	mOSCOutput.doInfrequentTasks();
	mMIDIOutput.doInfrequentTasks();

	if (mCarrierMaskDirty)
	{
		enableCarriers(mCarriersMask);
	}
	else if (mNeedsCarriersSet)
	{
		mNeedsCarriersSet = false;
		if(mDoOverrideCarriers)
		{
			setCarriers(mOverrideCarriers);
		}
		else
		{
			setCarriers(mCarriers);
		}
        
		mNeedsCalibrate = true;
	}
    else if (mNeedsCalibrate && (!mSelectingCarriers))
	{
		mNeedsCalibrate = false;
		beginCalibrate();
	}
}

void SoundplaneModel::setDefaultCarriers()
{
	MLSignal cSig(kSoundplaneNumCarriers);
	for (int car=0; car<kSoundplaneNumCarriers; ++car)
	{
		cSig[car] = kModelDefaultCarriers[car];
	}
	setProperty("carriers", cSig);
}

void SoundplaneModel::setCarriers(const SoundplaneDriver::Carriers& c)
{
	enableOutput(false);
	mpDriver->setCarriers(c);
}

int SoundplaneModel::enableCarriers(unsigned long mask)
{
	mpDriver->enableCarriers(~mask);
	if (mask != mCarriersMask)
	{
		mCarriersMask = mask;
	}
	mCarrierMaskDirty = false;
	return 0;
}

void SoundplaneModel::dumpCarriers(const SoundplaneDriver::Carriers& carriers)
{
	debug() << "\n------------------\n";
	debug() << "carriers: \n";
	for(int i=0; i<kSoundplaneNumCarriers; ++i)
	{
		int c = carriers[i];
		debug() << i << ": " << c << " ["<< carrierToFrequency(c) << "Hz] \n";
	}
}

void SoundplaneModel::enableOutput(bool b)
{
	mOutputEnabled = b;
}

void SoundplaneModel::clear()
{
	mTracker.clear();
}

// --------------------------------------------------------------------------------
#pragma mark surface calibration

// using the current carriers, calibrate the surface by collecting data and
// calculating the mean rest value for each taxel.
//
void SoundplaneModel::beginCalibrate()
{
	if(getDeviceState() == kDeviceHasIsochSync)
	{
 		mStats.clear();
		mCalibrating = true;
	}
}

// called by process routine when enough samples have been collected.
//
void SoundplaneModel::endCalibrate()
{
	SensorFrame mean = clamp(mStats.mean(), 0.0001f, 1.f);	
	mCalibrateMeanInv = divide(fill(1.f), mean);
	mCalibrating = false;
	mHasCalibration = true;
	enableOutput(true);
}

float SoundplaneModel::getCalibrateProgress()
{
	return mStats.getCount() / (float)kSoundplaneCalibrateSize;
}

// --------------------------------------------------------------------------------
#pragma mark carrier selection

void SoundplaneModel::beginSelectCarriers()
{
	// each possible group of carrier frequencies is tested to see which
	// has the lowest overall noise.
	// each step collects kSoundplaneCalibrateSize frames of data.
	//
	if(getDeviceState() == kDeviceHasIsochSync)
	{
		mSelectCarriersStep = 0;
		mStats.clear();
		mSelectingCarriers = true;
		mTracker.clear();
		mMaxNoiseByCarrierSet.resize(kStandardCarrierSets);
		mMaxNoiseByCarrierSet.clear();
		mMaxNoiseFreqByCarrierSet.resize(kStandardCarrierSets);
		mMaxNoiseFreqByCarrierSet.clear();

		// setup first set of carrier frequencies
		MLConsole() << "testing carriers set " << mSelectCarriersStep << "...\n";
		makeStandardCarrierSet(mCarriers, mSelectCarriersStep);
		setCarriers(mCarriers);
	}
}

float SoundplaneModel::getSelectCarriersProgress()
{
	float p;
	if(mSelectingCarriers)
	{
		p = (float)mSelectCarriersStep / (float)kStandardCarrierSets;
	}
	else
	{
		p = 0.f;
	}
	return p;
}

void SoundplaneModel::nextSelectCarriersStep()
{
	// analyze calibration data just collected.
	SensorFrame mean = clamp(mStats.mean(), 0.0001f, 1.f);
	SensorFrame stdDev = mStats.standardDeviation();
	SensorFrame variation = divide(stdDev, mean);
	

	// find maximum noise in any column for this set.  This is the "badness" value
	// we use to compare carrier sets.
	float maxVar = 0;
	float maxVarFreq = 0;
	float variationSum;
	int startSkip = 2;
	
	for(int col = startSkip; col<kSoundplaneNumCarriers; ++col)
	{
		variationSum = 0;
		int carrier = mCarriers[col];
		float cFreq = carrierToFrequency(carrier);
		
		variationSum = getColumnSum(variation, col);
		if(variationSum > maxVar)
		{
			maxVar = variationSum;
			maxVarFreq = cFreq;
		}
	}

	mMaxNoiseByCarrierSet[mSelectCarriersStep] = maxVar;
	mMaxNoiseFreqByCarrierSet[mSelectCarriersStep] = maxVarFreq;

	MLConsole() << "max noise for set " << mSelectCarriersStep << ": " << maxVar << "(" << maxVarFreq << " Hz) \n";

	// set up next step.
	mSelectCarriersStep++;
	if (mSelectCarriersStep < kStandardCarrierSets)
	{
		// set next carrier frequencies to calibrate.
		MLConsole() << "testing carriers set " << mSelectCarriersStep << "...\n";
		makeStandardCarrierSet(mCarriers, mSelectCarriersStep);
		setCarriers(mCarriers);
	}
	else
	{
		endSelectCarriers();
	}
	
	// clear data
	mStats.clear();
}

void SoundplaneModel::endSelectCarriers()
{
	// get minimum of collected noise sums
	float minNoise = 99999.f;
	int minIdx = -1;
	MLConsole() << "------------------------------------------------\n";
	MLConsole() << "carrier select noise results:\n";
	for(int i=0; i<kStandardCarrierSets; ++i)
	{
		float n = mMaxNoiseByCarrierSet[i];
		float h = mMaxNoiseFreqByCarrierSet[i];
		MLConsole() << "set " << i << ": max noise " << n << "(" << h << " Hz)\n";
		if(n < minNoise)
		{
			minNoise = n;
			minIdx = i;
		}
	}

	// set that carrier group
	MLConsole() << "setting carriers set " << minIdx << "...\n";
	makeStandardCarrierSet(mCarriers, minIdx);
	setCarriers(mCarriers);

	// set chosen carriers as model parameter so they will be saved
	// this will trigger a recalibrate
	MLSignal cSig(kSoundplaneNumCarriers);
	for (int car=0; car<kSoundplaneNumCarriers; ++car)
	{
		cSig[car] = mCarriers[car];
	}
	setProperty("carriers", cSig);
	MLConsole() << "carrier select done.\n";
	
	mSelectingCarriers = false;
	mNeedsCalibrate = true;
}

void SoundplaneModel::processThread()
{
    std::chrono::time_point<std::chrono::system_clock> previous, now;
    previous = now = std::chrono::system_clock::now();
    while(!mTerminating)
    {
        process();
        mProcessCounter++;
        
        size_t queueSize = mSensorFrameQueue->elementsAvailable();
        if(queueSize > kMaxQueueSize)
        {
            kMaxQueueSize = queueSize;
        }
        
        if(mProcessCounter >= 1000)
        {
            if(mVerbose)
            {
                if(kMaxQueueSize >= kSensorFrameQueueSize)
                {
                    MLConsole() << "warning: input queue full \n";
                }
            }
            
            mProcessCounter = 0;
            kMaxQueueSize = 0;
        }
        
        // sleep, less than one frame interval
        std::this_thread::sleep_for(std::chrono::microseconds(500));
        
        // do infrequent tasks every second
        now = std::chrono::system_clock::now();
        int secondsInterval = std::chrono::duration_cast<std::chrono::seconds>(now - previous).count();
        if (secondsInterval >= 1)
        {
            previous = now;
            doInfrequentTasks();
        }
    }
}

