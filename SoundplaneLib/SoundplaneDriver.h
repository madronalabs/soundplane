

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

/*

MLK1_N_SURFACE_TAXELS = the total number of taxels per surface, for Soundplane A 256 (32*8). It can only be used to derive the size of unpacked arrays, which excludes the USB packet data.
MLSP_N_TAXEL_WORDS = the number of 16-bit words in the packed USB data that are dedicated to surface data (without the sequence number).
MLSP_N_TAXEL_BYTES = the number of bytes in the packed USB data that are dedicated to surface data (without the sequence number).
MLSP_N_SURFACE_WORDS = the number of 16-bit words in the packed USB data including both surface data and sequence number.
MLSP_N_SURFACE_BYTES = the number of bytes in the packed USB data including both surface data and sequence number.
MLSP_N_ISOCH_WORDS = the number of 16-bit words for the Isochronous packet size (there is never a packet sent that is actually this long).
MLSP_N_ISOCH_BYTES = the number of bytes declared in the USB descriptors for the Isochronous packet size (there is never a packet sent that is actually this long).

I use MLSP_N_ISOCH_* for interactions directly with the USB API, even though the system should always return a packet that is shorter than this (by two bytes).
MLSP_N_SURFACE_* represents the actual size of the packed data from the Soundplane A, regardless of interpretation.
MLSP_N_TAXEL_* is a handy define to calculate the size of the Isoch packet that represents the surface data without the appended sequence number or any checksum that we might add.

*/

const int kSoundplaneASampleRate = 125000;
const int kSoundplaneAFFTSize = 128;

// K1 (Soundplane A) hardware
#define MLK1_N_USER_INPUTS			8
#define MLK1_N_CARRIERS				32
#define MLK1_N_PICKUPS_PER_BOARD	8
#define MLK1_N_DSP_BITS				16
#define MLK1_N_SURFACE_TAXELS		(MLK1_N_CARRIERS * MLK1_N_PICKUPS_PER_BOARD)

// K1 (Soundplane A) USB firmware
#define MLSP_ALT_INTERFACE_A		2
#define MLSP_N_ISO_ENDPOINTS		2
#define MLSP_EP_SURFACE1			1
#define MLSP_EP_SURFACE2			2
#define MLSP_N_MAGNITUDE_BITS		12
#define MLSP_N_TAXEL_WORDS			(MLK1_N_SURFACE_TAXELS * MLSP_N_MAGNITUDE_BITS / MLK1_N_DSP_BITS)
#define MLSP_N_TAXEL_BYTES			(MLSP_N_TAXEL_WORDS * 2)
#define MLSP_N_SURFACE_WORDS		(MLSP_N_TAXEL_WORDS + 1)
#define MLSP_N_SURFACE_BYTES		(MLSP_N_SURFACE_WORDS * 2)
#define MLSP_N_ISOCH_WORDS			(MLSP_N_SURFACE_WORDS + 1)
#define MLSP_N_ISOCH_BYTES			(MLSP_N_ISOCH_WORDS * 2)

#define MLSP_REQUEST_STATUS			0
#define MLSP_REQUEST_MASK			1
#define MLSP_REQUEST_CARRIERS		2
#define MLSP_REQUEST_INDEX_CARRIERS	0
#define MLSP_REQUEST_INDEX_MASK		1

// user inputs - unused
#define MLSP_EP_USER				3
#define MLSP_N_USER_WORDS			(MLK1_N_USER_INPUTS + 1)
#define MLSP_N_USER_BYTES			(MLSP_N_USER_WORDS * 2)
#define MLSP_MASK_USER_CHANNEL		0x0007
#define MLSP_MASK_USER_VALUE		0xFFC0

// K1 (Soundplane A) OSX client software
#define SP_EXPONENT_N_BUFFERS		3
#define SP_N_BUFFERS				(1<<SP_EXPONENT_N_BUFFERS)
#define SP_MASK_BUFFERS				(SP_N_BUFFERS-1)
#define SP_N_BUFFERS_IN_FLIGHT		4

#define SP_NUM_FRAMES				20
#define SP_BUF_LENGTH				SP_N_BUFFERS * SP_NUM_FRAMES

// isoc frame data updates in ms. see LowLatencyReadIsochPipeAsync docs in IOUSBLib.h.
#define SP_UPDATE_FREQUENCY			1

const int kSoundplaneSensorWidth = 32;
const int kSoundplaneOutputBufFrames = 128;
const int kSoundplanePossibleCarriers = 64;
const int kSoundplaneWidth = 64;
const int kSoundplaneHeight = 8;

// device name. someday, an array of these
//
extern const char* kSoundplaneAName;

extern const unsigned char kDefaultCarriers[kSoundplaneSensorWidth];

// device states
//
typedef enum MLSoundplaneState
{
	kNoDevice = 0,
	kDeviceConnected = 1,
	kDeviceHasIsochSync = 2,
	kDeviceIsTerminating = 3,
	kDeviceSuspend = 4,
	kDeviceResume = 5
};

void K1_unpack_float2(unsigned char *pSrc0, unsigned char *pSrc1, float *pDest);

int SuspendDevice( IOUSBDeviceInterface187 **dev, bool suspend );
	
class SoundplaneDriverListener
{
public:
	SoundplaneDriverListener() {}
	virtual ~SoundplaneDriverListener() {}
	virtual void deviceStateChanged(MLSoundplaneState s) = 0;
};

class SoundplaneDriver;

typedef struct K1IsocTransaction
{
	UInt64						busFrameNumber;
	SoundplaneDriver			*parent;
	IOUSBLowLatencyIsocFrame	*isocFrames;
	UInt16						*payloads;
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
	
	UInt64						busFrameNumber[MLSP_N_ISO_ENDPOINTS];
	UInt16						user[MLSP_N_USER_WORDS];
	K1IsocTransaction*			mpTransactionData;
	UInt8						payloadIndex[MLSP_N_ISO_ENDPOINTS];

	K1IsocTransaction* getTransactionData(int endpoint, int buf) { return mpTransactionData + SP_N_BUFFERS*endpoint + buf; }

	UInt16 getFirmwareVersion();
	int getSerialNumberString(char* destStr, int maxLen);

	MLSoundplaneState getDeviceState();
	
	void addListener(SoundplaneDriverListener* pL) { mListeners.push_back(pL); }
	int mTransactionsInFlight;
	
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
	UInt16* getPayloadPtr(int endpoint, int buf, int frame, int offset = 0);

	void reclockFrameToBuffer(float* pSurface);
	void setDeviceState(MLSoundplaneState n);
	void removeDevice();
	
	MLSoundplaneState mState;
	char mDescStr[256]; // max descriptor length.
	char mSerialString[64]; // scratch string
	unsigned char mCurrentCarriers[kSoundplaneSensorWidth];
	float * mpOutputData;
	PaUtilRingBuffer mOutputBuf;	
	std::list<SoundplaneDriverListener*> mListeners;
};

UInt16 getTransactionSequenceNumber(K1IsocTransaction* t, int f);
inline void setSequenceNumber(K1IsocTransaction* t, int f, UInt16 s) { t->payloads[MLSP_N_ISOCH_WORDS*f + MLSP_N_TAXEL_WORDS] = s; }
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
