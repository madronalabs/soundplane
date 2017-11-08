
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

class MacSoundplaneDriver : public SoundplaneDriver
{
public:
	MacSoundplaneDriver();
	~MacSoundplaneDriver();

	virtual SoundplaneDriver::returnValue process(SensorFrame* pOut) override;

	virtual uint16_t getFirmwareVersion() const override;
	virtual std::string getSerialNumberString() const override;

	virtual const unsigned char *getCarriers() const override;
	virtual void setCarriers(const Carriers& carriers) override;
	virtual void enableCarriers(unsigned long mask) override;

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
		void setSequenceNumber(int f, uint16_t s);
	};

	int createLowLatencyBuffers();
	int destroyLowLatencyBuffers();
	
	virtual int getDeviceState() const override
	{
		return mState;
	}	

	inline void setDeviceState(int n)
	{
		std::lock_guard<std::mutex> lock(mDeviceStateLock);
		mState = n;
	}

	IOReturn scheduleIsoch(K1IsocTransaction *t);
	static void isochComplete(void *refCon, IOReturn result, void *arg0);

	void addOffset(int& buffer, int& frame, int offset);
	uint16_t getTransferBytesReceived(int endpoint, int buffer, int frame, int offset = 0);
	AbsoluteTime getTransferTimeStamp(int endpoint, int buffer, int frame, int offset = 0);
	IOReturn getTransferStatus(int endpoint, int buffer, int frame, int offset = 0);
	uint16_t getSequenceNumber(int endpoint, int buf, int frame, int offset = 0);
	unsigned char* getPayloadPtr(int endpoint, int buf, int frame, int offset = 0);

	IOReturn setBusFrameNumber();
	static void deviceAdded(void *refCon, io_iterator_t iterator);
	static void deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument);

	void grabThread();
	void destroyDevice();

	static int getStringDescriptor(IOUSBDeviceInterface187 **dev, uint8_t descIndex, char *destBuf, uint16_t maxLen, uint16_t lang);
	void dumpTransactions(int bufferIndex, int frameIndex);
	K1IsocTransaction* getTransactionData(int endpoint, int buf) { return transactionData + kSoundplaneABuffers*endpoint + buf; }

	int mTransactionsInFlight;
	int startupCtr;

	std::thread					mGrabThread;

	IONotificationPortRef		notifyPort;
	io_iterator_t				matchedIter;
	io_object_t					notification;

	IOUSBDeviceInterface187		**dev;
	IOUSBInterfaceInterface192	**intf;

	UInt64						busFrameNumber[kSoundplaneANumEndpoints];
	K1IsocTransaction			transactionData[kSoundplaneANumEndpoints * kSoundplaneABuffers];
	uint8_t						payloadIndex[kSoundplaneANumEndpoints];

	int mState;
	std::mutex mDeviceStateLock;
	
	unsigned char mCurrentCarriers[kSoundplaneNumCarriers];
	
	// TODO refactor duplicate signal code here
	
	// used by grab thread to signal presence of a new device
	bool mDeviceFound;
	std::mutex mDeviceFoundLock;
	
	// TODO: this would be a great place to use a compare and swap
	inline void setDeviceFound()
	{
		std::lock_guard<std::mutex> lock(mDeviceFoundLock);
		mDeviceFound = true;
	}
	inline bool wasDeviceFound()
	{
		std::lock_guard<std::mutex> lock(mDeviceFoundLock);
		bool r = mDeviceFound;
		mDeviceFound = false;
		return r;
	}
	
	
	// used to signal removal of a device
	bool mDeviceRemoved;
	std::mutex mDeviceRemovedLock;
	
	// TODO: this would be a great place to use a compare and swap
	inline void setDeviceRemoved()
	{
		std::lock_guard<std::mutex> lock(mDeviceRemovedLock);
		mDeviceRemoved = true;
	}
	inline bool wasDeviceRemoved()
	{
		std::lock_guard<std::mutex> lock(mDeviceRemovedLock);
		bool r = mDeviceRemoved;
		mDeviceRemoved = false;
		return r;
	}
	
	// mListener may be nullptr
//	SoundplaneDriverListener* const mListener;
};

#endif // __MAC_SOUNDPLANE_DRIVER__
