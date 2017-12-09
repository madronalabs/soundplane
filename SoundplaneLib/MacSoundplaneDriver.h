
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
const int kIsochBuffersInFlight = 4; // AppleUSBAudioStream.h: 6
const int kIsochFramesPerTransaction = 8; // MLTEST 20
const int kIsochStartupFrames = 250;

class MacSoundplaneDriver : public SoundplaneDriver
{
	// isoch completion routine is allowed to set our device state.
	friend void isochComplete(void *refCon, IOReturn result, void *arg0);

public:
	MacSoundplaneDriver(SoundplaneDriverListener& m);
	~MacSoundplaneDriver();
	
	int getDeviceState() const override
	{
		return mDeviceState;
	}	
	
	void close() override;
	
	uint16_t getFirmwareVersion() const override;
	std::string getSerialNumberString() const override;
	
	const unsigned char *getCarriers() const override;
	void setCarriers(const Carriers& carriers) override;
	void enableCarriers(unsigned long mask) override;
	
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
	
	struct EndpointReader
	{
		FramePosition position;
		uint16_t seqNum = 0;
		uint16_t lost = 1;
	};
	
	FramePosition advance (FramePosition a, int d)
	{		
		uint16_t newFrame = a.frame;
		uint16_t newBuffer = a.buffer;
		if(d > 0)
		{
			for(int n=0; n<d; ++n)
			{
				if(++newFrame >= kIsochFramesPerTransaction)
				{
					newFrame = 0;
					if(++newBuffer >= kNumIsochBuffers)
					{
						newBuffer = 0;
					}
				}
			}
		}
		else if(d < 0)
		{
			for(int n=0; n<d; ++n)
			{
				if(--newFrame < 0)
				{
					newFrame = kIsochFramesPerTransaction - 1;
					if(--newBuffer < 0)
					{
						newBuffer = kNumIsochBuffers - 1;
					}
				}
			}
		}
		
		return FramePosition (newBuffer, newFrame);
	}

protected:
	
	void setDeviceState(int state) { mDeviceState = state; }	
	
private:
	
	struct K1IsocTransaction
	{
		UInt64						busFrameNumber = 0;
		MacSoundplaneDriver			*parent = 0;
		IOUSBLowLatencyIsocFrame	*isocFrames = nullptr;
		unsigned char				*payloads = nullptr;
		uint8_t						endpointNum = 0;
		uint8_t						endpointIndex = 0;
		uint8_t						bufIndex = 0;
		
		uint16_t getTransactionSequenceNumber(int f);
		IOReturn getTransactionStatus(int n);
		void setSequenceNumber(int f, uint16_t s);
	};
	
	std::array< EndpointReader, kSoundplaneANumEndpoints > mEndpointReaders;
	
	int createLowLatencyBuffers();
	int destroyLowLatencyBuffers();
	
	IOReturn scheduleIsoch(K1IsocTransaction *t);
	static void isochComplete(void *refCon, IOReturn result, void *arg0);
	void resetIsochTransactions();
	void resetIsochIfStalled();
	
	void addOffset(int& buffer, int& frame, int offset);
	uint16_t getTransferBytesReceived(int endpoint, int buffer, int frame, int offset = 0);
	AbsoluteTime getTransferTimeStamp(int endpoint, int buffer, int frame, int offset = 0);
	IOReturn getTransferStatus(int endpoint, int buffer, int frame, int offset = 0);
	uint16_t getSequenceNumber(int endpoint, int buf, int frame, int offset = 0);
	unsigned char* getPayloadPtr(int endpoint, int buf, int frame, int offset = 0);
	unsigned char getPayloadLastByte(int endpoint, int buffer, int frame);
	
	IOReturn setBusFrameNumber();
	static void deviceAdded(void *refCon, io_iterator_t iterator);
	static void deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument);

	void resetEndpointReaders();
	int advanceEndpointReader(int endpointIdx, uint16_t destSeqNum);
	
	void grabThread();
	void process(SensorFrame* pOut);	
	void processThread();
	
	static int getStringDescriptor(IOUSBDeviceInterface187 **dev, uint8_t descIndex, char *destBuf, uint16_t maxLen, uint16_t lang);	
	K1IsocTransaction* getTransactionData(int endpoint, int buf) { return mTransactionData + kNumIsochBuffers*endpoint + buf; }
	uint32_t getTransactionDataChecksum();
	
	int mTransactionsInFlight;

	std::thread					mGrabThread;
	std::thread					mProcessThread;
	
	IONotificationPortRef		notifyPort;
	io_iterator_t				matchedIter;
	io_object_t					notification;
	
	IOUSBDeviceInterface187		**dev;
	IOUSBInterfaceInterface192	**intf;
	
	// TODO move to reader
	UInt64						mNextBusFrameNumber[kSoundplaneANumEndpoints];
	uint8_t						payloadIndex[kSoundplaneANumEndpoints];
	
	K1IsocTransaction			mTransactionData[kSoundplaneANumEndpoints * kNumIsochBuffers];
	
	uint16_t mSequenceNum;

	int mDeviceState;
	std::mutex mDeviceStateMutex;
	
	unsigned char mCurrentCarriers[kSoundplaneNumCarriers];

	SoundplaneDriverListener& mListener;
	
	// MLTEST
	SensorFrame                 mWorkingFrame;
	SensorFrame                 mPrevFrame;
	
	// stats
	int mFrameCounter;
	int mNoFrameCounter;
	int mGaps{0};
	
	bool mTerminating{false};
		
	int mStartupCtr;
	int mErrorCount;
};

#endif // __MAC_SOUNDPLANE_DRIVER__
