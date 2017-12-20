// MacSoundplaneDriver.cpp
//
// Returns raw data frames from the Soundplane.  The frames are reclocked if needed (TODO)
// to reconstruct a steady sample rate.
//
// Two threads are used to do this work.  A grab thread maintains a stream of
// low-latency isochronous transfers. A process thread looks through the buffers
// specified by these transfers every ms or so.  When new frames of data arrive,
// the process thread reclocks them and pushes them to a ring buffer where they
// can be read by clients.

#include "MacSoundplaneDriver.h"

#include <vector>

#include <iostream> // TEMP
#include "ThreadUtility.h"

#if DEBUG
#define VERBOSE
#endif

//#define SHOW_BUS_FRAME_NUMBER
//#define SHOW_ALL_SEQUENCE_NUMBERS

namespace {
	
	// --------------------------------------------------------------------------------
#pragma mark error handling
	
	// REVIEW: IOKit has documentation on handling errors
	const char *io_err_string(IOReturn err)
	{
		static char other[32];
		
		switch (err)
		{
			case kIOReturnSuccess:
				break;
			case KERN_INVALID_ADDRESS:
				return "Specified address is not currently valid";
			case KERN_PROTECTION_FAILURE:
				return "Specified memory is valid, but does not permit the required forms of access";
			case kIOReturnNoDevice:
				return "no such device";
			case kIOReturnAborted:
				return "operation aborted";
			case kIOReturnUnderrun:
				return "data underrun";
			case kIOReturnNoBandwidth:
				return "No Bandwidth: bus bandwidth would be exceeded";
			case kIOReturnIsoTooOld:
				return "isochronous I/O request for distant past!";
			case kIOUSBNotSent2Err:
				return "USB: Transaction not sent";
			case kIOUSBTransactionTimeout:
				return "USB: Transaction timed out";
			case kIOUSBPipeStalled:
				return "Pipe has stalled, error needs to be cleared";
			case kIOUSBLowLatencyFrameListNotPreviouslyAllocated:
				return "Attempted to use user land low latency isoc calls w/out calling PrepareBuffer (on the frame list) first";
			default:
				sprintf(other, "result %#x", err);
				return other;
		}
		return NULL;
	}
	
	void show_io_err(const char *msg, IOReturn err)
	{
		fprintf(stderr, "%s (%08x) %s\n", msg, err, io_err_string(err));
	}
	
	void show_kern_err(const char *msg, kern_return_t kr)
	{
		fprintf(stderr, "%s (%08x)\n", msg, kr);
	}
	
}

// -------------------------------------------------------------------------------
#pragma mark MacSoundplaneDriver

std::unique_ptr<SoundplaneDriver> SoundplaneDriver::create(SoundplaneDriverListener& m)
{
	return std::unique_ptr<MacSoundplaneDriver>(new MacSoundplaneDriver(m));
}

MacSoundplaneDriver::MacSoundplaneDriver(SoundplaneDriverListener& m) : 
mTransactionsInFlight(0),
dev(0),
intf(0),
mDeviceState(kNoDevice),
notifyPort(0),
matchedIter(0),
notification(0),
mListener(m),
mErrorCount(0),
mFrameCounter(0),
mNoFrameCounter(0)
{
	printf("creating SoundplaneDriver...\n");
	for(int i=0; i<kSoundplaneANumEndpoints; ++i)
	{
		mNextBusFrameNumber[i] = 0;
	}
	
	for(int i=0; i < kSoundplaneNumCarriers; ++i)
	{
		mCurrentCarriers[i] = kDefaultCarriers[i];
	}
	
	// create device grab thread
	mGrabThread = std::thread(&MacSoundplaneDriver::grabThread, this);
	mGrabThread.detach();  // REVIEW: mGrabThread is leaked
	
	// create isochronous read and process thread
	mProcessThread = std::thread(&MacSoundplaneDriver::processThread, this);
	
	// set thread to real time priority
	setThreadPriority(mProcessThread.native_handle(), 96, true);
}

MacSoundplaneDriver::~MacSoundplaneDriver()
{
	printf("deleting SoundplaneDriver...\n");
	
	kern_return_t	kr;
	
	{		
		std::lock_guard<std::mutex> lock(mDeviceStateMutex);
		setDeviceState(kDeviceClosing);
		mTerminating = true;
		
		// wait for any process() calls in progress to finish
		usleep(100*1000);
		
		// stop any device added / removed notifications and iterator
		if(notifyPort)
		{
			IONotificationPortDestroy(notifyPort);
			notifyPort = 0;
		}
		if (matchedIter)
		{
			IOObjectRelease(matchedIter);
			matchedIter = 0;
		}
		if(notification)
		{
			kr = IOObjectRelease(notification);	
			notification = 0;
		}
		
		// wait for process thread to terminate
		//
		if (mProcessThread.joinable())
		{
			mProcessThread.join();
			printf("process thread terminated.\n");
		}
		
		destroyDevice();
		
		setDeviceState(kNoDevice);
	}
	
	printf("error count: %d\n", mErrorCount);
	printf("frames: %d\n", mFrameCounter);
	printf("no frames: %d\n", mNoFrameCounter);
	printf("gaps: %d\n", mGaps);
}

void MacSoundplaneDriver::close()
{
	std::lock_guard<std::mutex> lock(mDeviceStateMutex);
	setDeviceState(kDeviceClosing);
	mTerminating = true;
	
	// wait for any process() calls in progress to finish
	usleep(100*1000);
}

int MacSoundplaneDriver::createLowLatencyBuffers()
{
	IOReturn err;
	
	// create and setup transaction data structures for each buffer in each isoch endpoint
	for (int i = 0; i < kSoundplaneANumEndpoints; i++)
	{
		for (int j = 0; j < kNumIsochBuffers; j++)
		{
			K1IsocTransaction* t = getTransactionData(i, j);
			t->endpointNum = kSoundplaneAEndpointStartIdx + i;
			t->endpointIndex = i;
			t->bufIndex = j;
			int payloadSize = sizeof(SoundplaneADataPacket)*kIsochFramesPerTransaction;
			
			// create buffer for the payload (our sensor data) itself
			err = (*intf)->LowLatencyCreateBuffer(intf, (void **)&t->payloads, payloadSize, kUSBLowLatencyReadBuffer);
			if (kIOReturnSuccess != err)
			{
				fprintf(stderr, "createLowLatencyBuffers: could not create payload buffer #%d (%08x)\n", j, err);
				return false;
			}
			bzero(t->payloads, payloadSize);
			
			// create buffer for the frame transaction data
			int isocFrameSize = sizeof(IOUSBLowLatencyIsocFrame)*kIsochFramesPerTransaction;
			err = (*intf)->LowLatencyCreateBuffer(intf, (void **)&t->isocFrames, isocFrameSize, kUSBLowLatencyFrameListBuffer);
			if (kIOReturnSuccess != err)
			{
				fprintf(stderr, "createLowLatencyBuffers: could not create frame list buffer #%d (%08x)\n", j, err);
				return false;
			}
			bzero(t->isocFrames, isocFrameSize);
			t->parent = this;
		}
	}
	return true;
}

int MacSoundplaneDriver::destroyLowLatencyBuffers()
{
	IOReturn err;
	
	for (int i = 0; i < kSoundplaneANumEndpoints; i++)
	{
		for (int j = 0; j < kNumIsochBuffers; j++)
		{
			K1IsocTransaction* t = getTransactionData(i, j);
			if (t->payloads)
			{
				err = (*intf)->LowLatencyDestroyBuffer(intf, t->payloads);
				if (kIOReturnSuccess != err)
				{
					fprintf(stderr, "destroyLowLatencyBuffers: could not destroy payload buffer #%d (%08x)\n", j, err);
					return false;
				}
				t->payloads = NULL;
			}
			if (t->isocFrames)
			{
				err = (*intf)->LowLatencyDestroyBuffer(intf, t->isocFrames);
				if (kIOReturnSuccess != err)
				{
					fprintf(stderr, "destroyLowLatencyBuffers: could not destroy frame list buffer #%d (%08x)\n", j, err);
					return false;
				}
				t->isocFrames = NULL;
			}
		}
	}
	
	return true;
}

/*
const K1IsocTransaction* MacSoundplaneDriver::getTransactionData(int endpoint, int buf) const
{ 
	return mTransactionData + kNumIsochBuffers*endpoint + buf; 
}
*/

uint32_t MacSoundplaneDriver::getTransactionDataChecksum() // TODO const
{
	uint16_t checkSum = 0;
	for (int i = 0; i < kSoundplaneANumEndpoints; i++)
	{
		for (int j = 0; j < kNumIsochBuffers; j++)
		{
			K1IsocTransaction* t = getTransactionData(i, j);
			t->endpointNum = kSoundplaneAEndpointStartIdx + i;
			t->endpointIndex = i;
			t->bufIndex = j;

			SoundplaneADataPacket* p = reinterpret_cast<SoundplaneADataPacket*>(t->payloads);
			
			for(int k=0; k < kIsochFramesPerTransaction; ++k)
			{
				uint16_t s = p[k].seqNum;
				checkSum += s;
			}
		}
	}
	return checkSum;
}

uint16_t MacSoundplaneDriver::getFirmwareVersion() const
{
	if(getDeviceState() < kDeviceConnected) return 0;
	uint16_t r = 0;
	IOReturn err;
	if (dev)
	{
		uint16_t version = 0;
		err = (*dev)->GetDeviceReleaseNumber(dev, &version);
		if (err == kIOReturnSuccess)
		{
			r = version;
		}
	}
	return r;
}

std::string MacSoundplaneDriver::getSerialNumberString() const
{
	if (getDeviceState() < kDeviceConnected) return 0;
	char buffer[64];
	uint8_t idx;
	int r = 0;
	IOReturn err;
	if (dev)
	{
		err = (*dev)->USBGetSerialNumberStringIndex(dev, &idx);
		if (err == kIOReturnSuccess)
		{
			r = getStringDescriptor(dev, idx, buffer, sizeof(buffer), 0);
		}
		
		// extract returned string from wchars.
		for(int i=0; i < r - 2; ++i)
		{
			buffer[i] = buffer[i*2 + 2];
		}
		
		return std::string(buffer, r / 2 - 1);
	}
	return "";
}

// -------------------------------------------------------------------------------
#pragma mark carriers

const unsigned char *MacSoundplaneDriver::getCarriers() const {
	return mCurrentCarriers;
}

void MacSoundplaneDriver::setCarriers(const Carriers& cData)
{
	if (!dev) return;
	if (getDeviceState() < kDeviceConnected) return;
	IOUSBDevRequest request;
	std::copy(cData.begin(), cData.end(), mCurrentCarriers);
	
	// wait for data to settle after setting carriers
	// TODO understand this startup behavior better
	mStartupCtr = 0;
		
	request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
	request.bRequest = kRequestCarriers;
	request.wValue = 0;
	request.wIndex = kRequestCarriersIndex;
	request.wLength = 32;
	request.pData = mCurrentCarriers;
	(*dev)->DeviceRequest(dev, &request);
}

void MacSoundplaneDriver::enableCarriers(unsigned long mask)
{
	if (!dev) return;
	IOUSBDevRequest	request;
	
	mStartupCtr = 0;
	
	request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
	request.bRequest = kRequestMask;
	request.wValue = mask >> 16;
	request.wIndex = mask;
	request.wLength = 0;
	request.pData = NULL;
	
	(*dev)->DeviceRequest(dev, &request);
}


// --------------------------------------------------------------------------------
#pragma mark K1IsocTransaction

uint16_t MacSoundplaneDriver::K1IsocTransaction::getTransactionSequenceNumber(int f)
{
	if (!payloads) return 0;
	SoundplaneADataPacket* p = (SoundplaneADataPacket*)payloads;
	return p[f].seqNum;
}

IOReturn MacSoundplaneDriver::K1IsocTransaction::getTransactionStatus(int n)
{
	if (!isocFrames) return 0;
	IOUSBLowLatencyIsocFrame* frames = (IOUSBLowLatencyIsocFrame*)isocFrames;
	return frames[n].frStatus;
}

void MacSoundplaneDriver::K1IsocTransaction::setSequenceNumber(int f, uint16_t s)
{
	SoundplaneADataPacket* p = (SoundplaneADataPacket*)payloads;
	p[f].seqNum = s;
}


/*
 
 notes on isoch scheduling:
 
 if you take a look at AppleUSBOHCI.cpp, you'll find that kIOUSBNotSent2Err
 corresponds to OHCI status 15. You can look that up in the OHCI spec. A not
 sent error occurs because your request is never put onto the bus. This
 usually happens if the request is for a frame number in the past, or too far
 in the future. I think that all your requests are for a time in the past, so
 they never get put onto the bus by the controller.
 
 We add about 50 to 100 frames to the value returned by GetBusFrameNumber, so
 the first read happens about 100ms in the future. There is a considerable
 and unpredictable latency between sending an isoch read request to the USB
 controller and that request appearing on the bus. Subsequent reads have
 their frame start time incremented by a fixed amount, governed by the size
 of the reads. You should queue up several reads at once - don't wait until
 your first read completes before you calculate the parameters for the second
 read - when the first read completes, you should be calculating the
 parameters for perhaps the eighth read.
 
 The first few reads you requested may come back empty, because their frame
 number has already passed. This is normal.
 
 */

void prepareForRequest(IOUSBLowLatencyIsocFrame& frame)
{
	frame.frStatus = 0;
	frame.frReqCount = sizeof(SoundplaneADataPacket);
	frame.frActCount = 0;
	frame.frTimeStamp.hi = 0;
	frame.frTimeStamp.lo = 0;
}


// --------------------------------------------------------------------------------
#pragma mark isochronous data

// Schedule an isochronous transfer. LowLatencyReadIsochPipeAsync() is used for all transfers
// to get the lowest possible latency. In order to complete, the transfer should be scheduled for a
// time in the future, but as soon as possible for low latency.
//
// The OS X low latency async code requires all transfer data to be in special blocks created by
// LowLatencyCreateBuffer() to manage communication with the kernel.
//
IOReturn MacSoundplaneDriver::scheduleIsoch(K1IsocTransaction *t)
{
	if ((!dev) || (!intf)) return kIOReturnNoDevice;
	
	IOReturn err = kIOReturnNoDevice;
	
	if(mDeviceState != kNoDevice) 
	{
		assert(t);
		
		t->parent = this;
		t->busFrameNumber = mNextBusFrameNumber[t->endpointIndex];
		
		for (int k = 0; k < kIsochFramesPerTransaction; k++)
		{
			prepareForRequest(t->isocFrames[k]);
			t->setSequenceNumber(k, 0);
		}
		
		size_t payloadSize = sizeof(SoundplaneADataPacket) * kIsochFramesPerTransaction;
		bzero(t->payloads, payloadSize);
		
#ifdef SHOW_BUS_FRAME_NUMBER
		fprintf(stderr, "read(%d, %p, %llu, %p, %p) [%d]\n", t->endpointNum, t->payloads, t->busFrameNumber, t->isocFrames, t, mTransactionsInFlight);
#endif
		
		err = (*intf)->LowLatencyReadIsochPipeAsync(intf, t->endpointNum, t->payloads,
													t->busFrameNumber, kIsochFramesPerTransaction, kSoundplaneAUpdateFrequency, t->isocFrames, isochComplete, t);
		
		mNextBusFrameNumber[t->endpointIndex] += kIsochFramesPerTransaction;
		
		mTransactionsInFlight++;
	}
	return err;
}

// isochComplete() is the callback executed whenever an isochronous transfer completes.
// Since this is called at main interrupt time, it must return as quickly as possible.
// It is only responsible for scheduling the next transfer into the next transaction buffer.
//
void MacSoundplaneDriver::isochComplete(void *refCon, IOReturn result, void *arg0)
{
	IOReturn err;
	
	K1IsocTransaction *t = (K1IsocTransaction *)refCon;
	MacSoundplaneDriver *k1 = t->parent;
	k1->mTransactionsInFlight--;
	
	switch (result)
	{			
		default:
			k1->mErrorCount++;				
			// when mystery error 0xe000400e is returned, one or more packets have
			// been lost from the given endpoint, after which normal
			// operation seems possible, therefore do fall through.
			// TODO interpolate any missing data.
			
		case kIOReturnUnderrun:
			// This error is often received when all payloads are present
			// and frActCounts are as expected. So fall through to success.
			
		case kIOReturnSuccess:
			// if not shutting down, schedule another transaction and set the device
			// state to isoch sync if needed.
			{
				int state = k1->getDeviceState();				
				if((state == kDeviceConnected) || (state == kDeviceHasIsochSync))
				{
					int nextBuf = (t->bufIndex + kIsochBuffersInFlight) & kIsochBuffersMask;
					K1IsocTransaction *pNextTransactionBuffer = k1->getTransactionData(t->endpointIndex, nextBuf);
					err = k1->scheduleIsoch(pNextTransactionBuffer);
					if(state != kDeviceHasIsochSync)
					{
						k1->setDeviceState(kDeviceHasIsochSync);
					}
				}
			}
			break;
			
		case kIOReturnIsoTooOld:
			break;
			
		case kIOReturnAborted: 
			// returned when Soundplane is unplugged
			break;				
	}
}

void MacSoundplaneDriver::resetIsochTransactions()
{
	IOReturn err = setBusFrameNumber();
	assert(!err);
	
	mTransactionsInFlight = 0;

	// for each endpoint, schedule first transaction and
	// a few buffers into the future
	for (int j = 0; j < kIsochBuffersInFlight; j++)
	{
		for (int i = 0; i < kSoundplaneANumEndpoints; i++)
		{
			err = scheduleIsoch(getTransactionData(i, j));
			if (kIOReturnSuccess != err)
			{
				show_io_err("scheduleIsoch", err);
				return;
			}
		}
	}
}


// --------------------------------------------------------------------------------
#pragma mark transfer utilities

// add a positive or negative offset to the current (buffer, frame) position.
//
void MacSoundplaneDriver::addOffset(int& buffer, int& frame, int offset)
{
	// add offset to (buffer, frame) position
	if(!offset) return;
	int totalFrames = buffer*kIsochFramesPerTransaction + frame + offset;
	int remainder = totalFrames % kIsochFramesPerTransaction;
	if (remainder < 0)
	{
		remainder += kIsochFramesPerTransaction;
		totalFrames -= kIsochFramesPerTransaction;
	}
	frame = remainder;
	buffer = (totalFrames / kIsochFramesPerTransaction) % kNumIsochBuffers;
	if (buffer < 0)
	{
		buffer += kNumIsochBuffers;
	}
}

uint16_t MacSoundplaneDriver::getTransferBytesReceived(int endpoint, int buffer, int frame, int offset)
{
	if(getDeviceState() < kDeviceConnected) return 0;
	uint16_t b = 0;
	addOffset(buffer, frame, offset);
	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	
	if (t)
	{
		IOUSBLowLatencyIsocFrame* pf = &t->isocFrames[frame];
		b = pf->frActCount;
	}
	return b;
}

AbsoluteTime MacSoundplaneDriver::getTransferTimeStamp(int endpoint, int buffer, int frame, int offset)
{
	if(getDeviceState() < kDeviceConnected) return AbsoluteTime();
	AbsoluteTime b;
	addOffset(buffer, frame, offset);
	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	
	if (t)
	{
		IOUSBLowLatencyIsocFrame* pf = &t->isocFrames[frame];
		b = pf->frTimeStamp;
	}
	return b;
}

IOReturn MacSoundplaneDriver::getTransferStatus(int endpoint, int buffer, int frame, int offset)
{
	if(getDeviceState() < kDeviceConnected) return kIOReturnNoDevice;
	IOReturn b = kIOReturnSuccess;
	addOffset(buffer, frame, offset);
	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	
	if (t)
	{
		IOUSBLowLatencyIsocFrame* pf = &t->isocFrames[frame];
		b = pf->frStatus;
	}
	return b;
}

uint16_t MacSoundplaneDriver::getSequenceNumber(int endpoint, int buffer, int frame, int offset)
{
	if(getDeviceState() < kDeviceConnected) return 0;
	uint16_t s = 0;
	addOffset(buffer, frame, offset);
	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	
	if (t && t->payloads)
	{
		SoundplaneADataPacket* p = (SoundplaneADataPacket*)t->payloads;
		s = p[frame].seqNum;
	}
	return s;
}

unsigned char* MacSoundplaneDriver::getPayloadPtr(int endpoint, int buffer, int frame, int offset)
{
	if(getDeviceState() < kDeviceConnected) return 0;
	unsigned char* p = 0;
	addOffset(buffer, frame, offset);
	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	if (t && t->payloads)
	{
		p = t->payloads + frame*sizeof(SoundplaneADataPacket);
	}
	return p;
}

unsigned char MacSoundplaneDriver::getPayloadLastByte(int endpoint, int buffer, int frame)
{
	if(getDeviceState() < kDeviceConnected) return 0;
	unsigned char* p = 0;
	unsigned char r = 0;

	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	if (t && t->payloads)
	{
		p = t->payloads + frame*sizeof(SoundplaneADataPacket);
		r = p[kSoundplaneAPackedDataSize - 1];
	}
	return r;
}


// --------------------------------------------------------------------------------
#pragma mark device utilities

namespace {
	
	IOReturn ConfigureDevice(IOUSBDeviceInterface187 **dev)
	{
		uint8_t							numConf;
		IOReturn						err;
		IOUSBConfigurationDescriptorPtr	confDesc;
		
		err = (*dev)->GetNumberOfConfigurations(dev, &numConf);
		if (err || !numConf)
			return err;
#ifdef VERBOSE
		printf("%d configuration(s)\n", numConf);
#endif
		
		// get the configuration descriptor for index 0
		err = (*dev)->GetConfigurationDescriptorPtr(dev, 0, &confDesc);
		if (err)
		{
			show_io_err("unable to get config descriptor for index 0", err);
			return err;
		}
		
		/*
		 REVIEW: Important: Because a composite class device is configured
		 by the AppleUSBComposite driver, setting the configuration again
		 from your application will result in the destruction of the IOUSBInterface
		 nub objects and the creation of new ones. In general, the only reason
		 to set the configuration of a composite class device that? matched by the
		 AppleUSBComposite driver is to choose a configuration other than the first one.
		 NOTE: This seems to be necessary
		 */
		
		err = (*dev)->SetConfiguration(dev, confDesc->bConfigurationValue);
		if (err)
		{
			show_io_err("unable to set configuration to index 0", err);
			return err;
		}
#ifdef VERBOSE
		printf("%d interface(s)\n", confDesc->bNumInterfaces);
#endif
		return kIOReturnSuccess;
	}
	
	IOReturn SelectIsochronousInterface(IOUSBInterfaceInterface192 **intf, int n)
	{
		IOReturn	err;
		
		// GetInterfaceNumber, GetAlternateSetting
		err = (*intf)->SetAlternateInterface(intf, n);
		if (err)
		{
			show_io_err("unable to set alternate interface", err);
			return err;
		}
		
		if (!n)
		{
			err = (*intf)->GetPipeStatus(intf, 0);
			if (err)
			{
				show_io_err("pipe #0 status failed", err);
				return err;
			}
		}
		else
		{
			err = (*intf)->GetPipeStatus(intf, 1);
			if (err)
			{
				show_io_err("pipe #1 status failed", err);
				return err;
			}
			err = (*intf)->GetPipeStatus(intf, 2);
			if (err)
			{
				show_io_err("pipe #2 status failed", err);
				return err;
			}
		}
		return kIOReturnSuccess;
	}
	
}

IOReturn MacSoundplaneDriver::setBusFrameNumber()
{
	IOReturn err;
	AbsoluteTime atTime;
	
	err = (*dev)->GetBusFrameNumber(dev, &mNextBusFrameNumber[0], &atTime);
	if (kIOReturnSuccess != err)
		return err;
#ifdef VERBOSE
	printf("Bus Frame Number: %llu @ %X%08X\n", mNextBusFrameNumber[0], (int)atTime.hi, (int)atTime.lo);
#endif
	mNextBusFrameNumber[0] += 50;	// schedule 50 ms into the future
	mNextBusFrameNumber[1] = mNextBusFrameNumber[0];
	return kIOReturnSuccess;
}

// deviceAdded() is called by the callback set up in the grab thread when a new Soundplane device is found.
//
void MacSoundplaneDriver::deviceAdded(void *refCon, io_iterator_t iterator)
{
	MacSoundplaneDriver			*k1 = static_cast<MacSoundplaneDriver *>(refCon);
	kern_return_t				kr;
	IOReturn					err;
	io_service_t				usbDeviceRef;
	io_iterator_t				interfaceIterator;
	io_service_t				usbInterfaceRef;
	IOCFPlugInInterface			**plugInInterface = NULL;
	IOUSBDeviceInterface187		**dev = NULL;	// 182, 187, 197
	
	IOUSBInterfaceInterface192	**intf; // ?! 
	
	IOUSBFindInterfaceRequest	req;
	ULONG						res;
	SInt32						score;
	UInt32						powerAvailable;
#ifdef VERBOSE
	uint16_t					vendor;
	uint16_t					product;
	uint16_t					release;
#endif
	int							i;
	uint8_t						n;
	
	bool addedDevice = false;
	
	while ((usbDeviceRef = IOIteratorNext(iterator)))
	{
		kr = IOCreatePlugInInterfaceForService(usbDeviceRef, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugInInterface, &score);
		if ((kIOReturnSuccess != kr) || !plugInInterface)
		{
			show_kern_err("unable to create a device plugin", kr);
			continue;
		}
		// have device plugin, need device interface
		err = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (void**)(LPVOID)&dev);
		IODestroyPlugInInterface(plugInInterface);
		plugInInterface = NULL;
		if (err || !dev)
		{
			show_io_err("could not create device interface", err);
			continue;
		}
		assert(!kr);
		err = (*dev)->GetDeviceBusPowerAvailable(dev, &powerAvailable);
		if (err)
		{
			show_io_err("could not get bus power available", err);
			res = (*dev)->Release(dev);
			if (kIOReturnSuccess != res)
				show_io_err("unable to release device", res);
			dev = NULL;
			continue;
		}
		printf("    Available Bus Power: %d mA\n", (int)(2*powerAvailable));
		// REVIEW: GetDeviceSpeed()
#ifdef VERBOSE
		
		// REVIEW: technically should check these err values
		err = (*dev)->GetDeviceVendor(dev, &vendor);
		err = (*dev)->GetDeviceProduct(dev, &product);
		err = (*dev)->GetDeviceReleaseNumber(dev, &release);
		printf("    Vendor:%04X Product:%04X Release Number:", vendor, product);
		// printf("%hhx.%hhx.%hhx\n", release >> 8, 0x0f & release >> 4, 0x0f & release)
		// NOTE: GetLocationID might be helpful
#endif
		
		// need to open the device in order to change its state
		do {
			err = (*dev)->USBDeviceOpenSeize(dev);
			if (kIOReturnExclusiveAccess == err)
			{
				printf("Exclusive access err, sleeping on it\n");
				sleep(10);
			}
		} while (kIOReturnExclusiveAccess == err);
		
		if (kIOReturnSuccess != err)
		{
			show_io_err("unable to open device:", err);
			goto release;
		}
		
		err = ConfigureDevice(dev);
		
		// get the list of interfaces for this device
		req.bInterfaceClass = kIOUSBFindInterfaceDontCare;
		req.bInterfaceSubClass = kIOUSBFindInterfaceDontCare;
		req.bInterfaceProtocol = kIOUSBFindInterfaceDontCare;
		req.bAlternateSetting = kIOUSBFindInterfaceDontCare;
		err = (*dev)->CreateInterfaceIterator(dev, &req, &interfaceIterator);
		if (kIOReturnSuccess != err)
		{
			show_io_err("could not create interface iterator", err);
			continue;
		}
		usbInterfaceRef = IOIteratorNext(interfaceIterator);
		if (!usbInterfaceRef)
		{
			fprintf(stderr, "unable to find an interface\n");
		}
		else while (usbInterfaceRef)
		{
			kr = IOCreatePlugInInterfaceForService(usbInterfaceRef, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugInInterface, &score);
			if ((kIOReturnSuccess != kr) || !plugInInterface)
			{
				show_kern_err("unable to create plugin interface for USB interface", kr);
				goto close;
			}
			kr = IOObjectRelease(usbInterfaceRef);
			usbInterfaceRef = 0;
			
			// have interface plugin, need interface interface
			err = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID192), (void **)(LPVOID)&intf);
			kr = IODestroyPlugInInterface(plugInInterface);
			assert(!kr);
			plugInInterface = NULL;
			if (err || !intf)
				show_io_err("could not create interface interface", err);
			else if (intf)
			{
				// Don't release the interface here. That's one too many releases and causes set alt interface to fail
				err = (*intf)->USBInterfaceOpenSeize(intf);
				if (kIOReturnSuccess != err)
				{
					show_io_err("unable to seize interface for exclusive access", err);
					goto close;
				}
				
				err = SelectIsochronousInterface(intf, kSoundplaneAlternateSetting);
				
				// add notification for device removal and other info
				if (kIOReturnSuccess == err)
				{
					mach_port_t asyncPort;
					CFRunLoopSourceRef source;
					
					k1->dev = dev;
					k1->intf = intf;
					k1->payloadIndex[0] = 0;
					k1->payloadIndex[1] = 0;
					kr = IOServiceAddInterestNotification(k1->notifyPort,		// notifyPort
														  usbDeviceRef,			// service
														  kIOGeneralInterest,	// interestType
														  deviceNotifyGeneral,	// callback
														  k1,					// refCon
														  &(k1->notification)	// notification
														  );
					if (kIOReturnSuccess != kr)
					{
						show_kern_err("could not add interest notification", kr);
						goto close;
					}
					
					
					err = (*intf)->CreateInterfaceAsyncPort(intf, &asyncPort);
					if (kIOReturnSuccess != err)
					{
						show_io_err("could not create asynchronous port", err);
						goto close;
					}
					
					// from SampleUSBMIDIDriver: confirm MIDIGetDriverIORunLoop() contains InterfaceAsyncEventSource
					source = (*intf)->GetInterfaceAsyncEventSource(intf);
					if (!source)
					{
#ifdef VERBOSE
						fprintf(stderr, "created missing async event source\n");
#endif
						err = (*intf)->CreateInterfaceAsyncEventSource(intf, &source);
						if (kIOReturnSuccess != err)
						{
							show_io_err("failure to create async event source", err);
							goto close;
						}
					}
					if (!CFRunLoopContainsSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode))
						CFRunLoopAddSource(CFRunLoopGetCurrent(), source, kCFRunLoopDefaultMode);
					
					err = (*intf)->GetNumEndpoints(intf, &n);
					if (kIOReturnSuccess != err)
					{
						show_io_err("could not get number of endpoints in interface", err);
						goto close;
					}
					else
					{
						printf("isochronous interface opened, %d endpoints\n", n);
					}
					
					// for each endpoint of the isochronous interface, get pipe properties
					for (i = 1; i <= n; i++)
					{
						uint8_t		direction;
						uint8_t		number;
						uint8_t		transferType;
						uint16_t	maxPacketSize;
						uint8_t		interval;
						
						err = (*intf)->GetPipeProperties(intf, i, &direction, &number, &transferType, &maxPacketSize, &interval);
						if (kIOReturnSuccess != err)
						{
							fprintf(stderr, "endpoint %d - could not get endpoint properties (%08x)\n", i, err);
							goto close;
						}
					}
					
					if(!k1->createLowLatencyBuffers()) goto close;
					k1->setDeviceState(kDeviceConnected);
					k1->resetIsochTransactions();
					addedDevice = true;				
				}
				else
				{
					res = (*intf)->Release(intf);
					intf = NULL;
				}
				
			}
			usbInterfaceRef = IOIteratorNext(interfaceIterator);
		}
		kr = IOObjectRelease(interfaceIterator);
		assert(!kr);
		interfaceIterator = 0;
		continue;
		
	close:
		k1->dev = NULL;
		err = (*dev)->USBDeviceClose(dev);
		if (kIOReturnSuccess != err)
			show_io_err("unable to close device", err);
		else
			printf("closed dev:%p\n", dev);
	release:
		k1->dev = NULL;
		res = (*dev)->Release(dev);
		if (kIOReturnSuccess != res)
			show_io_err("unable to release device", err);
		else
			printf("released dev:%p\n", dev);
		dev = NULL;
		kr = IOObjectRelease(usbDeviceRef);
		assert(!kr);
	}
	
	if(addedDevice)
	{
		k1->mListener.onStartup();
	}
}

// if device is unplugged, remove device and go back to waiting.
//
void MacSoundplaneDriver::deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
	MacSoundplaneDriver *k1 = static_cast<MacSoundplaneDriver *>(refCon);
	
	if (kIOMessageServiceIsTerminated == messageType)
	{
		std::cout << "deviceNotifyGeneral, state " << k1->getDeviceState() << " \n";
		
		std::lock_guard<std::mutex> lock(k1->getDeviceStateMutex());
		k1->setDeviceState(kDeviceClosing);
		k1->destroyDevice();
		k1->setDeviceState(kNoDevice);
	}
}

// This thread is responsible for finding and adding USB devices matching the Soundplane.
// Execution is controlled by a Core Foundation (Cocoa) run loop.
//
void MacSoundplaneDriver::grabThread()
{
	bool OK = true;
	kern_return_t				kr;
	CFMutableDictionaryRef		matchingDict;
	CFRunLoopSourceRef			runLoopSource;
	CFNumberRef					numberRef;
	SInt32						usbVendor = kSoundplaneUSBVendor;
	SInt32						usbProduct = kSoundplaneUSBProduct;
	
	matchingDict = IOServiceMatching(kIOUSBDeviceClassName);
	if (!matchingDict)
	{
		fprintf(stderr, "Cannot create USB matching dictionary\n");
		OK = false;
	}
	if (OK)
	{
		numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usbVendor);
		assert(numberRef);
		CFDictionarySetValue(matchingDict, CFSTR(kUSBVendorID), numberRef);
		CFRelease(numberRef);
		numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usbProduct);
		assert(numberRef);
		CFDictionarySetValue(matchingDict, CFSTR(kUSBProductID), numberRef);
		CFRelease(numberRef);
		numberRef = NULL;
		
		notifyPort = IONotificationPortCreate(kIOMasterPortDefault);
		runLoopSource = IONotificationPortGetRunLoopSource(notifyPort);
		CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopDefaultMode);
		
		// Set up asynchronous callback to call deviceAdded() when a Soundplane is found.
		// TODO REVIEW: Check out IOServiceMatching() and IOServiceNameMatching()
		kr = IOServiceAddMatchingNotification(  notifyPort,
											  kIOFirstMatchNotification,
											  matchingDict,
											  deviceAdded,
											  this, // refCon
											  &(matchedIter) );
		
		// Iterate once to get already-present devices and arm the notification
		deviceAdded(this, matchedIter);
		
		// Start the run loop. Now we'll receive notifications and remain looping here until the
		// run loop is stopped with CFRunLoopStop or all the sources and timers are removed from
		// the default run loop mode.
		// For more information about how Core Foundation run loops behave, see Run Loops in
		// Apple's Threading Programming Guide.
		CFRunLoopRun();
		
		// clean up
		CFRunLoopRemoveSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopDefaultMode);
	}
}

void MacSoundplaneDriver::destroyDevice()
{
	//IOReturn err;
	kern_return_t	kr;
	
    mListener.onClose();
    
	// wait for any pending transactions to finish
	//
	int totalWaitTime = 0;
	int waitTimeMicrosecs = 100*1000;
	while (mTransactionsInFlight > 0)
	{
		printf("%d pending transactions, waiting...\n", mTransactionsInFlight);
		usleep(waitTimeMicrosecs);
		totalWaitTime += waitTimeMicrosecs;
		
		// waiting too long-- bail without cleaning up buffers
		if (totalWaitTime > 1000*1000)
		{
			printf("WARNING: Soundplane driver could not finish pending transactions: %d remaining\n", mTransactionsInFlight);
			intf = NULL;
			break;
		}
	}
	
	// wait some more
	//
	usleep(100*1000);
    
	// clean up transaction data and release interface.
	//
	// Doing this with any transactions pending WILL cause a kernel panic!
	//
	if (intf)
	{
		destroyLowLatencyBuffers();
		kr = (*intf)->Release(intf);
		intf = NULL;
	}
	
	if (dev)
	{
		printf("closing device.\n");
		kr = (*dev)->USBDeviceClose(dev);
		kr = (*dev)->Release(dev);
		dev = NULL;
	}
}

// watch the buffers being filled by the isochronous layer. If there is no new data for a while,
// reset the isoch layer. 
//
void MacSoundplaneDriver::resetIsochIfStalled()
{
	// timing that will result from these options depends on lots of things, but since timing
	// of stall recovery is not critical, we can just use these counters instead of something 
	// more precise
	const int checksumInterval = 100; // checksum every n calls of this method
	const int maxStallsBeforeReset = 8; 

	static int checksumCtr = 0;
	static uint16_t prevTransactionChecksum = 0;
	static int stalled = 0;
	
	if(mDeviceState == kDeviceHasIsochSync)
	{
		// don't checksum every frame
		checksumCtr++;
		if(checksumCtr > checksumInterval)
		{
			checksumCtr = 0;
            
            // TDOO why a checksum? we can just bail if equality check fails, which is usual
			uint16_t transactionChecksum = getTransactionDataChecksum();
			if(transactionChecksum != prevTransactionChecksum)
			{
				stalled = 0;
			}
			else
			{
				stalled++;
			}
			prevTransactionChecksum = transactionChecksum;
			
			if(stalled > maxStallsBeforeReset)
			{
				stalled = 0;
				resetIsochTransactions();
			}
		}
	}
}

// search for the most recent sequence number in all the buffers in the queue.
// point the readers at it. 
//
void MacSoundplaneDriver::resetEndpointReaders()
{
	int maxSequenceNum = -1;
	for(int readerIdx = 0; readerIdx < kSoundplaneANumEndpoints; ++readerIdx)
	{
		MacSoundplaneDriver::EndpointReader& reader = mEndpointReaders[readerIdx];
		int maxEndpointSequenceNum = -1;
		
		// find most recent sequence #
		FramePosition pos, maxPos;
		bool foundData = false;
		const int totalFrames = kIsochFramesPerTransaction*kNumIsochBuffers;
		for(int c = 0; c < totalFrames; ++c)
		{
			pos = advance(pos, 1);
			
			// look at the last byte of the payload to determine whether there is new data
			int lastByte = getPayloadLastByte(readerIdx, pos.buffer, pos.frame);			
			if(lastByte > 0)
			{
				int frameSeqNum = getSequenceNumber(readerIdx, pos.buffer, pos.frame);
				if(frameSeqNum > maxEndpointSequenceNum)
				{
					maxEndpointSequenceNum = frameSeqNum;
					maxPos = pos;
					foundData = true;
				}
			}
		}
		
		if(foundData)
		{
			if(maxEndpointSequenceNum > maxSequenceNum)
			{
				maxSequenceNum = maxEndpointSequenceNum;
			}
			
			reader.lost = false;
			reader.seqNum = maxEndpointSequenceNum;
			reader.position = maxPos;
		}
		else
		{
			reader.lost = true;
		}
	}
	
	// set max sequence number
	if(maxSequenceNum >= 0)
	{
		mSequenceNum = maxSequenceNum;
	}
}

// try to advance the reader to the given sequence number. return the sequence number the reader
// is at after the call. side effect: the reader's state changes, including its lost flag which we look at later.
// if already at the destination, the reader will not be moved.
//
int MacSoundplaneDriver::advanceEndpointReader(int readerIdx, uint16_t destSeqNum)
{
	MacSoundplaneDriver::EndpointReader& reader = mEndpointReaders[readerIdx];
	
	int frameSeqNum;
	int frameActualBytesRead;
	
	if(reader.seqNum != destSeqNum)
	{
		int advanced = 0;
		const int maxAdvance = 2;
		FramePosition nextPos = reader.position;
		while((reader.seqNum != destSeqNum)&&(advanced < maxAdvance))
		{		
			nextPos = advance(nextPos, 1);
			
			// each frame either gets 0 or a full packet
			frameActualBytesRead = getTransferBytesReceived(readerIdx, nextPos.buffer, nextPos.frame);

			unsigned char lastByte = getPayloadLastByte(readerIdx, nextPos.buffer, nextPos.frame);
			
			// if the last byte of the payload is nonzero, we know the whole payload is present. 
			// payload data is written before the bytes received, which lets us shave off a few ms of latency.
			// TODO CONFIRM: All possible soundplane A payloads have a nonzero last byte.
			if(lastByte > 0)
			{
				reader.position = nextPos;
				uint16_t expectedSeqNum = reader.seqNum + 1; // note: wraps
				frameSeqNum = getSequenceNumber(readerIdx, nextPos.buffer, nextPos.frame);
				
				if(frameSeqNum == expectedSeqNum)
				{
					// got next expected sequence but not the destination, keep going
					reader.seqNum = frameSeqNum;
					advanced++;
				}
				else
				{
					// if the next sequence number was not the one expected, we are lost.
					reader.lost = true;
					break;
				}
			}
			else
			{
				advanced++;
			}
		}
	}
	
	return reader.seqNum;
}

void MacSoundplaneDriver::process(SensorFrame* pOut)
{	
	uint16_t nextSequenceNum = mSequenceNum + 1; // wraps

    std::lock_guard<std::mutex> lock(mDeviceStateMutex);
	if(mDeviceState == kDeviceHasIsochSync) 
	{
		bool gotFrame = false;
		
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
					unsigned char* pPayload = getPayloadPtr(endpointIdx, bufIdx, frameIdx);					
					unsigned char b = getPayloadLastByte(endpointIdx, bufIdx, frameIdx);
					printf("%05d[%03d][%02x] ", frameSeqNum, actCount, b);
				}
				std::cout << "\n";
			}
		}
#endif
		
		int numReadersAtNext = 0;
		int numReadersLost = 0;
		
		for(int i = 0; i < kSoundplaneANumEndpoints; ++i)
		{
			EndpointReader& r = mEndpointReaders[i];
			if (advanceEndpointReader(i, nextSequenceNum) == nextSequenceNum)
			{
				numReadersAtNext++;	
			}
			
			if(r.lost)
				numReadersLost++;
		}
		
//		std::cout << " at next: " << numReadersAtNext << ", lost: " << numReadersLost << "\n";
		
		// if all readers are at next sequence, generate frame
		if(numReadersAtNext == kSoundplaneANumEndpoints)
		{			
			mSequenceNum = nextSequenceNum;
			
			// make frame
			std::array<unsigned char *, kSoundplaneANumEndpoints> payloads;
			bool payloadsOK = true; 
			for(int i=0 ; i<kSoundplaneANumEndpoints; ++i)
			{
				EndpointReader& r = mEndpointReaders[i];
				payloads[i] = getPayloadPtr(i, r.position.buffer, r.position.frame);
				if(!payloads[i])
				{
					payloadsOK = false;
					break;
				}
			}
			
			if (payloadsOK)
			{
				if(mStartupCtr > kIsochStartupFrames)
				{
					// assemble endpoints into frame
					// for two endpoints only
					K1_unpack_float2(payloads[0], payloads[1], mWorkingFrame);
					K1_clear_edges(mWorkingFrame);
					
					// call client callback	
					mListener.onFrame(mWorkingFrame);
					gotFrame = true;
				}
				else
				{
					// wait for startup
					mStartupCtr++;
				}
			}
		}
		else if (numReadersLost > 0) 
		{
			resetEndpointReaders();
			mGaps++;
		}
				
		if(gotFrame)
		{
			mFrameCounter++;
		}
		else
		{
			mNoFrameCounter++;
		}
	}
}

void MacSoundplaneDriver::processThread()
{
	while(!mTerminating)
	{
		resetIsochIfStalled();
		process(&mWorkingFrame);
		usleep(500); // 0.5_ms
	}
}

// -------------------------------------------------------------------------------
#pragma mark transfer utilities

int MacSoundplaneDriver::getStringDescriptor(IOUSBDeviceInterface187 **dev, uint8_t descIndex, char *destBuf, uint16_t maxLen, UInt16 lang)
{
	IOUSBDevRequest req;
	uint8_t 		desc[256]; // Max possible descriptor length
	uint8_t 		desc2[256]; // Max possible descriptor length
	int stringLen;
	IOReturn err;
	if (lang == 0) // set default langID
		lang=0x0409;
	
	bzero(&req, sizeof(req));
	req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice);
	req.bRequest = kUSBRqGetDescriptor;
	req.wValue = (kUSBStringDesc << 8) | descIndex;
	req.wIndex = lang;	// English
	req.wLength = 2;
	req.pData = &desc;
	(err = (*dev)->DeviceRequest(dev, &req));
	if ( (err != kIOReturnSuccess) && (err != kIOReturnOverrun) )
		return -1;
	
	// If the string is 0 (it happens), then just return 0 as the length
	//
	stringLen = desc[0];
	if(stringLen == 0)
	{
		return 0;
	}
	
	// OK, now that we have the string length, make a request for the full length
	//
	bzero(&req, sizeof(req));
	req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice);
	req.bRequest = kUSBRqGetDescriptor;
	req.wValue = (kUSBStringDesc << 8) | descIndex;
	req.wIndex = lang;	// English
	req.wLength = stringLen;
	req.pData = desc2;
	
	(err = (*dev)->DeviceRequest(dev, &req));
	if ( err ) return -1;
	
	// copy to output buffer
	size_t destLen = std::min((int)req.wLenDone, (int)maxLen);
	std::copy((const char *)desc2, (const char *)desc2 + destLen, destBuf);
	
	return static_cast<int>(destLen);
}
