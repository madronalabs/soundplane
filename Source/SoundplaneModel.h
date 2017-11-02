
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_MODEL__
#define __SOUNDPLANE_MODEL__

#include <list>
#include <map>
#include <stdint.h>

#include "MLDebug.h"
#include "MLTime.h"
#include "MLModel.h"
#include "SoundplaneModelA.h"
#include "SoundplaneDriver.h"
#include "SoundplaneDataListener.h"
#include "MLOSCListener.h"
#include "MLNetServiceHub.h"
#include "TouchTracker.h"
#include "SoundplaneMIDIOutput.h"
#include "SoundplaneOSCOutput.h"
#include "MLSymbol.h"
#include "MLParameter.h"
#include "MLFileCollection.h"
#include "cJSON.h"
#include "Zone.h"
#include "SoundplaneBinaryData.h"

typedef enum
{
	xColumn = 0,
	yColumn = 1,
	zColumn = 2,
	dzColumn = 3,
	ageColumn = 4
} TouchSignalColumns;

class SoundplaneModel :
	public MLOSCListener,
	public MLNetServiceHub,
	public MLModel
{
public:

	SoundplaneModel();
	~SoundplaneModel();
	
	// MLModel
    void doPropertyChangeAction(MLSymbol , const MLProperty & ) override;

	void setAllPropertiesToDefaults();
	
	// start a thread to run process() in a loop. This is needed for platforms on which we can't just
	// run our own main event loop.
	void startProcessThread();
	
	
	SoundplaneDriver::returnValue process();

	// MLOSCListener
	void ProcessMessage(const osc::ReceivedMessage &m, const IpEndpointName& remoteEndpoint) override;
	void ProcessBundle(const osc::ReceivedBundle &b, const IpEndpointName& remoteEndpoint) override;

	// MLNetServiceHub
	void didResolveAddress(NetService *pNetService) override;

	// OSC services
	void refreshServices();
	const std::vector<std::string>& getServicesList();
	void formatServiceName(const std::string& inName, std::string& outName);

	MLFileCollection& getZonePresetsCollection() { return *mZonePresets; }

	void testCallback();
	void processCallback();

	float getSampleHistory(int x, int y);

	void getHistoryStats(float& mean, float& stdDev);
	int getWidth() { return mSurface.getWidth(); }
	int getHeight() { return mSurface.getHeight(); }

	void setDefaultCarriers();
	void setCarriers(const SoundplaneDriver::Carriers& c);
	int enableCarriers(unsigned long mask);
	int getNumCarriers() { return kSoundplaneNumCarriers; }
	void dumpCarriers(const SoundplaneDriver::Carriers& carriers);

	void enableOutput(bool b);

	int getStateIndex();
	const char* getHardwareStr();
	const char* getStatusStr();
	const char* getClientStr();

	int getSerialNumber() const {return mSerialNumber;}

	void clear();

	void setRaw(bool b);
	bool getRaw(){ return mRaw; }

	void beginCalibrate();
	bool isCalibrating() { return mCalibrating; }
	float getCalibrateProgress();
	void endCalibrate();

	void beginSelectCarriers();
	bool isSelectingCarriers() { return mSelectingCarriers; }
	float getSelectCarriersProgress();
	void nextSelectCarriersStep();
	void endSelectCarriers();

	void setFilter(bool b);

	void getMinMaxHistory(int n);
	const MLSignal& getCorrelation();
	
	const MLSignal& getTouchFrame() { return mTouchFrame; }
	const MLSignal& getTouchHistory() { return mTouchHistory; }
	const MLSignal getRawSignal() { std::lock_guard<std::mutex> lock(mRawSignalMutex); return mRawSignal; }
	const MLSignal getCalibratedSignal() { std::lock_guard<std::mutex> lock(mCalibratedSignalMutex); return mCalibratedSignal; }
	const MLSignal getSmoothedSignal() { std::lock_guard<std::mutex> lock(mSmoothedSignalMutex); return mSmoothedSignal; }
	
	const TouchTracker::TouchArray& getTouchArray() { return mTouchArray; }
	
	bool isWithinTrackerCalibrateArea(int i, int j);
	const int getHistoryCtr() { return mHistoryCtr; }
	
	const std::vector<ZonePtr>& getZones(){ return mZones; }
    const CriticalSection* getZoneLock() {return &mZoneLock;}

    void setStateFromJSON(cJSON* pNode, int depth);
    bool loadZonePresetByName(const std::string& name);

	int getDeviceState(void);
	int getClientState(void);

	SoundplaneMIDIOutput& getMIDIOutput() { return mMIDIOutput; }

	Vec2 xyToKeyGrid(Vec2 xy);

private:

	// TODO order!
	void trackTouches(const SensorFrame& frame);
	void initialize();
	void clearTouchData();
	void scaleTouchPressureData();
	void sendTouchDataToZones();
	void sendMessageToListeners();

	void addListener(SoundplaneDataListener* pL) { mListeners.push_back(pL); }
	SoundplaneListenerList mListeners;

    void clearZones();
    void sendParametersToZones();
    void addZone(ZonePtr pz);

    CriticalSection mZoneLock;
    std::vector<ZonePtr> mZones;
    MLSignal mZoneMap;

	bool mOutputEnabled;

	static const int miscStrSize = 256;
    void loadZonesFromString(const std::string& zoneStr);

	void doInfrequentTasks();
	uint64_t mLastInfrequentTaskTime;

	/**
	 * Please note that it is not safe to access this member from the processing
	 * thread: It is nulled out by the destructor before the SoundplaneDriver
	 * is torn down. (It would not be safe to not null it out either because
	 * then the pointer would point to an object that's being destroyed.)
	 */
	std::unique_ptr<SoundplaneDriver> mpDriver;
	int mSerialNumber;

	SoundplaneMIDIOutput mMIDIOutput;
	SoundplaneOSCOutput mOSCOutput;
    SoundplaneDataMessage mMessage;

	uint64_t mLastTimeDataWasSent;

	SensorFrame mSensorFrame;
	MLSignal mSurface; 

	int	mMaxTouches;
	TouchTracker::TouchArray mTouchArray; 
	MLSignal mTouchFrame;
	MLSignal mTouchHistory;	

	bool mTesting;
	bool mCalibrating;
	bool mSelectingCarriers;
	bool mRaw;
    bool mSendMatrixData;

	// when on, calibration tries to collect the lowest noise carriers to use.  otherwise a default set is used.
	//
	bool mDynamicCarriers;
	SoundplaneDriver::Carriers mCarriers;

	bool mHasCalibration;

	SensorFrameStats mStats;
	SensorFrame mCalibrateMeanInv;
	
	MLSignal mRawSignal;
	std::mutex mRawSignalMutex;
	
	MLSignal mCalibratedSignal;
	std::mutex mCalibratedSignalMutex;

	SensorFrame mSmoothedFrame;
	MLSignal mSmoothedSignal;
	std::mutex mSmoothedSignalMutex;

	int mCalibrateStep; // calibrate step from 0 - end
	int mTotalCalibrateSteps;
	int mSelectCarriersStep;

	float mSurfaceWidthInv;
	float mSurfaceHeightInv;

    // store current key for each touch to implement hysteresis.
	int mCurrentKeyX[kSoundplaneMaxTouches];
	int mCurrentKeyY[kSoundplaneMaxTouches];

	float mZ1[kSoundplaneMaxTouches];

	char mHardwareStr[miscStrSize];
	char mStatusStr[miscStrSize];
	char mClientStr[miscStrSize];

	TouchTracker mTracker;

	int mHistoryCtr;
	int mTestCtr;

	int mZoneModeTemp;
	bool mCarrierMaskDirty;
	bool mNeedsCarriersSet;
	bool mNeedsCalibrate;
	unsigned long mCarriersMask;
	
	bool mDoOverrideCarriers;
	SoundplaneDriver::Carriers mOverrideCarriers;
	
	int mTest;

	std::vector<float> mMaxNoiseByCarrierSet;
	std::vector<float> mMaxNoiseFreqByCarrierSet;

	bool mKymaMode;
	int mKymaIsConnected; // TODO more custom clients

    MLFileCollectionPtr mTouchPresets;
    MLFileCollectionPtr mZonePresets;

	// OSC services
	std::vector<std::string> mServiceNames;
	
	void infrequentTaskThread();
	std::thread					mInfrequentTaskThread;
	
	void processThread();
	std::thread					mProcessThread;
	
	int mShuttingDown;
};

// JSON utilities (to go where?)
std::string getJSONString(cJSON* pNode, const char* name);
double getJSONDouble(cJSON* pNode, const char* name);
int getJSONInt(cJSON* pNode, const char* name);

#endif // __SOUNDPLANE_MODEL__
