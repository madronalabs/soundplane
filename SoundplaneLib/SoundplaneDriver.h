
// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_DRIVER__
#define __SOUNDPLANE_DRIVER__

#include <thread>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/usb/USB.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOReturn.h>
#include <mach/kern_return.h>

#include <mach/mach.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <list>
#include "pa_ringbuffer.h"

#include "SoundplaneDriverData.h"

class SoundplaneDriverListener
{
public:
	SoundplaneDriverListener() {}
	virtual ~SoundplaneDriverListener() {}

	/**
	 * This callback may be invoked from an arbitrary thread, even from an
	 * interrupt context, so be careful! However, when s is
	 * kDeviceIsTerminating, the callback is never invoked from an interrupt
	 * context, so in that particular case it is safe to block.
	 *
	 * For each state change, there will be exactly one deviceStateChanged
	 * invocation. However, these calls may arrive simultaneously or out of
	 * order, so be careful!
	 *
	 * Please note that by the time the callback is invoked with a given state
	 * s, the SoundplaneDriver might already have moved to another state, so
	 * it might be that s != driver->getDeviceState().
	 */
	virtual void deviceStateChanged(MLSoundplaneState s) = 0;
	/**
	 * This callback may be invoked from an arbitrary thread, but is never
	 * invoked in an interrupt context.
	 */
	virtual void handleDeviceError(int errorType, int di1, int di2, float df1, float df2) = 0;
	/**
	 * This callback may be invoked from an arbitrary thread, but is never
	 * invoked in an interrupt context.
	 */
	virtual void handleDeviceDataDump(float* pData, int size) = 0;
};

class SoundplaneDriver;

typedef struct
{
	UInt64						busFrameNumber;
	SoundplaneDriver			*parent;
	IOUSBLowLatencyIsocFrame	*isocFrames;
	unsigned char				*payloads;
	UInt8						endpointNum;
	UInt8						endpointIndex;
	UInt8						bufIndex;
} K1IsocTransaction;

class SoundplaneDriver
{
public:
	/**
	 * listener may be nullptr
	 */
	SoundplaneDriver(SoundplaneDriverListener* listener);
	~SoundplaneDriver();

	void init();

	int readSurface(float* pDest);
	void flushOutputBuffer();
	MLSoundplaneState getDeviceState();
	UInt16 getFirmwareVersion();
	int getSerialNumber();
	int getSerialNumberString(char* destStr, int maxLen);

	static float carrierToFrequency(int carrier);
	void dumpCarriers();
	int setCarriers(const unsigned char *carriers);
	int enableCarriers(unsigned long mask);
	void setDefaultCarriers();

private:
	IOReturn scheduleIsoch(K1IsocTransaction *t);
	static void isochComplete(void *refCon, IOReturn result, void *arg0);

	void addOffset(int& buffer, int& frame, int offset);
	UInt16 getTransferBytesRequested(int endpoint, int buffer, int frame, int offset = 0);
	UInt16 getTransferBytesReceived(int endpoint, int buffer, int frame, int offset = 0);
	AbsoluteTime getTransferTimeStamp(int endpoint, int buffer, int frame, int offset = 0);
	IOReturn getTransferStatus(int endpoint, int buffer, int frame, int offset = 0);
	UInt16 getSequenceNumber(int endpoint, int buf, int frame, int offset = 0);
	unsigned char* getPayloadPtr(int endpoint, int buf, int frame, int offset = 0);

	IOReturn setBusFrameNumber();
	void removeDevice();
	static void deviceAdded(void *refCon, io_iterator_t iterator);
	static void deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument);

	void grabThread();
	void processThread();

	void reclockFrameToBuffer(float* pSurface);
	void setDeviceState(MLSoundplaneState n);
	void reportDeviceError(int errCode, int d1, int d2, float df1, float df2);
	void dumpDeviceData(float* pData, int size);

	static int getStringDescriptor(IOUSBDeviceInterface187 **dev, UInt8 descIndex, char *destBuf, UInt16 maxLen, UInt16 lang);
	void dumpTransactions(int bufferIndex, int frameIndex);
	K1IsocTransaction* getTransactionData(int endpoint, int buf) { return transactionData + kSoundplaneABuffers*endpoint + buf; }

	int mTransactionsInFlight;
	int startupCtr;

	std::thread					mGrabThread;
	std::thread					mProcessThread;

	IONotificationPortRef		notifyPort;
	io_iterator_t				matchedIter;
	io_object_t					notification;

	IOUSBDeviceInterface187		**dev;
	IOUSBInterfaceInterface192	**intf;

	UInt64						busFrameNumber[kSoundplaneANumEndpoints];
	K1IsocTransaction			transactionData[kSoundplaneANumEndpoints * kSoundplaneABuffers];
	UInt8						payloadIndex[kSoundplaneANumEndpoints];

	std::atomic<MLSoundplaneState> mState;
	char mDescStr[256]; // max descriptor length.
	char mSerialString[64]; // scratch string
	unsigned char mCurrentCarriers[kSoundplaneSensorWidth];
	float mpOutputData[kSoundplaneWidth * kSoundplaneHeight * kSoundplaneOutputBufFrames];
	PaUtilRingBuffer mOutputBuf;

	// mListener may be nullptr
	SoundplaneDriverListener* const mListener;
};

void setThreadPriority(pthread_t inThread, UInt32 inPriority, Boolean inIsFixed);

#endif // __SOUNDPLANE_DRIVER__
