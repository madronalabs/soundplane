
// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_DRIVER__
#define __SOUNDPLANE_DRIVER__

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
#include "pa_memorybarrier.h"
#include "pa_ringbuffer.h"

// Soundplane data format:
// The Soundplane Model A sends frames of data over USB using an isochronous interface with two endpoints. 
// Each endpoint carries the data for one of the two sensor boards in the Soundplane. There is a left sensor board, endpoint 0,
// and a right sensor board, endpoint 1.  Each sensor board has 8 pickups (horizontal) and 32 carriers (vertical)
// for a total of 256 taxels of data. 
// 
// The data is generated from FFTs run on the DSP inside the Soundplane. The sampling rate is 125000 Hz, which is created by
// the Soundplane processor’s internal clock dividing a 12 mHz crystal clock by 96. An FFT is performed every 128 samples
// to make data blocks for each endpoint at a post-FFT rate of 976.5625 Hz. 
//
// Each data block for a surface contains 256 12-bit taxels packed into 192 16-bit words, followed by one 16-bit
// sequence number for a total of 388 bytes. The taxel data are packed as follows:
// 12 bits taxel 1 [hhhhmmmmllll]
// 12 bits taxel 2 [HHHHMMMMLLLL]
// 24 bits combined in three bytes: [mmmmllll LLLLhhhh HHHHMMMM]
//
// The packed data are followed by a 16-bit sequence number.  
// Two bytes of padding are also present in the data packet. A full packet is always requested, and the
// Soundplane hardware returns either 0, or the data minus the padding. The padding is needed because for some
// arcane reason the data sent should be less than the negotiated size. So the negotiated size includes the
// padding (388 bytes) while 386 bytes are typically received in any transaction. 

// Soundplane A hardware
const int kSoundplaneASampleRate = 125000;
const int kSoundplaneAFFTSize = 128;
const int kSoundplaneANumCarriers = 32;
const int kSoundplaneAPickupsPerBoard = 8;
const int kSoundplaneATaxelsPerSurface = kSoundplaneANumCarriers*kSoundplaneAPickupsPerBoard; // 256
const int kSoundplaneSensorWidth = 32;
const int kSoundplanePossibleCarriers = 64;
const int kSoundplaneWidth = 64;
const int kSoundplaneHeight = 8;

// Soundplane A USB firmware
const int kSoundplaneANumEndpoints = 2;
const int kSoundplaneAEndpointStartIdx = 1;
const int kSoundplaneADataBitsPerTaxel = 12;
const int kSoundplaneAPackedDataSize = (kSoundplaneATaxelsPerSurface * kSoundplaneADataBitsPerTaxel / 8); // 384 bytes

typedef struct
{
	char packedData[kSoundplaneAPackedDataSize];
	UInt16 seqNum;
	UInt16 padding;
}	SoundplaneADataPacket; // 388 bytes

// Soundplane A OSX client software
const int kSoundplaneABuffersExp = 3;
const int kSoundplaneABuffers = 1 << kSoundplaneABuffersExp;
const int kSoundplaneABuffersMask = kSoundplaneABuffers - 1;
const int kSoundplaneABuffersInFlight = 4;
const int kSoundplaneANumIsochFrames = 20;
const int kSoundplaneOutputBufFrames = 128;
const int kSoundplaneStartupFrames = 50;
extern const unsigned char kDefaultCarriers[kSoundplaneSensorWidth];

// isoc frame data update rate in ms. see LowLatencyReadIsochPipeAsync docs in IOUSBLib.h.
const int kSoundplaneAUpdateFrequency = 1;

// device name. someday, an array of these
//
extern const char* kSoundplaneAName;

// USB device requests and indexes
//
typedef enum
{
	kRequestStatus = 0,
	kRequestMask = 1,
	kRequestCarriers = 2
} MLSoundplaneUSBRequest;

typedef enum
{
	kRequestCarriersIndex = 0,
	kRequestMaskIndex = 1
} MLSoundplaneUSBRequestIndex;

// device states
//
typedef enum
{
	kNoDevice = 0,
	kDeviceConnected = 1,
	kDeviceHasIsochSync = 2,
	kDeviceIsTerminating = 3,
	kDeviceSuspend = 4,
	kDeviceResume = 5
} MLSoundplaneState;

// device states
//
typedef enum
{
	kDevNoErr = 0,
	kDevDataDiffTooLarge = 1,
	kDevGapInSequence = 2
} MLSoundplaneErrorType;

void K1_unpack_float2(unsigned char *pSrc0, unsigned char *pSrc1, float *pDest);
	
class SoundplaneDriverListener
{
public:
	SoundplaneDriverListener() {}
	virtual ~SoundplaneDriverListener() {}
	virtual void deviceStateChanged(MLSoundplaneState s) = 0;
	virtual void handleDeviceError(int errorType, int di1, int di2, float df1, float df2) = 0;
	virtual void handleDeviceDataDump(float* pData, int size) = 0;
};

void * soundplaneProcessThread(void *arg);
void deviceAdded(void *refCon, io_iterator_t iterator);
void deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument);
void isochComplete(void *refCon, IOReturn result, void *arg0);

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
	SoundplaneDriver();
	~SoundplaneDriver();
	
	void init();
	void shutdown();
	int getSerialNumber();
	int readSurface(float* pDest);
	void flushOutputBuffer();
	float carrierToFrequency(int carrier);
	int setCarriers(const unsigned char *carriers);
	int enableCarriers(unsigned long mask);
	void setDefaultCarriers();
	void dumpCarriers();

	void						*clientRef;
	pthread_t					mGrabThread;
	pthread_t					mProcessThread;

	IONotificationPortRef		notifyPort;
	io_iterator_t				matchedIter;
	io_object_t					notification;

	IOUSBDeviceInterface187		**dev;
	IOUSBInterfaceInterface192	**intf;
	
	UInt64						busFrameNumber[kSoundplaneANumEndpoints];
	K1IsocTransaction*			mpTransactionData;
	UInt8						payloadIndex[kSoundplaneANumEndpoints];

	inline K1IsocTransaction* getTransactionData(int endpoint, int buf) { return mpTransactionData + kSoundplaneABuffers*endpoint + buf; }

	UInt16 getFirmwareVersion();
	int getSerialNumberString(char* destStr, int maxLen);

	MLSoundplaneState getDeviceState();
	
	void addListener(SoundplaneDriverListener* pL) { mListeners.push_back(pL); }
	int mTransactionsInFlight;
	int startupCtr;
	
private:
	// these fns in global namespace need to set the device state.
	//
	friend void * ::soundplaneProcessThread(void *arg);
	friend void ::deviceAdded(void *refCon, io_iterator_t iterator);
	friend void ::deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument);
	friend void ::isochComplete(void *refCon, IOReturn result, void *arg0);

	void addOffset(int& buffer, int& frame, int offset);
	UInt16 getTransferBytesRequested(int endpoint, int buffer, int frame, int offset = 0);
	UInt16 getTransferBytesReceived(int endpoint, int buffer, int frame, int offset = 0);
	AbsoluteTime getTransferTimeStamp(int endpoint, int buffer, int frame, int offset = 0);
	IOReturn getTransferStatus(int endpoint, int buffer, int frame, int offset = 0);
	UInt16 getSequenceNumber(int endpoint, int buf, int frame, int offset = 0);
	unsigned char* getPayloadPtr(int endpoint, int buf, int frame, int offset = 0);

	void reclockFrameToBuffer(float* pSurface);
	void setDeviceState(MLSoundplaneState n);
	void reportDeviceError(int errCode, int d1, int d2, float df1, float df2);
	void dumpDeviceData(float* pData, int size);
	void removeDevice();
	
	MLSoundplaneState mState;
	char mDescStr[256]; // max descriptor length.
	char mSerialString[64]; // scratch string
	unsigned char mCurrentCarriers[kSoundplaneSensorWidth];
	float * mpOutputData;
	PaUtilRingBuffer mOutputBuf;	
	std::list<SoundplaneDriverListener*> mListeners;
};

inline UInt16 getTransactionSequenceNumber(K1IsocTransaction* t, int f) 
{ 
	if (!t->payloads) return 0;
	SoundplaneADataPacket* p = (SoundplaneADataPacket*)t->payloads;
	return p[f].seqNum; 
}

inline void setSequenceNumber(K1IsocTransaction* t, int f, UInt16 s) 
{ 
	SoundplaneADataPacket* p = (SoundplaneADataPacket*)t->payloads;
	p[f].seqNum = s;
}

void setThreadPriority (pthread_t inThread, UInt32 inPriority, Boolean inIsFixed);
UInt32 getThreadPriority(pthread_t inThread, int inWhichPriority);
UInt32 getThreadPolicy(pthread_t inThread);
void displayThreadInfo(pthread_t inThread);
void dumpTransactions(void *arg, int bufferIndex, int frameIndex);
int GetStringDescriptor(IOUSBDeviceInterface187 **dev, UInt8 descIndex, char *destBuf, UInt16 maxLen, UInt16 lang);
void show_io_err(const char *msg, IOReturn err);
void show_kern_err(const char *msg, kern_return_t kr);
const char *io_err_string(IOReturn err);

#endif // __SOUNDPLANE_DRIVER__
