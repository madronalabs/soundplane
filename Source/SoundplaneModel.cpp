
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneModel.h"

void *soundplaneModelProcessThreadStart(void *arg);

const unsigned char kModelDefaultCarriers[42] = 
{	
	0, 0,
	// 40 default carriers.  avoiding 16, 32 (always bad)	
	6, 7, 8, 9, 
	10, 11, 12, 13, 14, 
	15, 17, 18, 19, 20, 
	21, 22, 23, 24, 25, 
	26, 27, 28, 29, 30, 
	31, 33, 34, 35, 36,
	37, 38, 39, 40, 41, 
	42, 43, 44, 45, 46,
	47
};

// make one of the possible standard carrier sets, skipping a range of carriers out of the 
// middle of the 40 defaults.
//
static const int kStandardCarrierSets = 8;
static void makeStandardCarrierSet(unsigned char pC[kSoundplaneSensorWidth], int set);
static void makeStandardCarrierSet(unsigned char pC[kSoundplaneSensorWidth], int set)
{
	int skipStart = set*4 + 2;
	skipStart = clamp(skipStart, 0, kSoundplaneSensorWidth);
	int skipSize = 8;
	pC[0] = pC[1] = 0;
	for(int i=2; i<skipStart; ++i)
	{
		pC[i] = kModelDefaultCarriers[i];
	}
	for(int i=skipStart; i<kSoundplaneSensorWidth; ++i)
	{
		pC[i] = kModelDefaultCarriers[i + skipSize];
	}
}

SoundplaneModel::SoundplaneModel() :
	mDeviceState(kNoDevice),
	mOutputEnabled(false),
	mSurface(kSoundplaneWidth, kSoundplaneHeight),

	mpDriver(0),
	
	mRawSignal(kSoundplaneWidth, kSoundplaneHeight),
	mCalibratedSignal(kSoundplaneWidth, kSoundplaneHeight),
	mTempSignal(kSoundplaneWidth, kSoundplaneHeight),
	mCookedSignal(kSoundplaneWidth, kSoundplaneHeight),
	mTestSignal(kSoundplaneWidth, kSoundplaneHeight),

	mCalibrating(false),
	mSelectingCarriers(false),
	mDynamicCarriers(true),
	mCalibrateSum(kSoundplaneWidth, kSoundplaneHeight),
	mCalibrateMean(kSoundplaneWidth, kSoundplaneHeight),
	mCalibrateMeanInv(kSoundplaneWidth, kSoundplaneHeight),
	mCalibrateStdDev(kSoundplaneWidth, kSoundplaneHeight),
	//
	mNotchFilter(kSoundplaneWidth, kSoundplaneHeight),
	mLopassFilter(kSoundplaneWidth, kSoundplaneHeight),
	//
	mSampleRate(kSoundplaneSampleRate),
	mHasCalibration(false),
	//
	mZoneMap(kSoundplaneKeyWidth, kSoundplaneKeyHeight),

	mHistoryCtr(0),

	mProcessThread(0),
	mLastTimeDataWasSent(0),
	mZoneModeTemp(0),
	mCarrierMaskDirty(false),
	mNeedsCarriersSet(true),
	mNeedsCalibrate(true),
	mLastInfrequentTaskTime(0),
	mCarriersMask(0xFFFFFFFF),
	//
	//mOSCListenerThread(0),
	//mpUDPReceiveSocket(nullptr),
	mTest(0),
	mKymaIsConnected(0),
	mTracker(kSoundplaneWidth, kSoundplaneHeight)
{
	// setup geometry
	mSurfaceWidthInv = 1.f / (float)mSurface.getWidth();
	mSurfaceHeightInv = 1.f / (float)mSurface.getHeight();

	// setup fixed notch
	mNotchFilter.setSampleRate(mSampleRate);
	mNotchFilter.setNotch(300., 0.1);
	
	// setup fixed lopass.
	mLopassFilter.setSampleRate(mSampleRate);
	mLopassFilter.setLopass(50, 0.707);
	
	mNoteFilters.resize(kSoundplaneMaxTouches);
	mVibratoFilters.resize(kSoundplaneMaxTouches);
	for(int i=0; i<kSoundplaneMaxTouches; ++i)
	{
		mNoteFilters[i].setSampleRate(mSampleRate);
		mVibratoFilters[i].setSampleRate(mSampleRate);
		mVibratoFilters[i].setOnePole(12.0f);
		mNoteLock[i] = 0;
		mCurrentKeyX[i] = -1;
		mCurrentKeyY[i] = -1;
		mAge1[i] = 0;
		mNote1[i] = 0.f;
		mZ1[i] = 0.f;
	}
	
	mTracker.setSampleRate(mSampleRate);	
	
	// setup default carriers in case there are no saved carriers
	for (int car=0; car<kSoundplaneSensorWidth; ++car)
	{
		mCarriers[car] = kModelDefaultCarriers[car];
	}			
	
	setAllParamsToDefaults();

	mTracker.setListener(this);
}

SoundplaneModel::~SoundplaneModel()
{
	notifyClients(0);
		
	// delete driver -- this will cause process thread to terminate.
	//  
	if (mpDriver) 
	{ 
		delete mpDriver; 
		mpDriver = 0; 
	}
}

void SoundplaneModel::setAllParamsToDefaults()
{
	// parameter defaults and creation
	setModelParam("max_touches", 4);
	setModelParam("lopass", 100.);
	
	setModelParam("snap", 1.);
	setModelParam("z_thresh", 0.01);
	setModelParam("z_max", 0.05);
	setModelParam("z_curve", 0.25);
	setModelParam("display_scale", 1.);
	
	setModelParam("quantize", 1.);
	setModelParam("lock", 0.);
	setModelParam("abs_rel", 0.);
	setModelParam("snap", 250.);
	setModelParam("vibrato", 0.5);
		
	setModelParam("t_thresh", 0.2);
	
	setModelParam("midi_active", 0);
	setModelParam("midi_multi_chan", 1);
	setModelParam("midi_start_chan", 1);
	setModelParam("data_freq_midi", 250.);
	
	setModelParam("kyma_poll", 1);
	
	setModelParam("osc_active", 1);	
	setModelParam("data_freq_osc", 250.);
	
	setModelParam("bend_range", 48);
	setModelParam("transpose", 0);
	setModelParam("bg_filter", 0.05);
	
	setModelParam("hysteresis", 0.5);
	
	// menu param defaults
	setModelParam("preset", "continuous pitch x");
	setModelParam("viewmode", "calibrated");
	
	for(int i=0; i<32; ++i)
	{
		setModelParam(MLSymbol("carrier_toggle").withFinalNumber(i), 1);		
	}
}

// Process incoming OSC.  Used for Kyma communication. 
//
void SoundplaneModel::ProcessMessage(const osc::ReceivedMessage& m, const IpEndpointName& remoteEndpoint)
{
	osc::ReceivedMessageArgumentStream args = m.ArgumentStream();
	osc::int32 a1;
	try
	{
		if( std::strcmp( m.AddressPattern(), "/osc/response_from" ) == 0 )
		{
			args >> a1 >> osc::EndMessage;
			// set Kyma mode
			if (mOSCOutput.getKymaMode())
			{
				mKymaIsConnected = true;
			}
		} 
		else if (std::strcmp( m.AddressPattern(), "/osc/notify/midi/Soundplane" ) == 0 )  
		{	
			args >> a1 >> osc::EndMessage;
			// set voice count to a1
			int newTouches = clamp((int)a1, 0, kSoundplaneMaxTouches);
			if(mKymaIsConnected)
			{
				// Kyma is sending 0 sometimes, which there is probably 
				// no reason to respond to
				if(newTouches > 0)
				{
					setModelParam("max_touches", newTouches);
				}
			}
		}
	}
	catch( osc::Exception& e )
	{
		debug() << "oscpack error while parsing message: "
			<< m.AddressPattern() << ": " << e.what() << "\n";
	}
}

void SoundplaneModel::setModelParam(MLSymbol p, float v) 
{
	MLModel::setModelParam(p, v);
	if (p.withoutFinalNumber() == MLSymbol("carrier_toggle"))
	{
		// toggles changed -- mute carriers 
		unsigned long mask = 0;	
		for(int i=0; i<32; ++i)
		{
			MLSymbol tSym = MLSymbol("carrier_toggle").withFinalNumber(i);
			bool on = (int)(getModelFloatParam(tSym));
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
			setModelParam(tSym, on);
		}
		mCarriersMask = on ? ~0 : 0;
		mCarrierMaskDirty = true; // trigger carriers set in a second or so
	}
	else if (p == "max_touches")
	{
		mTracker.setMaxTouches(v);
		mMIDIOutput.setMaxTouches(v);
		mOSCOutput.setMaxTouches(v);
	}
	else if (p == "lopass")
	{
		mTracker.setLopass(v);
	}
		
	else if (p == "z_thresh")
	{
		mTracker.setThresh(v);
	}
	else if (p == "z_max")
	{
		mTracker.setMaxForce(v); 
	}
	else if (p == "z_curve")
	{
		mTracker.setForceCurve(v); 
	}
	else if (p == "snap")
	{
		float snapFreq = 1000.f / (v + 1.);
		mSnapFreq = clamp(snapFreq, 1.f, 1000.f);
		for(int i=0; i<kSoundplaneMaxTouches; ++i)
		{
			mNoteFilters[i].setOnePole(mSnapFreq);
		}
	}
	
	else if (p == "data_freq_midi")
	{
		mMIDIOutput.setDataFreq(v);
	}
	else if (p == "data_freq_osc")
	{
		mOSCOutput.setDataFreq(v);
	}
	else if (p == "midi_active")
	{
		mMIDIOutput.setActive(bool(v));
	}
	else if (p == "midi_multi_chan")
	{
		mMIDIOutput.setMultiChannel(bool(v));
	}
	else if (p == "midi_start_chan")
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
		listenToOSC(b ? kDefaultUDPReceivePort : 0);
	}
	else if (p == "t_thresh")
	{
		mTracker.setTemplateThresh(v);
	}
	else if (p == "bg_filter")
	{
		mTracker.setBackgroundFilter(v);
	}
	else if (p == "quantize")
	{
		bool b = v;
		mTracker.setQuantize(b);
	}
	else if (p == "rotate")
	{
		bool b = v;
		mTracker.setRotate(b);
	}
	else if (p == "normalize")
	{
		bool b = v;
		mTracker.setNormalize(b);
	}
	else if (p == "retrig")
	{
		mMIDIOutput.setRetrig(bool(v));
	}
	else if (p == "hysteresis")
	{
		mMIDIOutput.setHysteresis(v);
	}
	else if (p == "bend_range")
	{
		mMIDIOutput.setBendRange(v);
	}
	else if (p == "debug_pause")
	{
		debug().setActive(!bool(v));
	}
	else if (p == "kyma_poll")
	{
		mMIDIOutput.setKymaPoll(bool(v));
	}
}

void SoundplaneModel::setModelParam(MLSymbol p, const std::string& v) 
{
	MLModel::setModelParam(p, v);
	// debug() << "SoundplaneModel::setModelParam " << p << " : " << v << "\n";

	if (p == "viewmode")
	{
		// nothing to do for Model
 	}
	else if (p == "midi_device")
	{
		mMIDIOutput.setDevice(v);
	}
	else if (p == "preset")
	{
		// TODO load preset by name.
		if(v == "continuous pitch x")
		{
			setZoneMode(0);
		}
		else if(v == "rows in fourths")
		{
			setZoneMode(1);
		}
	}
}

void SoundplaneModel::setModelParam(MLSymbol p, const MLSignal& v) 
{
	MLModel::setModelParam(p, v);
	if(p == MLSymbol("carriers"))
	{
		// get carriers from signal
		assert(v.getSize() == kSoundplaneSensorWidth);
		for(int i=0; i<kSoundplaneSensorWidth; ++i)
		{
			mCarriers[i] = v[i];
		}		
		mNeedsCarriersSet = true;
	}
	if(p == MLSymbol("tracker_calibration"))
	{
		mTracker.setCalibration(v);
	}
	if(p == MLSymbol("tracker_normalize"))
	{
		mTracker.setNormalizeMap(v);
	}
}

void SoundplaneModel::initialize()
{
	mMIDIOutput.initialize();
	addDataListener(&mMIDIOutput);
	
	mOSCOutput.initialize();
	addDataListener(&mOSCOutput);
	
	mpDriver = new SoundplaneDriver();
	mpDriver->addListener(this);
	mpDriver->init();
	
	// TODO mem err handling	
	if (!mCalibrateData.setDims(kSoundplaneWidth, kSoundplaneHeight, kSoundplaneCalibrateSize))
	{
		debug() << "SoundplaneModel: out of memory!\n";
	}
	
	mTouchFrame.setDims(kTouchWidth, kSoundplaneMaxTouches);
	mTouchHistory.setDims(kTouchWidth, kSoundplaneMaxTouches, kSoundplaneHistorySize);
	mCarrierStats.resize(kSoundplanePossibleCarriers);
	
	// create process thread
	OSErr err;
	pthread_attr_t attr;
	err = pthread_attr_init(&attr);
	assert(!err);
	err = pthread_create(&mProcessThread, &attr, soundplaneModelProcessThreadStart, this);
	assert(!err);
	setThreadPriority(mProcessThread, 96, true);
}

int SoundplaneModel::getClientState(void)
{
	PaUtil_ReadMemoryBarrier(); 
	return mKymaIsConnected;
}

int SoundplaneModel::getDeviceState(void)
{
	PaUtil_ReadMemoryBarrier(); 
	return mDeviceState;
}
void SoundplaneModel::deviceStateChanged(MLSoundplaneState s)
{
	unsigned long instrumentModel = 1; // Soundplane A
	unsigned long serial = mpDriver->getSerialNumber();
	
	PaUtil_WriteMemoryBarrier();

	mDeviceState = s;
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
			mOSCOutput.setSerialNumber((instrumentModel << 16) | serial);
			mNeedsCarriersSet = true;
			// output will be enabled at end of calibration. 
			mNeedsCalibrate = true;						
		break;
		case kDeviceIsTerminating:
			// shutdown processing
			if (mProcessThread)
			{
				debug() << "SoundplaneModel: terminating \n";
				int exitResult = pthread_join(mProcessThread, NULL); // TODO this can hang here
				printf("SoundplaneModel:: process thread terminated.  Returned %d \n", exitResult);			
				mProcessThread = 0;
			}
			
		break;
		case kDeviceSuspend:
		break;
		case kDeviceResume:
		break;
	}
}

void SoundplaneModel::handleDeviceError(int errorType, int data1, int data2, float fd1, float fd2)
{
	switch(errorType)
	{
		case kDevDataDiffTooLarge:
			if(!mSelectingCarriers)
			{
				debug() << "SoundplaneModel::handleDeviceError: diff too large (" << fd1 << ")\n";
				debug() << "startup count = " << data1 << "\n";
			}
			break;
		case kDevGapInSequence:
			debug() << "SoundplaneModel::handleDeviceError: gap in sequence (" << data1 << " -> " << data2 << ")\n";
			break;
		case kDevNoErr:
		default:
			debug() << "SoundplaneModel::handleDeviceError: unknown error!\n";
			break;
	}
}

void SoundplaneModel::handleDeviceDataDump(float* pData, int size)
{
	if(mSelectingCarriers) return;

	debug() << "----------------------------------------------------------------\n ";
	int c = 0;
	int w = getWidth();
	int row = 0;
	debug() << std::setprecision(2);
	
	debug() << "[0] ";
	for(int i=0; i<size; ++i)
	{
		debug() << pData[i] << " ";
		c++;
		if(c >= w)
		{
			debug() << "\n";
			c = 0;
			if (i < (size - 1))
			{				
				debug() << "[" << ++row << "] ";
			}
		}
	}
	debug() << "\n";
}

// when calibration is done, set params to save entire calibration signal and
// set template threshold based on average distance
void SoundplaneModel::hasNewCalibration(const MLSignal& cal, const MLSignal& norm, float avgDistance)
{
	if(avgDistance > 0.f)
	{
		setModelParam("tracker_calibration", cal);	
		setModelParam("tracker_normalize", norm);	
		float thresh = avgDistance;
		debug() << "SoundplaneModel::hasNewCalibration: calculated template threshold: " << thresh << "\n";	
		setModelParam("t_thresh", thresh);	
	}
	else
	{
		// set default calibration
		setModelParam("tracker_calibration", cal);	
		setModelParam("tracker_normalize", norm);	
		float thresh = 0.2f;
		debug() << "SoundplaneModel::hasNewCalibration: default template threshold: " << thresh << "\n";	
		setModelParam("t_thresh", thresh);	
	}
}

// get a string that explains what Soundplane hardware and firmware and client versions are running.
const char* SoundplaneModel::getHardwareStr()
{
	long v;
	unsigned char a, b, c;
	char serial[64] = {0};
	int len;
	switch(mDeviceState)
	{
		case kNoDevice:
			snprintf(mHardwareStr, miscStrSize, "no device");
			break;
		case kDeviceConnected:
		case kDeviceHasIsochSync:
		
			len = mpDriver->getSerialNumberString(serial, 64);
			
			v = mpDriver->getFirmwareVersion();
			a = v >> 8 & 0x0F;
			b = v >> 4 & 0x0F, 
			c = v & 0x0F;
	
			snprintf(mHardwareStr, miscStrSize, "%s #%s, firmware %d.%d.%d", kSoundplaneAName, serial, a, b, c);
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
	switch(mDeviceState)
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

// TODO read attributes for each zone from JSON setup
// zone map signal stores note# attribute for easy interpolation.
//
void SoundplaneModel::setupZones() 
{
	int zoneIdx, zone;
	int mode = mZoneModeTemp;
	
	switch(mode)
	{
	case 0:
		
		for(int j=0; j<kSoundplaneKeyHeight; ++j)
		{
			for(int i=0; i<kSoundplaneKeyWidth; ++i)
			{
				zoneIdx = j*kSoundplaneKeyWidth + i + 1;
				// offset puts A220 at center
				zone = 45 + i; // TODO read map
				mZoneMap(i, j) = zone;
			}
		}
		break;
	case 1:
	default:	
		for(int j=0; j<kSoundplaneKeyHeight; ++j)
		{
			for(int i=0; i<kSoundplaneKeyWidth; ++i)
			{
				zoneIdx = j*kSoundplaneKeyWidth + i + 1;
				zone = 35 + i + 5*j; // TODO read map
				mZoneMap(i, j) = zone;
			}
		}
		break;
	}
}

// turn (x, y) position into a continuous 2D key position. Integer values of the
// x and y axes will be centered on the Soundplane surface keys. 
//
Vec2 SoundplaneModel::xyToKeyGrid(Vec2 xy) 
{
	MLRange xRange(4.5f, 60.5f);
	xRange.convertTo(MLRange(1.f, 29.f));
	float kx = clamp(xRange(xy.x()), -0.5f, 29.5f);

	MLRange yRange(1.35, 5.65);  // Soundplane A as measured		
	yRange.convertTo(MLRange(0.5f, 3.5f));
	float ky = clamp(yRange(xy.y()), -0.5f, 4.5f);

	return Vec2(kx, ky);
}

void SoundplaneModel::clearTouchData()
{
	const int maxTouches = getModelFloatParam("max_touches");
	for(int i=0; i<maxTouches; ++i)
	{
		mTouchFrame(xColumn, i) = 0;		
		mTouchFrame(yColumn, i) = 0;	
		mTouchFrame(zColumn, i) = 0;	
		mTouchFrame(dzColumn, i) = 0;	
		mTouchFrame(ageColumn, i) = 0;	
		mTouchFrame(dtColumn, i) = 1.;	
		mTouchFrame(noteColumn, i) = -1; 		
		mTouchFrame(reservedColumn, i) = 0; 		
	}
}

// scale and filter touch data before sending out to data listeners.
// here is where key-to-key hysteresis happens.
//
void SoundplaneModel::postProcessTouchData()
{
	float x, y, z;
	int age; 
	float note, noteQuant, vibratoX, vibratoHP;
	//const float thresh = getModelFloatParam("z_thresh");
	const float zmax = getModelFloatParam("z_max");
	const float zcurve = getModelFloatParam("z_curve");
	const int maxTouches = getModelFloatParam("max_touches");
	const int quantize = getModelFloatParam("quantize");
	const int lock = getModelFloatParam("lock");
	const float vibratoAmp = getModelFloatParam("vibrato");
	const int transpose = getModelFloatParam("transpose");
	const float hysteresis = getModelFloatParam("hysteresis");
	
	MLRange yRange(0.05, 0.8);
	yRange.convertTo(MLRange(0., 1.));

	for(int i=0; i<maxTouches; ++i)
	{
		age = mTouchFrame(ageColumn, i);
		if(age > 0)
		{
			x = mTouchFrame(xColumn, i);
			y = mTouchFrame(yColumn, i);
			z = mTouchFrame(zColumn, i);
				
			// apply adjustable force curve for z over [z_thresh, z_max] 
			z /= zmax;
			z = (1.f - zcurve)*z + zcurve*z*z*z;		
			mTouchFrame(zColumn, i) = clamp(z, 0.f, 1.f);
						
			// get note from x, y position (Soundplane A)
			Vec2 keyXY = xyToKeyGrid(Vec2(x, y));	
			if (!quantize)
			{
				// get fractional note using interpolated x and integer y.
				// don't add vibrato
				float fx = clamp(keyXY.x(), 0.f, 29.f);
				int iy = (float)lround(keyXY.y());			
				
				// do hysteresis between rows
				if(age == 1)
				{
					mCurrentKeyY[i] = iy;
				}
				else
				{
					float cy = mCurrentKeyY[i] ;
					float dy = keyXY.y() - cy;
					
					float hystThresh = 0.5f + hysteresis*0.25f;
					if(fabs(dy) > hystThresh)
					{
						mCurrentKeyY[i] = iy;
					}					
				}
				
				note = mZoneMap(Vec2(fx, (float)mCurrentKeyY[i])); 
			}
			else
			{
				bool changedY = false;
				
				// get quantized note 
				int ix = lround(keyXY.x());
				int iy = lround(keyXY.y()); 			
				
				// hysteresis: make it harder to move out of current key
				if(age == 1)
				{
					mCurrentKeyX[i] = ix;
					mCurrentKeyY[i] = iy;
				}
				else
				{
					float cx = mCurrentKeyX[i] ;
					float cy = mCurrentKeyY[i] ;
					float dx = keyXY.x() - cx;
					float dy = keyXY.y() - cy;
					
					float hystThresh = 0.5f + hysteresis*0.25f;
					changedY = (fabs(dy) > hystThresh);
					bool changedX = (fabs(dx) > hystThresh);
					if(changedX || changedY)
					{
						mCurrentKeyX[i] = ix;
						mCurrentKeyY[i] = iy;
					}		
				}
				
				noteQuant = mZoneMap(mCurrentKeyX[i], mCurrentKeyY[i]);		
				
				// unless touch is new, LPF zone number
				mNoteFilters[i].setInput(&noteQuant);
				mNoteFilters[i].setOutput(&noteQuant);
				// changedY test prevents MIDI slide from row to row
				if ((age == 1) || changedY)
				{
					mNoteFilters[i].setState(noteQuant);	
				}
				mNoteFilters[i].process(1);	
				note = noteQuant; 	
								
				// unless touch is new, filter vibrato
				vibratoX = x;
				mVibratoFilters[i].setInput(&vibratoX);
				mVibratoFilters[i].setOutput(&vibratoX);
				if (age == 1)
				{
					mVibratoFilters[i].setState(vibratoX);
				}
				mVibratoFilters[i].process(1);	
				
				// add vibrato to note
				vibratoHP = (x - vibratoX)*vibratoAmp*kSoundplaneVibratoAmount;		
				note += vibratoHP; 	
			}
					
			// lock: only change pitch on note start
			if (lock)  
			{
				if (age == 1)
				{
					mNoteLock[i] = note;
				}
				else
				{
					note = mNoteLock[i] + vibratoHP;
				}
			}
					
			mTouchFrame(noteColumn, i) = note + transpose; 		
			
			// scale x and y to unity range for clients
			mTouchFrame(xColumn, i) = clamp(x*mSurfaceWidthInv, 0.f, 1.f);		
			mTouchFrame(yColumn, i) = clamp(y*mSurfaceHeightInv, 0.f, 1.f);	
			
			// store previous z			
			mZ1[i] = z;
		}
		else if ((mAge1[i] > 0) && quantize)
		{
			// on note off, send quantized note for release
			float releaseNote = roundf(mNote1[i]) + transpose;
			mTouchFrame(noteColumn, i) = releaseNote;
			mTouchFrame(ageColumn, i) = age; 
			mTouchFrame(zColumn, i) = mZ1[i];
		}

		mAge1[i] = age;
		mNote1[i] = note;
					
	}
}

// send frame of touch data to any listeners.
//
void SoundplaneModel::sendTouchDataToClients()
{	
	// send to clients
	//
	int size = mDataListeners.size();
	for(int i=0; i<size; ++i)
	{
		SoundplaneDataListener* pL = mDataListeners[i];
		pL->processFrame(mTouchFrame);
	}
}

// notify clients of soundplane connect state
//
void SoundplaneModel::notifyClients(int c)
{	
	// send to clients
	//
	int size = mDataListeners.size();
	for(int i=0; i<size; ++i)
	{
		SoundplaneDataListener* pL = mDataListeners[i];
		pL->notify(c);
	}
}

void *soundplaneModelProcessThreadStart(void *arg)
{
	SoundplaneModel* m = static_cast<SoundplaneModel*>(arg);
	int waitTimeMicrosecs;
	
	// wait for data
	while((m->getDeviceState() != kDeviceHasIsochSync) && (m->getDeviceState() != kDeviceIsTerminating))
	{
		waitTimeMicrosecs = 1000; 
		usleep(waitTimeMicrosecs);
	}
	
	while(m->getDeviceState() != kDeviceIsTerminating) 
	{
		m->processCallback();
		
		waitTimeMicrosecs = 250; 
		usleep(waitTimeMicrosecs);
	}
	debug() << "soundplaneModelProcessThread terminating\n";
	return 0;
}

// --------------------------------------------------------------------------------
//
#pragma mark -

void SoundplaneModel::setKymaMode(bool m)
{
	mOSCOutput.setKymaMode(m);
	if (m)
	{
		// looking for Kyma
		// TODO poll and check Kyma connection periodically
	}
	else
	{
		mKymaIsConnected = false;
	}
}

// --------------------------------------------------------------------------------
//
#pragma mark -

// called by the process thread in a tight loop to receive data from the driver.
//
void SoundplaneModel::processCallback()
{
	if (!mpDriver) return;
	
	UInt64 now = getMicroseconds();
	if(now - mLastInfrequentTaskTime > 250*1000)
	{
		doInfrequentTasks();
		mLastInfrequentTaskTime = now;
	}

	// make sure driver is set up
	if (mpDriver->getDeviceState() != kDeviceHasIsochSync) return;
	
	// read from driver's ring buffer to incoming surface
	MLSample* pSurfaceData = mSurface.getBuffer();
	if(mpDriver->readSurface(pSurfaceData) != 1) return;
	
	// store surface for raw output
	mRawSignal.copy(mSurface);
	
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
		// scale incoming data
		float in, cmean, cout;
		float epsilon = 0.000001;
		if (mHasCalibration)
		{
			for(int j=0; j<mSurface.getHeight(); ++j)
			{
				for(int i=0; i<mSurface.getWidth(); ++i)
				{
					// scale to 1/z curve
					in = mSurface(i, j);
					cmean = mCalibrateMean(i, j);
					cout = (1.f - ((cmean + epsilon) / (in + epsilon)));
					mSurface(i, j) = cout;					
				}
			}
		}
		
		// fill in null data at edges
		int ww = mSurface.getWidth() - 1;
		for(int j=0; j<mSurface.getHeight(); ++j)
		{
			mSurface(0, j) = 0.;   
			mSurface(1, j) = mSurface(2, j);    
			mSurface(ww, j) = 0.;
			mSurface(ww - 1, j) = mSurface(ww - 2, j);
		}
			
		// filter data in time
		mNotchFilter.setInputSignal(&mSurface);
		mNotchFilter.setOutputSignal(&mSurface);
		mNotchFilter.process(1);					
		mLopassFilter.setInputSignal(&mSurface);
		mLopassFilter.setOutputSignal(&mSurface);
		mLopassFilter.process(1);	
		
		// send filtered data to touch tracker.
		mTracker.setInputSignal(&mSurface);					
		mTracker.setOutputSignal(&mTouchFrame);
		mTracker.process(1);
		
		postProcessTouchData();
		sendTouchDataToClients();	
		
		// get calibrated and cooked signals for viewing
		mCalibratedSignal = mTracker.getCalibratedSignal();								
		mCookedSignal = mTracker.getCookedSignal();
		mTestSignal = mTracker.getTestSignal();

		mHistoryCtr++;
		if (mHistoryCtr >= kSoundplaneHistorySize) mHistoryCtr = 0;
		mTouchHistory.setFrame(mHistoryCtr, mTouchFrame);			
	}	
}

void SoundplaneModel::doInfrequentTasks()
{
	if (mCarrierMaskDirty)
	{
		enableCarriers(mCarriersMask);
	}	
	else if (mpDriver && mNeedsCarriersSet)
	{
		mNeedsCarriersSet = false;
		setCarriers(mCarriers);
		mNeedsCalibrate = true;
		dumpCarriers();
	}
	else if (mpDriver && mNeedsCalibrate)
	{
		mNeedsCalibrate = false;
		beginCalibrate();
	}
	notifyClients(mpDriver->getDeviceState() == kDeviceHasIsochSync);
}

void SoundplaneModel::setDefaultCarriers()
{
	if (mpDriver)
	{
		MLSignal cSig(kSoundplaneSensorWidth);
		for (int car=0; car<kSoundplaneSensorWidth; ++car)
		{
			cSig[car] = kModelDefaultCarriers[car];
		}		
		setModelParam("carriers", cSig);
	}
}

void SoundplaneModel::setCarriers(unsigned char* c)
{
	if (mpDriver)
	{
		enableOutput(false);

		for(int i=0; i < kSoundplaneSensorWidth; ++i)
		{
	debug() << "carrier " << i << ":" << c[i] << "\n";		
		}

		IOReturn err = mpDriver->setCarriers(c);
		if (err != kIOReturnSuccess)
		{
			printf("SoundplaneModel::setCarriers error %d\n", err);
		}
	}
}

int SoundplaneModel::enableCarriers(unsigned long mask)
{
	if (!mpDriver) return 0;
	mpDriver->enableCarriers(~mask);
	if (mask != mCarriersMask)
	{
		mCarriersMask = mask;
	}
	mCarrierMaskDirty = false;
	return 0;
}

void SoundplaneModel::dumpCarriers()
{
	debug() << "\n------------------\n";
	debug() << "carriers: \n";
	for(int i=0; i<kSoundplaneSensorWidth; ++i)
	{
		int c = mCarriers[i];
		debug() << i << ": " << c << " ["<< mpDriver->carrierToFrequency(c) << "Hz] \n";
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
	if(mpDriver->getDeviceState() == kDeviceHasIsochSync)
	{
		clear();
		
		clearTouchData();
		sendTouchDataToClients();	

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

	MLSignal calibrateSum(kSoundplaneWidth, kSoundplaneHeight);
	MLSignal calibrateStdDev(kSoundplaneWidth, kSoundplaneHeight);	
	MLSignal dSum(kSoundplaneWidth, kSoundplaneHeight);
	MLSignal dMean(kSoundplaneWidth, kSoundplaneHeight);
	MLSignal mean(kSoundplaneWidth, kSoundplaneHeight);

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
	
	mNotchFilter.clear();	
	mLopassFilter.clear();
		
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
	if(mpDriver->getDeviceState() == kDeviceHasIsochSync)
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
		debug() << "testing carriers set " << mSelectCarriersStep << "...\n";
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
	MLSignal calibrateSum(kSoundplaneWidth, kSoundplaneHeight);
	MLSignal calibrateStdDev(kSoundplaneWidth, kSoundplaneHeight);	
	MLSignal dSum(kSoundplaneWidth, kSoundplaneHeight);
	MLSignal dMean(kSoundplaneWidth, kSoundplaneHeight);
	MLSignal mean(kSoundplaneWidth, kSoundplaneHeight);
	MLSignal noise(kSoundplaneWidth, kSoundplaneHeight);

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
	
	noise = calibrateStdDev;
	
	// find maximum noise in any column for this set.  This is the "badness" value
	// we use to compare carrier sets. 
	float maxNoise = 0;
	float maxNoiseFreq = 0;
	float noiseSum;
	int startSkip = 2;
	for(int col = startSkip; col<kSoundplaneSensorWidth; ++col)
	{
		noiseSum = 0;
		int carrier = mCarriers[col];
		float cFreq = mpDriver->carrierToFrequency(carrier);
		
		for(int row=0; row<kSoundplaneHeight; ++row)
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
	
	debug() << "max noise for set " << mSelectCarriersStep << ": " << maxNoise << "(" << maxNoiseFreq << " Hz) \n";
	
	// set up next step.
	mSelectCarriersStep++;		
	if (mSelectCarriersStep < kStandardCarrierSets)
	{		
		// set next carrier frequencies to calibrate.
		debug() << "testing carriers set " << mSelectCarriersStep << "...\n";
		makeStandardCarrierSet(mCarriers, mSelectCarriersStep);
		setCarriers(mCarriers);
	}
	else 
	{
		endSelectCarriers();
	}
	
	mpDriver->flushOutputBuffer();
}

void SoundplaneModel::endSelectCarriers()
{
	// get minimum of collected noise sums
	float minNoise = 99999.f;
	int minIdx = -1;
	debug() << "------------------------------------------------\n";
	debug() << "carrier select noise results:\n";
	for(int i=0; i<kStandardCarrierSets; ++i)
	{
		float n = mMaxNoiseByCarrierSet[i];
		float h = mMaxNoiseFreqByCarrierSet[i];
		debug() << "set " << i << ": max noise " << n << "(" << h << " Hz)\n";
		if(n < minNoise)
		{
			minNoise = n;
			minIdx = i;
		} 
	}

	// set that carrier group
	debug() << "setting carriers set " << minIdx << "...\n";
	makeStandardCarrierSet(mCarriers, minIdx);

	// set chosen carriers as model parameter so they will be saved
	// this will trigger a recalibrate
	if (mpDriver)
	{
		MLSignal cSig(kSoundplaneSensorWidth);
		for (int car=0; car<kSoundplaneSensorWidth; ++car)
		{
			cSig[car] = mCarriers[car];
		}		
		setModelParam("carriers", cSig);
	}
	debug() << "carrier select done.\n";

	mSelectingCarriers = false;
}
	
const MLSignal& SoundplaneModel::getSignalForViewMode(SoundplaneViewMode m)
{
	switch(m)
	{
		case kRaw:
			return mRawSignal;
			break;
		case kCalibrated:
			default:
			return mCalibratedSignal;
			break;
		case kCooked:
			return mCookedSignal; 
			break;
		case kXY:
			return mCalibratedSignal; 
			break;
		case kTest:
			return mTestSignal; 
			break;
		case kNrmMap:
			return mTracker.getNormalizeMap();
			break;
	}
}

const MLSignal& SoundplaneModel::getTrackerCalibrateSignal()
{
	return mTracker.getCalibrateSignal();
}

Vec3 SoundplaneModel::getTrackerCalibratePeak()
{
	return mTracker.getCalibratePeak();
}

bool SoundplaneModel::isWithinTrackerCalibrateArea(int i, int j)
{
	return mTracker.isWithinCalibrateArea(i, j);
}

// --------------------------------------------------------------------------------
#pragma mark tracker calibration

void SoundplaneModel::beginNormalize()
{
	if(mpDriver->getDeviceState() == kDeviceHasIsochSync)
	{	
		mTracker.beginCalibrate();
	}
}

void SoundplaneModel::cancelNormalize()
{
	if(mpDriver->getDeviceState() == kDeviceHasIsochSync)
	{	
		mTracker.cancelCalibrate();
	}
}

bool SoundplaneModel::trackerIsCalibrating()
{
	int r = 0;
	if(mpDriver->getDeviceState() == kDeviceHasIsochSync)
	{	
		r = mTracker.isCalibrating();
	}
	return r;
}

bool SoundplaneModel::trackerIsCollectingMap()
{
	int r = 0;
	if(mpDriver->getDeviceState() == kDeviceHasIsochSync)
	{	
		r = mTracker.isCollectingNormalizeMap();
	}
	return r;
}

void SoundplaneModel::setDefaultNormalize()
{
	if(mpDriver->getDeviceState() == kDeviceHasIsochSync)
	{	
		mTracker.setDefaultNormalizeMap();
	}
}
