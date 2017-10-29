	
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneModel.h"

#include "pa_memorybarrier.h"

#include "InertSoundplaneDriver.h"
#include "TestSoundplaneDriver.h"

static const std::string kOSCDefaultStr("localhost:3123 (default)");
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


// --------------------------------------------------------------------------------
//
#pragma mark SoundplaneModel

SoundplaneModel::SoundplaneModel() :
	mOutputEnabled(false),
	mSurface(SensorGeometry::width, SensorGeometry::height),
	mRawSignal(SensorGeometry::width, SensorGeometry::height),
	mCalibratedSignal(SensorGeometry::width, SensorGeometry::height),
	mSmoothedSignal(SensorGeometry::width, SensorGeometry::height),
	mTesting(false),
	mCalibrating(false),
	mSelectingCarriers(false),
	mDynamicCarriers(true),
	mCalibrateSum(SensorGeometry::width, SensorGeometry::height),
	mCalibrateMean(SensorGeometry::width, SensorGeometry::height),
	mCalibrateMeanInv(SensorGeometry::width, SensorGeometry::height),
	mCalibrateStdDev(SensorGeometry::width, SensorGeometry::height),
	//
	mHasCalibration(false),
	//
	mZoneMap(kSoundplaneAKeyWidth, kSoundplaneAKeyHeight),

	mHistoryCtr(0),
	mTestCtr(0),

	mLastTimeDataWasSent(0),
	mZoneModeTemp(0),
	mCarrierMaskDirty(false),
	mNeedsCarriersSet(false),
	mNeedsCalibrate(true),
	mLastInfrequentTaskTime(0),
	mCarriersMask(0xFFFFFFFF),
	mDoOverrideCarriers(false),
	//
	//mOSCListenerThread(0),
	//mpUDPReceiveSocket(nullptr),
	mTest(0),
	mKymaIsConnected(0),
	mKymaMode(false),
	mShuttingDown(0)
{
	// setup geometry
	mSurfaceWidthInv = 1.f / (float)mSurface.getWidth();
	mSurfaceHeightInv = 1.f / (float)mSurface.getHeight();
	
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
	mServices.push_back(kOSCDefaultStr);
	Browse(kLocalDotDomain, kUDPType);
	
	MLConsole() << "SoundplaneModel: listening for OSC on port " << kDefaultUDPReceivePort << "...\n";
	listenToOSC(kDefaultUDPReceivePort);
	
	startModelTimer();
	
	mMIDIOutput.initialize();
	addListener(&mMIDIOutput);
	addListener(&mOSCOutput);
	
	mpDriver = SoundplaneDriver::create(this);
	
	mTaskThread = std::thread(&SoundplaneModel::taskThread, this);
	
	// TODO mem err handling
	if (!mCalibrateData.setDims(SensorGeometry::width, SensorGeometry::height, kSoundplaneCalibrateSize))
	{
		MLConsole() << "SoundplaneModel: out of memory!\n";
	}
	
	mTouchFrame.setDims(kSoundplaneTouchWidth, TouchTracker::kMaxTouches);
	
	mTouchHistory.setDims(kSoundplaneTouchWidth, TouchTracker::kMaxTouches, kSoundplaneHistorySize);
	
	// make zone presets collection
	File zoneDir = getDefaultFileLocation(kPresetFiles).getChildFile("ZonePresets");
	debug() << "LOOKING for zones in " << zoneDir.getFileName() << "\n";
	mZonePresets = MLFileCollectionPtr(new MLFileCollection("zone_preset", zoneDir, "json"));
	mZonePresets->processFilesImmediate();
	mZonePresets->dump();

}

SoundplaneModel::~SoundplaneModel()
{
	// signal threads to shut down and wait
	mShuttingDown = true;
	usleep(500*1000);
	
	if (mTaskThread.joinable())
	{
		mTaskThread.join();
		printf("SoundplaneModel: task thread terminated.\n");
	}
	
	// Ensure the SoundplaneDriver is torn down before anything else in this
	// object. This is important because otherwise there might be processing
	// thread callbacks that fly around too late.
	mpDriver.reset(new InertSoundplaneDriver());
	
	listenToOSC(0);	
}

void SoundplaneModel::doPropertyChangeAction(MLSymbol p, const MLProperty & newVal)
{
	// debug() << "SoundplaneModel::doPropertyChangeAction: " << p << " -> " << newVal << "\n";

	int propertyType = newVal.getType();
	switch(propertyType)
	{
		case MLProperty::kFloatProperty:
		{
			float v = newVal.getFloatValue();
			if (p.withoutFinalNumber() == MLSymbol("carrier_toggle"))
			{
				// toggles changed -- mute carriers
				unsigned long mask = 0;
				for(int i=0; i<32; ++i)
				{
					MLSymbol tSym = MLSymbol("carrier_toggle").withFinalNumber(i);
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
					MLSymbol tSym = MLSymbol("carrier_toggle").withFinalNumber(i);
					setProperty(tSym, on);
				}
				mCarriersMask = on ? ~0 : 0;
				mCarrierMaskDirty = true; // trigger carriers set in a second or so
			}
			else if (p == "max_touches")
			{
				debug() << "TOUCHES: " << v << "\n";
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
				bool b = v;
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
				mNeedsCarriersSet = true;
			}
			else if (p == "override_carrier_set")
			{
				makeStandardCarrierSet(mOverrideCarriers, v);
				mNeedsCarriersSet = true;
			}			
		}
		break;
		case MLProperty::kStringProperty:
		{
			const std::string& str = newVal.getStringValue();
			if(p == "osc_service_name")
			{
				if(str == "default")
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
					setProperty("zone_JSON", std::string(SoundplaneBinaryData::chromatic_json));
				}
				else if(str == "rows in fourths")
				{
					setProperty("zone_JSON", std::string(SoundplaneBinaryData::rows_in_fourths_json));
				}
				else if(str == "rows in octaves")
				{
					setProperty("zone_JSON", std::string(SoundplaneBinaryData::rows_in_octaves_json));
				}
				// if not built in, load a zone map file.
				else
				{
					const MLFile& f = mZonePresets->getFileByPath(str);
					if(f.exists())
					{
						File zoneFile = f.getJuceFile();
						String stateStr(zoneFile.loadFileAsString());
						setPropertyImmediate("zone_JSON", std::string(stateStr.toUTF8()));
					}
				}
			}
		}
		break;
		case MLProperty::kSignalProperty:
		{
			const MLSignal& sig = newVal.getSignalValue();
			if(p == MLSymbol("carriers"))
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
			if(p == MLSymbol("tracker_calibration"))
			{
//				mTracker.setCalibration(sig);
			}
			if(p == MLSymbol("tracker_normalize"))
			{
//				mTracker.setNormalizeMap(sig);
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
	setProperty("lopass_xy", 50.);
	setProperty("lopass_z", 50.);

	setProperty("z_thresh", 0.01);
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
		setProperty(MLSymbol("carrier_toggle").withFinalNumber(i), 1);
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
			int newTouches = clamp((int)a1, 0, kSoundplaneMaxTouches);

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

void SoundplaneModel::taskThread()
{
	std::chrono::time_point<std::chrono::system_clock> previous, now;
	previous = now = std::chrono::system_clock::now();
	while(!mShuttingDown)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		now = std::chrono::system_clock::now();
		int secondsInterval = std::chrono::duration_cast<std::chrono::seconds>(now - previous).count();		
		if (secondsInterval >= 1)
		{
			previous = now;
			doInfrequentTasks();
		}
	}
}

void SoundplaneModel::initialize()
{
}

int SoundplaneModel::getClientState(void)
{
	PaUtil_ReadMemoryBarrier();
	return mKymaIsConnected;
}

int SoundplaneModel::getDeviceState(void)
{
	return mpDriver->getDeviceState();
}

void SoundplaneModel::deviceStateChanged(SoundplaneDriver& driver, MLSoundplaneState s)
{
	unsigned long instrumentModel = 1; // Soundplane A

	PaUtil_WriteMemoryBarrier();

	switch(s)
	{
		case kNoDevice:
		break;
		case kDeviceConnected:
			// connected but not calibrated -- disable output.
			enableOutput(false);
		break;
		case kDeviceHasIsochSync:
			// get serial number and auto calibrate noise on sync detect
			mOSCOutput.setSerialNumber((instrumentModel << 16) | driver.getSerialNumber());
			// output will be enabled at end of calibration.
			mNeedsCalibrate = true;
		break;
			
		default:
		case kDeviceIsTerminating:
		break;
	}
}

#pragma mark 

MLSignal sensorFrameToSignal(const SensorFrame &f)
{
	MLSignal out(SensorGeometry::width, SensorGeometry::height);
	std::copy(f.data(), f.data() + SensorGeometry::elements, out.getBuffer());
	return out;
}

SensorFrame signalToSensorFrame(const MLSignal& in)
{
	SensorFrame out;
	std::copy(in.getConstBuffer(), in.getConstBuffer() + SensorGeometry::elements, out.data());
	return out;
}

void SoundplaneModel::receivedFrame(SoundplaneDriver& driver, const float* data, int size)
{
	// read from driver's ring buffer to incoming surface
	float* pSurfaceData = mSurface.getBuffer();
	memcpy(pSurfaceData, data, size * sizeof(float));

	// store surface for raw output
	{ 
		std::lock_guard<std::mutex> lock(mRawSignalMutex); 
		mRawSignal.copy(mSurface);
	}
	
	if (mCalibrating)
	{
		// copy surface to a frame of 3D calibration buffer
		mCalibrateData.setFrame(mCalibrateCount++, mSurface);
		if (mCalibrateCount >= kSoundplaneCalibrateSize)
		{
			endCalibrate();
		}
	}
	else if (mSelectingCarriers)
	{
		// copy surface to a frame of 3D calibration buffer
		mCalibrateData.setFrame(mCalibrateCount++, mSurface);
		if (mCalibrateCount >= kSoundplaneCalibrateSize)
		{
			nextSelectCarriersStep();
		}
	}
	else if(mOutputEnabled)
	{
		// scale incoming data to reference mCalibrateMean = 1.0 
		float in, cmeanInv;
		if (mHasCalibration)
		{
			for(int j=0; j<mSurface.getHeight(); ++j)
			{
				for(int i=0; i<mSurface.getWidth(); ++i)
				{
					// subtract calibrated zero
					in = mSurface(i, j);
					cmeanInv = mCalibrateMeanInv(i, j);
					mSurface(i, j) = ((in*cmeanInv) - 1.0f);
				}
			}
			{
				std::lock_guard<std::mutex> lock(mCalibratedSignalMutex);
				mCalibratedSignal = mSurface;
			}
			
			trackTouches();		
		}
	}
}

void SoundplaneModel::handleDeviceError(int errorType, int data1, int data2, float fd1, float fd2)
{
	switch(errorType)
	{
		case kDevDataDiffTooLarge:
			if(!mSelectingCarriers)
			{
				MLConsole() << "note: diff too large (" << fd1 << ")\n";
				MLConsole() << "startup count = " << data1 << "\n";
			}
			break;
		case kDevGapInSequence:
			MLConsole() << "note: gap in sequence (" << data1 << " -> " << data2 << ")\n";
			break;
		case kDevNoErr:
		default:
			MLConsole() << "SoundplaneModel::handleDeviceError: unknown error!\n";
			break;
	}
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

void SoundplaneModel::setTesting(bool testing)
{
	if (mTesting == testing)
	{
		// Avoid unnecessarily tearing down drivers
		return;
	}
	mTesting = testing;

	// First, replace the driver with an inert driver. This is a necessary step
	// because if mpDriver was replaced with another "real" driver immediately,
	// there would be two simultaneous processing threads, one for the old
	// driver that's shutting down and one for the new driver.
	//
	// When done like this, the old driver's thread will be fully torn down
	// before the call to mpDriver.reset returns. Then it's safe to replace
	// it with a new "real" driver.
	mpDriver.reset(new InertSoundplaneDriver());
	if (testing)
	{
		mpDriver.reset(new TestSoundplaneDriver(this));
	}
	else
	{
		mpDriver = SoundplaneDriver::create(this);
	}
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
                MLSymbol typeSym(pZoneType->valuestring);
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

            pz->mName = getJSONString(pNode, "name");
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

// turn (x, y) position into a continuous 2D key position.
// Soundplane A only.
Vec2 SoundplaneModel::xyToKeyGrid(Vec2 xy)
{
	MLRange xRange(4.5f, 60.5f);
	xRange.convertTo(MLRange(1.5f, 29.5f));
	float kx = clamp(xRange(xy.x()), 0.f, (float)kSoundplaneAKeyWidth);

	MLRange yRange(1., 6.);  // Soundplane A as measured with kNormalizeThresh = .125
	yRange.convertTo(MLRange(1.f, 4.f));
    float scaledY = yRange(xy.y());
	float ky = clamp(scaledY, 0.f, (float)kSoundplaneAKeyHeight);

	return Vec2(kx, ky);
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
		z = clamp(z, 0.f, 4.f);
		z = responseCurve(z, zcurve);
		mTouchArray[i].z = z;
		
		// for note-ons, use same z scale controls as pressure
		float dz = mTouchArray[i].dz*dzScale;
		dz *= zscale;		
		dz = clamp(dz, 0.f, 1.f);
		dz = responseCurve(dz, zcurve);
		mTouchArray[i].dz = dz;
	}
}

// send raw touches to zones in order to generate note and controller events.
void SoundplaneModel::sendTouchDataToZones()
{
	float x, y, z, dz;
	int age;

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
    mMessage.mType = MLSymbol("start_frame");
	sendMessageToListeners();

    // process note offs for each zone
	// this happens before processTouches() to allow voices to be freed
    int zones = mZones.size();
	std::vector<bool> freedTouches;
	freedTouches.resize(kSoundplaneMaxTouches);

	// generates freedTouches array, syntax should be different
    for(int i=0; i<zones; ++i)
	{
        mZones[i]->processTouchesNoteOffs(freedTouches);
    }

    // process touches for each zone
	for(int i=0; i<zones; ++i)
	{
        mZones[i]->processTouches(freedTouches);
    }

    // send optional calibrated matrix
    if(mSendMatrixData)
    {		
		MLSignal calibratedPressure = getCalibratedSignal();
		if(calibratedPressure.getHeight() == SensorGeometry::height)
		{
			
			mMessage.mType = MLSymbol("matrix");
			for(int j = 0; j < SensorGeometry::height; ++j)
			{
				for(int i = 0; i < SensorGeometry::width; ++i)
				{
					mMessage.mMatrix[j*SensorGeometry::width + i] = calibratedPressure(i, j);
				}
			}
			sendMessageToListeners();
		}
    }

    // tell listeners we are done with this frame.
    mMessage.mType = MLSymbol("end_frame");
	sendMessageToListeners();
}

void SoundplaneModel::sendMessageToListeners()
{
 	for(SoundplaneListenerList::iterator it = mListeners.begin(); it != mListeners.end(); it++)
    if((*it)->isActive())
    {
        (*it)->processSoundplaneMessage(&mMessage);
    }
}

// --------------------------------------------------------------------------------
//
#pragma mark -

const int kTestLength = 8000;

void SoundplaneModel::testCallback()
{	
	mSurface.clear();
	
	int h = mSurface.getWidth();
	int v = mSurface.getHeight();
	
	// make kernel (where is 2D Gaussian utility?)
	MLSignal k;
	int kSize = 5;
	float kr = (float)kSize*0.5f;
	float amp = 0.25f;
	k.setDims(5, 5);
	k.addDeinterpolatedLinear(kr, kr, amp);
	float kc, ke, kk;
	kc = 4.f/16.f; ke = 2.f/16.f; kk=1.f/16.f;
	k.convolve3x3r(kc, ke, kk);
	
	// get phase
	mTestCtr++;
	if(mTestCtr >= kTestLength)
	{
		mTestCtr = 0;
	}
	float omega = kMLTwoPi*(float)mTestCtr/(float)kTestLength;
	
	MLRange xRange(-1, 1, 0 - kr + 1.f, h - kr - 1.f);
	MLRange yRange(-1, 1, 0 - kr + 1.f, v - kr - 1.f);
	
	float x = xRange(cosf(omega));
	float y = yRange(sinf(omega*3.f));
	float z = clamp(sinf(omega*9.f) + 0.75f, 0.f, 1.f);

	// draw touches
	k.scale(z);
	mSurface.add2D(k, Vec2(x, y));
	
	// add noise
	for(int j=0; j< v; j++)
	{
		for(int i=0; i < h; i++)
		{
			mSurface(i, j) += fabs(MLRand())*0.01f;
		}
	}
	
	trackTouches();
}

void SoundplaneModel::trackTouches()
{
	mTestCtr++;
	if(mTestCtr >= 500)
	{
		mTestCtr = 0;
	}
	mHistoryCtr++;
	if (mHistoryCtr >= kSoundplaneHistorySize) mHistoryCtr = 0;
		
	mSensorFrame = signalToSensorFrame(mSurface);
	
	mTracker.process(&mSensorFrame, mMaxTouches, &mTouchArray, &mSmoothedFrame);
	
	mSmoothedSignal = sensorFrameToSignal(mSmoothedFrame);
	
	scaleTouchPressureData();

	// TODO mutex? 
	touchArrayToFrame(&mTouchArray, &mTouchFrame);

	mTouchHistory.setFrame(mHistoryCtr, mTouchFrame);

	sendTouchDataToZones();
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
	else if (mNeedsCalibrate)
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
		debug() << i << ": " << c << " ["<< SoundplaneDriver::carrierToFrequency(c) << "Hz] \n";
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
// calculating the mean and std. deviation for each taxel.
//
void SoundplaneModel::beginCalibrate()
{
	if(getDeviceState() == kDeviceHasIsochSync)
	{
		clear();

		clearTouchData();
		sendTouchDataToZones();

		mCalibrateCount = 0;
		mCalibrating = true;
	}
}

// called by process routine when enough samples have been collected.
//
void SoundplaneModel::endCalibrate()
{
	// skip frames after commands to allow noise to settle.
	int skipFrames = 100;
	int startFrame = skipFrames;
	int endFrame = kSoundplaneCalibrateSize - skipFrames;
	float calibrateFrames = endFrame - startFrame + 1;

	MLSignal calibrateSum(SensorGeometry::width, SensorGeometry::height);
	MLSignal calibrateStdDev(SensorGeometry::width, SensorGeometry::height);
	MLSignal dSum(SensorGeometry::width, SensorGeometry::height);
	MLSignal dMean(SensorGeometry::width, SensorGeometry::height);
	MLSignal mean(SensorGeometry::width, SensorGeometry::height);

	// get mean
	for(int i=startFrame; i<=endFrame; ++i)
	{
		// read frame from calibrate data.
		calibrateSum.add(mCalibrateData.getFrame(i));
	}
	mean = calibrateSum;
	mean.scale(1.f / calibrateFrames);
	mCalibrateMean = mean;
	mCalibrateMean.sigClamp(0.0001f, 2.f);
	mCalibrateMeanInv.fill(1.f);
	mCalibrateMeanInv.divide(mCalibrateMean);
	
	// get std deviation
	for(int i=startFrame; i<endFrame; ++i)
	{
		dMean = mCalibrateData.getFrame(i);
		dMean.subtract(mean);
		dMean.square();
		dSum.add(dMean);
	}
	dSum.scale(1.f / calibrateFrames);
	calibrateStdDev = dSum;
	calibrateStdDev.sqrt();
	mCalibrateStdDev = calibrateStdDev;

	mCalibrating = false;
	mHasCalibration = true;

	enableOutput(true);
}

float SoundplaneModel::getCalibrateProgress()
{
	return mCalibrateCount / (float)kSoundplaneCalibrateSize;
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
		mCalibrateCount = 0;
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
	// clear data
	mCalibrateSum.clear();
	mCalibrateCount = 0;

	// analyze calibration data just collected.
	// it's necessary to skip around 100 frames at start and end to get good data, not sure why yet.
	int skipFrames = 100;
	int startFrame = skipFrames;
	int endFrame = kSoundplaneCalibrateSize - skipFrames;
	float calibrateFrames = endFrame - startFrame + 1;
	MLSignal calibrateSum(SensorGeometry::width, SensorGeometry::height);
	MLSignal calibrateStdDev(SensorGeometry::width, SensorGeometry::height);
	MLSignal dSum(SensorGeometry::width, SensorGeometry::height);
	MLSignal dMean(SensorGeometry::width, SensorGeometry::height);
	MLSignal mean(SensorGeometry::width, SensorGeometry::height);
	MLSignal noise(SensorGeometry::width, SensorGeometry::height);
	
	// get mean
	for(int i=startFrame; i<=endFrame; ++i)
	{
		// read frame from calibrate data.
		calibrateSum.add(mCalibrateData.getFrame(i));
	}
	mean = calibrateSum;
	mean.scale(1.f / calibrateFrames);
	mCalibrateMean = mean;
	mCalibrateMean.sigClamp(0.0001f, 2.f);
	mCalibrateMeanInv.fill(1.f);
	mCalibrateMeanInv.divide(mCalibrateMean);

	// get std deviation
	for(int i=startFrame; i<endFrame; ++i)
	{
		dMean = mCalibrateData.getFrame(i);
		dMean.subtract(mean);
		dMean.square();
		dSum.add(dMean);
	}
	dSum.scale(1.f / calibrateFrames);
	calibrateStdDev = dSum;
	calibrateStdDev.sqrt();
	mCalibrateStdDev = calibrateStdDev;

	noise = calibrateStdDev;
	noise.divide(mean);

	// find maximum noise in any column for this set.  This is the "badness" value
	// we use to compare carrier sets.
	float maxNoise = 0;
	float maxNoiseFreq = 0;
	float noiseSum;
	int startSkip = 2;
	for(int col = startSkip; col<kSoundplaneNumCarriers; ++col)
	{
		noiseSum = 0;
		int carrier = mCarriers[col];
		float cFreq = SoundplaneDriver::carrierToFrequency(carrier);

		for(int row=0; row<SensorGeometry::height; ++row)
		{
			noiseSum += noise(col, row);
		}
//		debug() << "noise sum col " << col << " = " << noiseSum << " carrier " << carrier << " freq " << cFreq << "\n";

		if(noiseSum > maxNoise)
		{
			maxNoise = noiseSum;
			maxNoiseFreq = cFreq;
		}
	}

	mMaxNoiseByCarrierSet[mSelectCarriersStep] = maxNoise;
	mMaxNoiseFreqByCarrierSet[mSelectCarriersStep] = maxNoiseFreq;

	MLConsole() << "max noise for set " << mSelectCarriersStep << ": " << maxNoise << "(" << maxNoiseFreq << " Hz) \n";

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

	enableOutput(true);
}

// JSON utilities

std::string getJSONString(cJSON* pNode, const char* name)
{
    cJSON* pItem = cJSON_GetObjectItem(pNode, name);
    if(pItem)
    {
        if(pItem->type == cJSON_String)
        {
            return std::string(pItem->valuestring);
        }
    }
    return std::string("");
}

double getJSONDouble(cJSON* pNode, const char* name)
{
    cJSON* pItem = cJSON_GetObjectItem(pNode, name);
    if(pItem)
    {
        if(pItem->type == cJSON_Number)
        {
            return pItem->valuedouble;
        }
    }
    return 0.;
}

int getJSONInt(cJSON* pNode, const char* name)
{
    cJSON* pItem = cJSON_GetObjectItem(pNode, name);
    if(pItem)
    {
        if(pItem->type == cJSON_Number)
        {
            return pItem->valueint;
        }
    }
    return 0;
}


