
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_MODEL__
#define __SOUNDPLANE_MODEL__

#include <list>
#include <map>

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

class SoundplaneModel :
	public SoundplaneDriverListener,
	public TouchTracker::Listener,
	public MLOSCListener,
	public MLNetServiceHub,
	public MLModel
{
public:

	SoundplaneModel();
	~SoundplaneModel();

	// MLModel
    void doPropertyChangeAction(MLSymbol , const MLProperty & );

	void setAllPropertiesToDefaults();

	// SoundplaneDriverListener
	void deviceStateChanged(MLSoundplaneState s);
	void handleDeviceError(int errorType, int data1, int data2, float fd1, float fd2);
	void handleDeviceDataDump(const float* pData, int size);

	// TouchTracker::Listener
	void hasNewCalibration(const MLSignal& cal, const MLSignal& norm, float avgDist);

	// MLOSCListener
	void ProcessMessage(const osc::ReceivedMessage &m, const IpEndpointName& remoteEndpoint);
	void ProcessBundle(const osc::ReceivedBundle &b, const IpEndpointName& remoteEndpoint);

	// MLNetServiceHub
	void didResolveAddress(NetService *pNetService);

	// OSC services
	void refreshServices();
	const std::vector<std::string>& getServicesList();
	void formatServiceName(const std::string& inName, std::string& outName);

	void initialize();
	void clearTouchData();
	void sendTouchDataToZones();
	void notifyListeners(int c);
    void sendMessageToListeners();

	MLFileCollection& getZonePresetsCollection() { return *mZonePresets; }

	void testCallback();
	void processCallback();
	float getSampleHistory(int x, int y);

	void getHistoryStats(float& mean, float& stdDev);
	int getWidth() { return mSurface.getWidth(); }
	int getHeight() { return mSurface.getHeight(); }

	void setDefaultCarriers();
	void setCarriers(unsigned char* c);
	int enableCarriers(unsigned long mask);
	int getNumCarriers() { return kSoundplaneSensorWidth; }
	void dumpCarriers();

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
	bool isTesting() { return mTesting; }
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
	void setTaxelsThresh(int t) { mTracker.setTaxelsThresh(t); }

	const MLSignal& getTouchFrame() { return mTouchFrame; }
	const MLSignal& getTouchHistory() { return mTouchHistory; }
	const MLSignal& getRawSignal() { return mRawSignal; }
	const MLSignal& getCalibratedSignal() { return mCalibratedSignal; }
	const MLSignal& getCookedSignal() { return mCookedSignal; }
	const MLSignal& getTestSignal() { return mTestSignal; }
	const MLSignal& getTrackerCalibrateSignal();
	Vec3 getTrackerCalibratePeak();
	bool isWithinTrackerCalibrateArea(int i, int j);
	const int getHistoryCtr() { return mHistoryCtr; }

	const MLSignal* getSignalForViewMode(const std::string& m);

    const std::vector<ZonePtr>& getZones(){ return mZones; }
    const CriticalSection* getZoneLock() {return &mZoneLock;}

    void setStateFromJSON(cJSON* pNode, int depth);
    bool loadZonePresetByName(const std::string& name);

	int getDeviceState(void);
	int getClientState(void);

	SoundplaneMIDIOutput& getMIDIOutput() { return mMIDIOutput; }

	void setKymaMode(bool m);
	void beginNormalize();
	void cancelNormalize();
	bool trackerIsCalibrating();
	bool trackerIsCollectingMap();
	void setDefaultNormalize();
	Vec2 getTrackerCalibrateDims() { return Vec2(kCalibrateWidth, kCalibrateHeight); }
	Vec2 xyToKeyGrid(Vec2 xy);

private:
    void dumpZoneMap();

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
	UInt64 mLastInfrequentTaskTime;

	SoundplaneDriver* mpDriver;
	int mSerialNumber;

	SoundplaneMIDIOutput mMIDIOutput;
	SoundplaneOSCOutput mOSCOutput;
    SoundplaneDataMessage mMessage;

	UInt64 mLastTimeDataWasSent;

	MLSignal mSurface;
	MLSignal mCalibrateData;

	int	mMaxTouches;
	MLSignal mTouchFrame;
	MLSignal mTouchHistory;

	bool mCalibrating;
	bool mSelectingCarriers;
	bool mRaw;
    bool mSendMatrixData;

	// when on, calibration tries to collect the lowest noise carriers to use.  otherwise a default set is used.
	//
	bool mDynamicCarriers;
	unsigned char mCarriers[kSoundplaneSensorWidth];

	bool mHasCalibration;
	MLSignal mCalibrateSum;
	MLSignal mCalibrateMean;
	MLSignal mCalibrateMeanInv;
	MLSignal mCalibrateStdDev;

	MLSignal mRawSignal;
	MLSignal mCalibratedSignal;
	MLSignal mCookedSignal;
	MLSignal mTestSignal;
	MLSignal mTempSignal;

	int mCalibrateCount; // samples in one calibrate step
	int mCalibrateStep; // calibrate step from 0 - end
	int mTotalCalibrateSteps;
	int mSelectCarriersStep;

	float mSurfaceWidthInv;
	float mSurfaceHeightInv;

	Biquad2D mNotchFilter;
	Biquad2D mLopassFilter;

    // store current key for each touch to implement hysteresis.
	int mCurrentKeyX[kSoundplaneMaxTouches];
	int mCurrentKeyY[kSoundplaneMaxTouches];

	float mZ1[kSoundplaneMaxTouches];

	char mHardwareStr[miscStrSize];
	char mStatusStr[miscStrSize];
	char mClientStr[miscStrSize];

	TouchTracker mTracker;

	pthread_t	mProcessThread;

	int mHistoryCtr;

	int mZoneModeTemp;
	bool mCarrierMaskDirty;
	bool mNeedsCarriersSet;
	bool mNeedsCalibrate;
	unsigned long mCarriersMask;
	int mTest;
	bool mTesting;

	std::vector<float> mMaxNoiseByCarrierSet;
	std::vector<float> mMaxNoiseFreqByCarrierSet;

	int mKymaIsConnected; // TODO more custom clients

    MLFileCollectionPtr mTouchPresets;
    MLFileCollectionPtr mZonePresets;

	std::map<std::string, MLSignal*> mViewModeToSignalMap;

	// OSC services
	std::vector<std::string> mServiceNames;
};

// JSON utilities (to go where?)
std::string getJSONString(cJSON* pNode, const char* name);
double getJSONDouble(cJSON* pNode, const char* name);
int getJSONInt(cJSON* pNode, const char* name);

#endif // __SOUNDPLANE_MODEL__
