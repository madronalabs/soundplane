
// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __MAC_SOUNDPLANE_DRIVER__
#define __MAC_SOUNDPLANE_DRIVER__

#include <thread>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/usb/USB.h>
#include <IOKit/IOKitLib.h>

#include <IOKit/usb/IOUSBLib.h>

#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOReturn.h>

#include "SoundplaneDriver.h"
#include "SoundplaneModelA.h"

// constants that affect isochronous transfers
const int kIsochBuffersExp = 3; // MLTEST 3
const int kNumIsochBuffers = 1 << kIsochBuffersExp;
const int kIsochBuffersMask = kNumIsochBuffers - 1;
const int kIsochBuffersInFlight = 6; // AppleUSBAudioStream.h: 6
const int kIsochFramesPerTransaction = 8; // MLTEST 20
const int kIsochStartupFrames = 250; // MLTEST 500

const int kPayloadBufferSize = sizeof(SoundplaneADataPacket)*kIsochFramesPerTransaction;

// isoc frame data update rate in ms. see LowLatencyReadIsochPipeAsync docs in IOUSBLib.h.
// This number is part of the lower limit on our possible latency.
const int kIsochUpdateFrequency = 1;

const int kMaxErrorStringSize = 256;

class MacSoundplaneDriver : public SoundplaneDriver
{
	// isoch completion routine is allowed to set our device state. (TODO YUK)
	friend void isochComplete(void *refCon, IOReturn result, void *arg0);

public:
	MacSoundplaneDriver(SoundplaneDriverListener& m);
	~MacSoundplaneDriver();
    
		
    // SoundplaneDriver
    void start() override;
	uint16_t getFirmwareVersion() const override;
	std::string getSerialNumberString() const override;
	const unsigned char *getCarriers() const override;
	void setCarriers(const Carriers& carriers) override;
	void enableCarriers(unsigned long mask) override;
    int getSerialNumber() const override;

	std::mutex& getDeviceStateMutex() { return mDeviceStateMutex; }
	
	void destroyDevice();
	
	class FramePosition
	{
	public:
		FramePosition() : buffer(0), frame(0){}
		FramePosition(int b, int f) : buffer(b), frame(f){}
		
		uint16_t buffer = 0;
		uint16_t frame = 0;
	};
	
    // advance the FramePosition, wrapping to frame and then buffer.
	FramePosition advance(FramePosition a)
	{		
		uint16_t newFrame = a.frame;
		uint16_t newBuffer = a.buffer;

        if(++newFrame >= kIsochFramesPerTransaction)
        {
            newFrame = 0;
            if(++newBuffer >= kNumIsochBuffers)
            {
                newBuffer = 0;
            }
        }

		return FramePosition (newBuffer, newFrame);
	}

    bool mUnplugged{true};
    
protected:
	
    inline int getDeviceState() const override
    {
        if(mDeviceState == kDeviceHasIsochSync)
        {
            if(mUnplugged) return kDeviceUnplugged;
        }
        return mDeviceState;
    }
    
	inline void setDeviceState(int state) { mDeviceState = state; }
	
private:
	
	struct K1IsocTransaction
	{
		UInt64						busFrameNumber = 0;
		MacSoundplaneDriver			*parent = 0;
		IOUSBLowLatencyIsocFrame	*isocFrames = nullptr;
		unsigned char				*payloads = nullptr;

		uint8_t						endpointIndex = 0;
		uint8_t						bufIndex = 0;
		
		uint16_t getTransactionSequenceNumber(int f);
		IOReturn getTransactionStatus(int n);
		void setSequenceNumber(int f, uint16_t s);
	};
	
	int createLowLatencyBuffers();
	int destroyLowLatencyBuffers();
	
	IOReturn scheduleIsoch(int endpointIndex);
	static void isochComplete(void *refCon, IOReturn result, void *arg0);
    void resetBusFrameNumbers();
	void resetIsochTransactions();
	void resetIsochIfStalled();
	
	void addOffset(int& buffer, int& frame, int offset);
    uint16_t getTransferBytesReceived(int endpoint, int buffer, int frame);
    unsigned char getPayloadLastByte(int endpoint, int buffer, int frame);
    void clearPayloads(K1IsocTransaction* t);
    void clearAllPayloads();

 
    bool frameWasReceived(int endpoint, int buffer, int frame);
    AbsoluteTime getTransferTimeStamp(int endpoint, int buffer, int frame);
	IOReturn getTransferStatus(int endpoint, int buffer, int frame);
	uint16_t getSequenceNumber(int endpoint, int buf, int frame);
    unsigned char* getPayloadPtr(int endpoint, int buf, int frame);
    
    FramePosition getPositionOfSequence(int endpoint, uint16_t seq);
 	
	static void deviceAdded(void *refCon, io_iterator_t iterator);
	static void deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument);

    uint16_t mostRecentSequenceNum(int endpointIdx);
    bool endpointDataHasSequence(int endpoint, uint16_t seq);
    
    bool sequenceIsComplete(uint16_t seq);
    int mostRecentCompleteSequence();
	
    void printTransactions();
	void grabThread();
	void process();	
	void processThread();
	
	static int getStringDescriptor(IOUSBDeviceInterface187 **dev, uint8_t descIndex, char *destBuf, uint16_t maxLen, uint16_t lang);
    
	K1IsocTransaction* getTransactionData(int endpoint, int buf) { return mTransactionData + kNumIsochBuffers*endpoint + buf; }
    
	uint32_t getTransactionDataChecksum();
	
    // we are only counting, so any races are benign
    std::atomic<int> mTransactionsInFlight{0};
    
    int getNextBufferIndex(int endpoint);
    std::array<int, kSoundplaneANumEndpoints> mNextBufferIndex {};
    std::array<std::mutex, kSoundplaneANumEndpoints>  mNextBufferIndexMutex;

	std::thread					mGrabThread;
	std::thread					mProcessThread;
	
	IONotificationPortRef		notifyPort{0};
	io_iterator_t				matchedIter{0};
	io_object_t					notification{0};
	
    IOUSBDeviceInterface187		**dev{nullptr};
	IOUSBInterfaceInterface192	**intf{nullptr};
	
	UInt64						mNextBusFrameNumber[kSoundplaneANumEndpoints];
	uint8_t						payloadIndex[kSoundplaneANumEndpoints];
	
	K1IsocTransaction			mTransactionData[kSoundplaneANumEndpoints * kNumIsochBuffers];
	
    uint16_t mSequenceNum{0};
    uint16_t mNextSequenceNum{1};
    uint16_t mPreviousSeq{0};

	int mDeviceState = kNoDevice;
	std::mutex mDeviceStateMutex;
	
	unsigned char mCurrentCarriers[kSoundplaneNumCarriers];

	SoundplaneDriverListener& mListener;
	
    SensorFrame                 mWorkingFrame{};
    SensorFrame                 mPrevFrame{};
	
	// stats
    int mFrameCounter{0};
    int mWaitCounter{0};
    int mErrorCounter{0};
    int mTotalWaitCounter{0};
    int mTotalErrorCounter{0};
	int mGaps{0};
		
    int mStartupCtr{0};
    int mTestCtr{0};
    char mErrorBuf[kMaxErrorStringSize];
    
    bool mTerminating{false};
};



#endif // __MAC_SOUNDPLANE_DRIVER__


#ifdef SHOW_ALL_SEQUENCE_NUMBERS


for(int endpointIdx = 0; endpointIdx < kSoundplaneANumEndpoints; ++endpointIdx)
{
    std::cout << "endpoint " << endpointIdx << ": \n" ;
    for(int bufIdx = 0; bufIdx < kNumIsochBuffers; ++bufIdx)
    {
        std::cout << "    buffer " << bufIdx << ": " ;
        for(int frameIdx=0; frameIdx<kIsochFramesPerTransaction; ++frameIdx)
        {
            int frameSeqNum = getSequenceNumber(endpointIdx, bufIdx, frameIdx);
            int actCount = getTransferBytesReceived(endpointIdx, bufIdx, frameIdx);
            unsigned char b = getPayloadLastByte(endpointIdx, bufIdx, frameIdx);
            printf("%05d[%03d][%02x] ", frameSeqNum, actCount, b);
        }
        std::cout << "\n";
    }
    }
#endif
    
    
