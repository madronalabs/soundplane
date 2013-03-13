
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_MODEL__
#define __SOUNDPLANE_MODEL__

#include "MLTime.h"
#include "MLModel.h"
#include "SoundplaneDriver.h"
#include "MLOSCListener.h"
#include "NetService.h"
#include "NetServiceBrowser.h"
#include "TouchTracker.h"
#include "SoundplaneMIDIOutput.h"
#include "SoundplaneOSCOutput.h"
#include "MLSymbol.h"
#include "MLParameter.h"
#include <list>
#include <map>

const int kSoundplaneMaxTouches = 16;
const int kSoundplaneCalibrateSize = 1024;
const int kSoundplaneHistorySize = 2048;
const float kSoundplaneSampleRate = 1000.f;
const float kZeroFilterFrequency = 10.f;
const int kSoundplaneKeyWidth = 30;
const int kSoundplaneKeyHeight = 5;
const float kSoundplaneVibratoAmount = 4.;

typedef enum SoundplaneViewMode
{
	kRaw = 0,
	kCalibrated = 1,
	kCooked = 2,
	kXY = 3,
	kTest = 4
};

class ColumnStats
{
public:
	int column;
	float freq;
	float sum;
	float mean;
	float stdDev;
};

class SoundplaneModel :
	public SoundplaneDriverListener,
	public TouchTracker::Listener,
	public MLOSCListener,
	public MLModel
{

public:
	SoundplaneModel();
	~SoundplaneModel();	

	// SoundplaneDriverListener
	void deviceStateChanged(MLSoundplaneState s);
	
	// TouchTracker::Listener
	void hasNewCalibration(const MLSignal& cal, const MLSignal& norm, float avgDist);

	// MLOSCListener
	void ProcessMessage(const osc::ReceivedMessage &m, const IpEndpointName& remoteEndpoint);
	
	// MLModel
	void setModelParam(MLSymbol p, float v);
	void setModelParam(MLSymbol p, const std::string& v); 
	void setModelParam(MLSymbol p, const MLSignal& v); 
	
	void initialize();
	void clearTouchData();
	void postProcessTouchData();
	void sendTouchDataToClients();
	void notifyClients(int c);
	
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
	bool isCalibrating() { return mCalibrating; }	
	float getCalibrateProgress();
	void endCalibrate();

	void beginSelectCarriers();
	void nextSelectCarriersStep();
	void endSelectCarriers();

	void setFilter(bool b);
	
	void getMinMaxHistory(int n);
	const MLSignal& getCorrelation();
	
	void setHysteresis(float f) { mTracker.setHysteresis(f); }
	void setTaxelsThresh(int t) { mTracker.setTaxelsThresh(t); }

	const MLSignal& getTouchFrame() { return mTouchFrame; }
	const MLSignal& getTouchHistory() { return mTouchHistory; }
	const MLSignal& getRawSignal() { return mRawSignal; }
	const MLSignal& getCalibratedSignal() { return mCalibratedSignal; }
	const MLSignal& getCookedSignal() { return mCookedSignal; }
	const MLSignal& getTestSignal() { return mTestSignal; }
	const MLSignal& getSignalForViewMode(SoundplaneViewMode m);
	const MLSignal& getTrackerCalibrateSignal();
	Vec3 getTrackerCalibratePeak();
	const int getHistoryCtr() { return mHistoryCtr; }

	// TEMP
	void setZoneMode(int m) { mZoneModeTemp = m; setupZones(); }
	
	int getDeviceState(void);
	int getClientState(void);

	SoundplaneMIDIOutput& getMIDIOutput() { return mMIDIOutput; } 
	SoundplaneOSCOutput& getOSCOutput() { return mOSCOutput; }  // TODO NO
	
	void setKymaMode(bool m);
	void beginCalibrateTracker();
	void cancelCalibrateTracker();
	bool trackerIsCalibrating();
	Vec2 getTrackerCalibrateDims() { return Vec2(kCalibrateWidth, kCalibrateHeight); }	

	Vec2 xyToKeyGrid(Vec2 xy);
	Vec2 keyGridToXY(Vec2 g);
		
private:	

	void addDataListener(SoundplaneDataListener* pL) { mDataListeners.push_back(pL); }
	std::vector<SoundplaneDataListener*> mDataListeners;
	
	MLSoundplaneState mDeviceState;
	bool mOutputEnabled;
	
	static const int miscStrSize = 256;
	
	std::vector<ColumnStats> mCarrierStats;

	void setupZones();
	
	void doInfrequentTasks();
	UInt64 mLastInfrequentTaskTime;

	SoundplaneDriver* mpDriver;
	int mSerialNumber;
	
	SoundplaneMIDIOutput mMIDIOutput;
	SoundplaneOSCOutput mOSCOutput;
		
	UInt64 mLastTimeDataWasSent;
	
	MLSignal mSurface;
	MLSignal mCalibrateData;
	
	int	mMaxTouches;
	MLSignal mTouchFrame;
	MLSignal mTouchHistory;

	bool mCalibrating;
	bool mSelectingCarriers;
	bool mRaw;
	
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
	MLSignal mZoneMap;
	
	int mCalibrateCount; // samples in one calibrate step
	int mCalibrateStep; // calibrate step from 0 - end
	int mTotalCalibrateSteps;
	int mSelectCarriersStep;
	
	float mSampleRate;
	float mSnapFreq;
	
	float mSurfaceWidthInv;
	float mSurfaceHeightInv;
	
	Biquad2D mNotchFilter;
	Biquad2D mLopassFilter;
	std::vector<Biquad> mNoteFilters;
	std::vector<Biquad> mVibratoFilters;
	float mNoteLock[kSoundplaneMaxTouches];
	int mCurrentKeyX[kSoundplaneMaxTouches];
	int mCurrentKeyY[kSoundplaneMaxTouches];
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
	
	std::vector<float> mMaxNoiseByCarrierSet;
	std::vector<float> mMaxNoiseFreqByCarrierSet;
	
	int mKymaIsConnected; // TODO more custom clients

};


#endif // __SOUNDPLANE_MODEL__