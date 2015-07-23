
// SoundplaneDriver.cpp
//
// Returns raw data frames from the Soundplane.  The frames are reclocked if needed (TODO)
// to reconstruct a steady sample rate. 
//
// Two threads are used to do this work.  A grab thread maintains a stream of 
// low-latency isochronous transfers. A process thread looks through the buffers 
// specified by these transfers every ms or so.  When new frames of data arrive,
// the process thread reclocks them and pushes them to a ring buffer where they
// can be read by clients.

#include "SoundplaneDriver.h"

#if DEBUG
	#define VERBOSE
#endif
//#define SHOW_BUS_FRAME_NUMBER
//#define SHOW_ALL_SEQUENCE_NUMBERS

const char* kSoundplaneAName = ("Soundplane Model A");

const int kSoundplaneAlternateSetting = 1;

#define printBCD(bcd) printf("%hhx.%hhx.%hhx\n", bcd >> 8, 0x0f & bcd >> 4, 0x0f & bcd)

void deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument);
void *soundplaneGrabThread(void *arg);
void *soundplaneProcessThread(void *arg);

// default carriers.  avoiding 32 (always bad)	
// in use these should be overridden by the selected carriers.
//
const unsigned char kDefaultCarriers[kSoundplaneSensorWidth] = 
{	
	0, 0, 4, 5, 6, 7, 8, 9, 10, 11, 
	12, 13, 14, 15, 16, 17, 18, 19, 
	20, 21, 22, 23, 24, 25, 26, 27, 
	28, 29, 30, 31, 33, 34
};

// -------------------------------------------------------------------------------
#pragma mark SoundplaneDriver

SoundplaneDriver::SoundplaneDriver() :
	clientRef(0),
	dev(0),
	intf(0), 
	mpTransactionData(0),
	mpOutputData(0),
	mTransactionsInFlight(0),
	mState(kNoDevice),
	startupCtr(0)
{
	clientRef = 0;
	
	for(int i=0; i<kSoundplaneANumEndpoints; ++i)
	{
		busFrameNumber[i] = 0;
	}
	
	size_t transactionsSize = kSoundplaneANumEndpoints * kSoundplaneABuffers * sizeof(K1IsocTransaction);
	mpTransactionData = (K1IsocTransaction*)malloc(transactionsSize); 
	if (mpTransactionData)
	{
		bzero(mpTransactionData, transactionsSize);
	}
	
	size_t outputFrameSize = kSoundplaneWidth * kSoundplaneHeight * sizeof(float);
	size_t outputBufSize = kSoundplaneOutputBufFrames * outputFrameSize;
	mpOutputData = (float *)malloc(outputBufSize); 
	if (mpOutputData)
	{
		PaUtil_InitializeRingBuffer(&mOutputBuf, outputFrameSize, kSoundplaneOutputBufFrames, mpOutputData);
	}
	else
	{
		fprintf(stderr, "Soundplane driver: couldn't create output buffer!\n");
	}
	
	for(int i=0; i < kSoundplaneSensorWidth; ++i)
	{
		mCurrentCarriers[i] = kDefaultCarriers[i];
	}	 
}

SoundplaneDriver::~SoundplaneDriver()
{
	printf("SoundplaneDriver shutting down...\n");	
	shutdown();	
	if (mpTransactionData) free(mpTransactionData);
	if (mpOutputData) free(mpOutputData);
}

void SoundplaneDriver::init()
{
	// create device grab thread
	OSErr err;
	pthread_attr_t attr;
	err = pthread_attr_init(&attr);
	assert(!err);
	err = pthread_create(&mGrabThread, &attr, soundplaneGrabThread, this);
	assert(!err);

	// create isochronous read and process thread
	err = pthread_attr_init(&attr);
	assert(!err);
	err = pthread_create(&mProcessThread, &attr, soundplaneProcessThread, this);
	assert(!err);
	
	// set thread to real time priority
	setThreadPriority(mProcessThread, 96, true);
}

void SoundplaneDriver::shutdown()
{
	kern_return_t	kr;

	setDeviceState(kDeviceIsTerminating);
	
	if(notifyPort)
	{
		IONotificationPortDestroy(notifyPort);
	}
	
	if (matchedIter) 
	{
        IOObjectRelease(matchedIter);
        matchedIter = 0;
	}
	
	// clear output
	PaUtil_FlushRingBuffer( &mOutputBuf );

	// wait for any pending transactions to finish
	//
	int totalWaitTime = 0;
	int waitTimeMicrosecs = 1000; 
	while (mTransactionsInFlight > 0)
	{
		usleep(waitTimeMicrosecs);		
		totalWaitTime += waitTimeMicrosecs;
		
		// waiting too long-- bail without cleaning up
		if (totalWaitTime > 100*1000) 
		{
			printf("WARNING: Soundplane driver could not finish pending transactions!\n");
			return;
		}
	}

	// wait for process thread to terminate
	//
	if (mProcessThread)
	{
		int exitResult = pthread_join(mProcessThread, NULL); // pthread_cancel()?

		printf("process thread terminated.  Returned %d \n", exitResult);
	
		mProcessThread = 0;
	}
	
	// wait some more
	//
	usleep(100*1000);		

	if (dev)
	{
		kr = (*dev)->USBDeviceClose(dev);
		kr = (*dev)->Release(dev);
		dev = NULL;
	}
	kr = IOObjectRelease(notification); 
	
	// clean up transaction data.  
	//
	// Doing this with any transactions pending WILL cause a kernel panic!	
	//	
	if (intf)
	{
		unsigned i, n;
		for (n = 0; n < kSoundplaneANumEndpoints; n++)
		{
			for (i = 0; i < kSoundplaneABuffers; i++)
			{
				K1IsocTransaction* t = getTransactionData(n, i);
				if (t->payloads)
				{
					(*intf)->LowLatencyDestroyBuffer(intf, t->payloads);
					t->payloads = NULL;
				}
				if (t->isocFrames)
				{
					(*intf)->LowLatencyDestroyBuffer(intf, t->isocFrames);
					t->isocFrames = NULL;
				}
			}
		}
		kr = (*intf)->Release(intf);
		intf = NULL;
	}	
}		


void SoundplaneDriver::removeDevice()
{
	IOReturn err;
	kern_return_t	kr;
	setDeviceState(kNoDevice);

	printf("Soundplane A removed.\n");
	
	if (intf)
	{
		unsigned i, n;

		for (n = 0; n < kSoundplaneANumEndpoints; n++)
		{
			for (i = 0; i < kSoundplaneABuffers; i++)
			{
				K1IsocTransaction* t = getTransactionData(n, i);
				if (t->payloads)
				{
					err = (*intf)->LowLatencyDestroyBuffer(intf, t->payloads);
					t->payloads = NULL;
				}
				if (t->isocFrames)
				{
					err = (*intf)->LowLatencyDestroyBuffer(intf, t->isocFrames);
					t->isocFrames = NULL;
				}
			}
		}
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
	kr = IOObjectRelease(notification);
}
			
// Called by client to put one frame of data from the ring buffer into memory at pDest. 
// This mechanism assumes we have only one consumer of data. 
//			
int SoundplaneDriver::readSurface(float* pDest)
{
	int result = 0;
	if(PaUtil_GetRingBufferReadAvailable(&mOutputBuf) >= 1)
	{
		result = PaUtil_ReadRingBuffer(&mOutputBuf, pDest, 1);
	}
	return result;
}

void SoundplaneDriver::flushOutputBuffer() 
{ 
	PaUtil_FlushRingBuffer(&mOutputBuf);
}	

// write frame to buffer, reconstructing a constant clock from the data.
// this may involve interpolating frames. 
//
void SoundplaneDriver::reclockFrameToBuffer(float* pFrame)
{
	// currently, clock is ignored and we simply ship out data as quickly as possible.
	// TODO timestamps that will allow reconstituting the data with lower jitter. 
	PaUtil_WriteRingBuffer(&mOutputBuf, pFrame, 1);	
}				

MLSoundplaneState SoundplaneDriver::getDeviceState()
{ 
	PaUtil_ReadMemoryBarrier(); 
	return mState; 
} 

void SoundplaneDriver::setDeviceState(MLSoundplaneState n)
{
	PaUtil_WriteMemoryBarrier(); 
	mState = n;
	for(std::list<SoundplaneDriverListener*>::iterator it = mListeners.begin(); it != mListeners.end(); ++it)
	{
		(*it)->deviceStateChanged(mState);
	}
}

void SoundplaneDriver::reportDeviceError(int errCode, int d1, int d2, float df1, float df2)
{
	PaUtil_WriteMemoryBarrier(); 
	for(std::list<SoundplaneDriverListener*>::iterator it = mListeners.begin(); it != mListeners.end(); ++it)
	{
		(*it)->handleDeviceError(errCode, d1, d2, df1, df2);
	}
}

void SoundplaneDriver::dumpDeviceData(float* pData, int size)
{
	PaUtil_WriteMemoryBarrier(); 
	for(std::list<SoundplaneDriverListener*>::iterator it = mListeners.begin(); it != mListeners.end(); ++it)
	{
		(*it)->handleDeviceDataDump(pData, size);
	}
}

// add a positive or negative offset to the current (buffer, frame) position.
//
void SoundplaneDriver::addOffset(int& buffer, int& frame, int offset) 
{ 
	// add offset to (buffer, frame) position
	if(!offset) return;
	int totalFrames = buffer*kSoundplaneANumIsochFrames + frame + offset;
	int remainder = totalFrames % kSoundplaneANumIsochFrames;
	if (remainder < 0)
	{
		remainder += kSoundplaneANumIsochFrames;
		totalFrames -= kSoundplaneANumIsochFrames;
	}
	frame = remainder;
	buffer = (totalFrames / kSoundplaneANumIsochFrames) % kSoundplaneABuffers;
	if (buffer < 0)
	{
		buffer += kSoundplaneABuffers;
	}
}

UInt16 SoundplaneDriver::getTransferBytesRequested(int endpoint, int buffer, int frame, int offset) 
{ 
	if(getDeviceState() < kDeviceConnected) return 0;
	UInt16 b = 0;
	addOffset(buffer, frame, offset);
	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	
	if (t)
	{
		IOUSBLowLatencyIsocFrame* pf = &t->isocFrames[frame];
		b = pf->frReqCount;
	}
	return b;
}

UInt16 SoundplaneDriver::getTransferBytesReceived(int endpoint, int buffer, int frame, int offset) 
{ 
	if(getDeviceState() < kDeviceConnected) return 0;
	UInt16 b = 0;
	addOffset(buffer, frame, offset);
	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	
	if (t)
	{
		IOUSBLowLatencyIsocFrame* pf = &t->isocFrames[frame];
		b = pf->frActCount;
	}
	return b;
}

AbsoluteTime SoundplaneDriver::getTransferTimeStamp(int endpoint, int buffer, int frame, int offset)
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

IOReturn SoundplaneDriver::getTransferStatus(int endpoint, int buffer, int frame, int offset)
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

UInt16 SoundplaneDriver::getSequenceNumber(int endpoint, int buffer, int frame, int offset) 
{ 
	if(getDeviceState() < kDeviceConnected) return 0;
	UInt16 s = 0;
	addOffset(buffer, frame, offset);
	K1IsocTransaction* t = getTransactionData(endpoint, buffer);
	
	if (t && t->payloads)
	{
		SoundplaneADataPacket* p = (SoundplaneADataPacket*)t->payloads;
		s = p[frame].seqNum;
	}
	return s;
}

unsigned char* SoundplaneDriver::getPayloadPtr(int endpoint, int buffer, int frame, int offset)
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

UInt16 SoundplaneDriver::getFirmwareVersion()
{
	if(getDeviceState() < kDeviceConnected) return 0;
	UInt16 r = 0;
	IOReturn err;
	if (dev)
	{
		UInt16 version = 0;
		err = (*dev)->GetDeviceReleaseNumber(dev, &version);
		if (err == kIOReturnSuccess)
		{
			r = version;
		}
	}	
	return r;
}

int SoundplaneDriver::getSerialNumberString(char* destStr, int maxLen)
{
	if(getDeviceState() < kDeviceConnected) return 0;
	UInt8 idx;
	int r = 0;
	IOReturn err;
	if (dev)
	{
		err = (*dev)->USBGetSerialNumberStringIndex(dev, &idx);
		if (err == kIOReturnSuccess)
		{
			r = GetStringDescriptor(dev, idx, destStr, maxLen, 0); 
		}
		
		// exctract returned string from wchars. 
		for(int i=0; i < r - 2; ++i)
		{
			destStr[i] = destStr[i*2 + 2];
		}
		
		return r;
	}	
	return 0;
}

int SoundplaneDriver::getSerialNumber()
{
	int ret = 0;
	int len;
	switch(getDeviceState())
	{
		case kDeviceConnected:
		case kDeviceHasIsochSync:
		
			len = getSerialNumberString(mSerialString, 64);
			if(len)
			{
				ret = atoi(mSerialString);
			}

		case kNoDevice:
		default:
			break;
	}
	return ret;
}

// -------------------------------------------------------------------------------
#pragma mark carriers

float SoundplaneDriver::carrierToFrequency(int carrier)
{
	int cIdx = carrier; // offset?
	float freq = kSoundplaneASampleRate/kSoundplaneAFFTSize*cIdx;
	return freq;
}

void SoundplaneDriver::dumpCarriers()
{
	printf( "-----------------\n");
	for(int i=0; i < kSoundplaneSensorWidth; ++i)
	{
		printf("carrier idx %d : %f Hz\n" ,i , carrierToFrequency(mCurrentCarriers[i]));
	}
}

int SoundplaneDriver::setCarriers(const unsigned char *cData)
{
	if (!dev) return 0;
	if (getDeviceState() < kDeviceConnected) return 0;
	IOUSBDevRequest request;
	for(int i=0; i < kSoundplaneSensorWidth; ++i)
	{
		mCurrentCarriers[i] = cData[i];
	}
	
	// wait for data to settle after setting carriers
	// TODO understand this startup behavior better
	startupCtr = 0;

	request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    request.bRequest = kRequestCarriers;
	request.wValue = 0;
	request.wIndex = kRequestCarriersIndex;
	request.wLength = 32;
	request.pData = mCurrentCarriers;
	return (*dev)->DeviceRequest(dev, &request);
}

int SoundplaneDriver::enableCarriers(unsigned long mask)
{
	if (!dev) return 0;
    IOUSBDevRequest	request;

	startupCtr = 0;

    request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    request.bRequest = 1;
	request.wValue = mask >> 16;
	request.wIndex = mask;
	request.wLength = 0;
	request.pData = NULL;

	return (*dev)->DeviceRequest(dev, &request);
}

void SoundplaneDriver::setDefaultCarriers()
{
	startupCtr = 0;
	for(int i=0; i < kSoundplaneSensorWidth; ++i)
	{
		mCurrentCarriers[i] = kDefaultCarriers[i];
	}	
	setCarriers(mCurrentCarriers);
}
		
// --------------------------------------------------------------------------------
#pragma mark unpacking data

// combine two surface payloads to a single buffer of floating point pressure values.
//
void K1_unpack_float2(unsigned char *pSrc0, unsigned char *pSrc1, float *pDest)
{
	unsigned short a, b;
	float *pDestRow0, *pDestRow1;
	
	// evey three bytes of payload provide 24 bits, 
	// which is two 12-bit magnitude values packed
	// ml Lh HM
	//
	int c = 0;
	for(int i=0; i<kSoundplaneAPickupsPerBoard; ++i)
	{
		pDestRow0 = pDest + kSoundplaneANumCarriers*2*i;
		pDestRow1 = pDestRow0 + kSoundplaneANumCarriers;
		for (int j = 0; j < kSoundplaneANumCarriers; j += 2)
		{
			a = pSrc0[c+1] & 0x0F;	// 000h
			a <<= 8;				// 0h00
			a |= pSrc0[c];			// 0hml
			pDestRow0[j] = a / 4096.f;	
			
			a = pSrc0[c+2];			// 00HM
			a <<= 4;				// 0HM0
			a |= ((pSrc0[c+1] & 0xF0) >> 4);	// 0HML
			pDestRow0[j + 1] = a / 4096.f;	
			
			// flip surface 2
			
			b = pSrc1[c+1] & 0x0F;	// 000h
			b <<= 8;				// 0h00
			b |= pSrc1[c];			// 0hml
			pDestRow1[kSoundplaneANumCarriers - 1 - j] = b / 4096.f;	
			
			b = pSrc1[c+2];			// 00HM
			b <<= 4;				// 0HM0
			b |= ((pSrc1[c+1] & 0xF0) >> 4);	// 0HML
			pDestRow1[kSoundplaneANumCarriers - 2 - j] = b / 4096.f;	
			
			c += 3;		
		}
	}
}

// set data from edge carriers, unused on Soundplane A, to duplicate 
// actual data nearby.
void K1_clear_edges(float *pDest)
{
	float *pDestRow;
	for(int i=0; i<kSoundplaneAPickupsPerBoard; ++i)
	{
		pDestRow = pDest + kSoundplaneANumCarriers*2*i;
		const float zl = pDestRow[2];
		pDestRow[1] = zl;
		pDestRow[0] = 0;
		const float zr = pDestRow[kSoundplaneANumCarriers*2 - 3];
		pDestRow[kSoundplaneANumCarriers*2 - 2] = zr;
		pDestRow[kSoundplaneANumCarriers*2 - 1] = 0;
	}
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


// --------------------------------------------------------------------------------
#pragma mark isochronous data

void isochComplete(void *refCon, IOReturn result, void *arg0);

// Schedule an isochronous transfer. LowLatencyReadIsochPipeAsync() is used for all transfers
// to get the lowest possible latency. In order to complete, the transfer should be scheduled for a 
// time in the future, but as soon as possible for low latency. 
//
// The OS X low latency async code requires all transfer data to be in special blocks created by 
// LowLatencyCreateBuffer() to manage communication with the kernel.
// These are made in deviceAdded() when a Soundplane is connected.
//
IOReturn scheduleIsoch(SoundplaneDriver *k1, K1IsocTransaction *t)
{	
	if (!k1->dev) return kIOReturnNoDevice;
	MLSoundplaneState state = k1->getDeviceState();
	if (state == kNoDevice) return kIOReturnNotReady;
	if (state == kDeviceIsTerminating) return kIOReturnNotReady;

	IOReturn err;
	assert(t);
	
	t->parent = k1;	
	t->busFrameNumber = k1->busFrameNumber[t->endpointIndex];

	for (int k = 0; k < kSoundplaneANumIsochFrames; k++)
	{
		t->isocFrames[k].frStatus = 0;
		t->isocFrames[k].frReqCount = sizeof(SoundplaneADataPacket);
		t->isocFrames[k].frActCount = 0;
		t->isocFrames[k].frTimeStamp.hi = 0;
		t->isocFrames[k].frTimeStamp.lo = 0;
		setSequenceNumber(t, k, 0);
	}
	
	size_t payloadSize = sizeof(SoundplaneADataPacket) * kSoundplaneANumIsochFrames;
	bzero(t->payloads, payloadSize);
	
#ifdef SHOW_BUS_FRAME_NUMBER
	fprintf(stderr, "read(%d, %p, %llu, %p, %p)\n", t->endpointNum, t->payloads, t->busFrameNumber, t->isocFrames, t);
#endif
	err = (*k1->intf)->LowLatencyReadIsochPipeAsync(k1->intf, t->endpointNum, t->payloads, 
		t->busFrameNumber, kSoundplaneANumIsochFrames, kSoundplaneAUpdateFrequency, t->isocFrames, isochComplete, t);
		
	k1->busFrameNumber[t->endpointIndex] += kSoundplaneANumIsochFrames;
	k1->mTransactionsInFlight++;
	
	return err;
}

// isochComplete() is the callback executed whenever an isochronous transfer completes. 
// Since this is called at main interrupt time, it must return as quickly as possible. 
// It is only responsible for scheduling the next transfer into the next transaction buffer. 
// 
void isochComplete(void *refCon, IOReturn result, void *arg0)
{
	IOReturn err;
	
	K1IsocTransaction *t = (K1IsocTransaction *)refCon;
	SoundplaneDriver *k1 = t->parent;
	assert(k1);	
	
	k1->mTransactionsInFlight--;

	if (!k1->dev) return;
	MLSoundplaneState state = k1->getDeviceState();
	if (state == kNoDevice) return;
	if (state == kDeviceIsTerminating) return;
	if (state == kDeviceSuspend) return;

	switch (result)
	{
		case kIOReturnSuccess:
		case kIOReturnUnderrun:
			break;
			
		case kIOReturnIsoTooOld:		
		default:
			printf("isochComplete error: %s\n", io_err_string(result));
			// try to recover.
			k1->setDeviceState(kDeviceConnected);
			break;
	}
	
	K1IsocTransaction *pNextTransactionBuffer = k1->getTransactionData(t->endpointIndex, (t->bufIndex + kSoundplaneABuffersInFlight) & kSoundplaneABuffersMask);
	
#ifdef SHOW_ALL_SEQUENCE_NUMBERS
	int index = pNextTransactionBuffer->endpointIndex;
	int start = getTransactionSequenceNumber(pNextTransaction, 0);
	int end = getTransactionSequenceNumber(pNextTransactionBuffer, kSoundplaneANumIsochFrames - 1);
	printf("endpoint %d: %d - %d\n", index, start, end);
	if (start > end)
	{		
		for(int f=0; f<kSoundplaneANumIsochFrames; ++f)
		{
			if (f % 5 == 0) printf("\n");
			printf("%d ", getTransactionSequenceNumber(pNextTransactionBuffer, f));
		}
		printf("\n\n");
	}
#endif
		
	err = scheduleIsoch(k1, pNextTransactionBuffer);
}

// --------------------------------------------------------------------------------
#pragma mark device utilities

IOReturn ConfigureDevice(IOUSBDeviceInterface187 **dev)
{
	UInt8							numConf;
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
	to set the configuration of a composite class device that’s matched by the 
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

IOReturn SetBusFrameNumber(SoundplaneDriver *k1)
{
	IOReturn err;
	AbsoluteTime atTime;

	err = (*k1->dev)->GetBusFrameNumber(k1->dev, &k1->busFrameNumber[0], &atTime);
	if (kIOReturnSuccess != err)
		return err;
#ifdef VERBOSE
	printf("Bus Frame Number: %llu @ %X%08X\n", k1->busFrameNumber[0], (int)atTime.hi, (int)atTime.lo);
#endif
	k1->busFrameNumber[0] += 50;	// schedule 50 ms into the future
	k1->busFrameNumber[1] = k1->busFrameNumber[0];
	return kIOReturnSuccess;
}


// deviceAdded() is called by the callback set up in the grab thread when a new Soundplane device is found. 
//
void deviceAdded(void *refCon, io_iterator_t iterator)
{
	SoundplaneDriver			*k1 = (SoundplaneDriver *)refCon;
	kern_return_t				kr;
	IOReturn					err;
	io_service_t				usbDeviceRef;
	io_iterator_t				interfaceIterator;
	io_service_t				usbInterfaceRef;
	IOCFPlugInInterface			**plugInInterface = NULL;
	IOUSBDeviceInterface187		**dev = NULL;	// 182, 187, 197
	IOUSBInterfaceInterface192	**intf;
	IOUSBFindInterfaceRequest	req;
	ULONG						res;
	SInt32						score;
	UInt32						powerAvailable;
#ifdef VERBOSE
	UInt16						vendor;
	UInt16						product;
	UInt16						release;
#endif
	int							i, j;
	UInt8						n;

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
		printf("Available Bus Power: %d mA\n", (int)(2*powerAvailable));
		// REVIEW: GetDeviceSpeed()
#ifdef VERBOSE
		
        // REVIEW: technically should check these err values
        err = (*dev)->GetDeviceVendor(dev, &vendor);
        err = (*dev)->GetDeviceProduct(dev, &product);
        err = (*dev)->GetDeviceReleaseNumber(dev, &release);
		printf("Vendor:%04X Product:%04X Release Number:", vendor, product);
		// printBCD(release);
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
						UInt8		direction;
						UInt8		number;
						UInt8		transferType;
						UInt16		maxPacketSize;
						UInt8		interval;

						err = (*intf)->GetPipeProperties(intf, i, &direction, &number, &transferType, &maxPacketSize, &interval);
						if (kIOReturnSuccess != err)
						{
							fprintf(stderr, "endpoint %d - could not get endpoint properties (%08x)\n", i, err);
							goto close;
						}
					}
					// create and setup transaction data structures for each buffer in each isoch endpoint
					for (i = 0; i < kSoundplaneANumEndpoints; i++)
					{
						for (j = 0; j < kSoundplaneABuffers; j++)
						{
							k1->getTransactionData(i, j)->endpointNum = kSoundplaneAEndpointStartIdx + i;
							k1->getTransactionData(i, j)->endpointIndex = i;
							k1->getTransactionData(i, j)->bufIndex = j;
							size_t payloadSize = sizeof(SoundplaneADataPacket) * kSoundplaneANumIsochFrames;

							err = (*intf)->LowLatencyCreateBuffer(intf, (void **)&k1->getTransactionData(i, j)->payloads, payloadSize, kUSBLowLatencyReadBuffer);
							if (kIOReturnSuccess != err)
							{
								fprintf(stderr, "could not create read buffer #%d (%08x)\n", j, err);
								goto close;
							}
							bzero(k1->getTransactionData(i, j)->payloads, payloadSize);
							
							size_t isocFrameSize = kSoundplaneANumIsochFrames * sizeof(IOUSBLowLatencyIsocFrame);
							err = (*intf)->LowLatencyCreateBuffer(intf, (void **)&k1->getTransactionData(i, j)->isocFrames, isocFrameSize, kUSBLowLatencyFrameListBuffer);
							if (kIOReturnSuccess != err)
							{
								fprintf(stderr, "could not create frame list buffer #%d (%08x)\n", j, err);
								goto close;
							}
							bzero(k1->getTransactionData(i, j)->isocFrames, isocFrameSize);
							k1->getTransactionData(i, j)->parent = k1;
						}
					}
					
					// set initial state before isoch schedule
					k1->setDeviceState(kDeviceConnected);										
					err = SetBusFrameNumber(k1);
					assert(!err);					
				
					// for each endpoint, schedule first transaction and
					// a few buffers into the future
					for (j = 0; j < kSoundplaneABuffersInFlight; j++)
					{
						for (i = 0; i < kSoundplaneANumEndpoints; i++)
						{
							err = scheduleIsoch(k1, k1->getTransactionData(i, j));
							if (kIOReturnSuccess != err)
							{
								show_io_err("scheduleIsoch", err);
								goto close;
							}
						}
					}
				}
				else
				{
					//	err = (*intf)->USBInterfaceClose(intf);
					res = (*intf)->Release(intf);	// REVIEW: This kills the async read!!!!!
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
}
					
// if device is unplugged, remove device and go back to waiting. 
//
void deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
	SoundplaneDriver *k1 = (SoundplaneDriver *)refCon;

	if (kIOMessageServiceIsTerminated == messageType)
	{
		k1->removeDevice();
	}
}

// -------------------------------------------------------------------------------
#pragma mark thread utilities

void setThreadPriority (pthread_t inThread, UInt32 inPriority, Boolean inIsFixed)
{
	if (inPriority == 96)
	{
        // REAL-TIME / TIME-CONSTRAINT THREAD
        thread_time_constraint_policy_data_t		theTCPolicy;
        
		theTCPolicy.period = 1000 * 1000;
		theTCPolicy.computation = 50 * 1000;
		theTCPolicy.constraint = 1000 * 1000;
		theTCPolicy.preemptible = true;
		thread_policy_set (pthread_mach_thread_np(inThread), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&theTCPolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	} 
	else 
	{
        // OTHER THREADS
		thread_extended_policy_data_t		theFixedPolicy;
        thread_precedence_policy_data_t		thePrecedencePolicy;
        SInt32								relativePriority;
        
		// [1] SET FIXED / NOT FIXED
        theFixedPolicy.timeshare = !inIsFixed;
        thread_policy_set (pthread_mach_thread_np(inThread), THREAD_EXTENDED_POLICY, (thread_policy_t)&theFixedPolicy, THREAD_EXTENDED_POLICY_COUNT);
        
		// [2] SET PRECEDENCE
        relativePriority = inPriority;
        thePrecedencePolicy.importance = relativePriority;
        thread_policy_set (pthread_mach_thread_np(inThread), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&thePrecedencePolicy, THREAD_PRECEDENCE_POLICY_COUNT);
	}
}

UInt32 getThreadPriority(pthread_t inThread, int inWhichPriority)
{
    thread_basic_info_data_t threadInfo;
    policy_info_data_t thePolicyInfo;
    unsigned int count;

    // get basic info
    count = THREAD_BASIC_INFO_COUNT;
    thread_info(pthread_mach_thread_np(inThread), THREAD_BASIC_INFO, (thread_info_t)&threadInfo, &count);

    switch (threadInfo.policy) 
	{
        case POLICY_TIMESHARE:
            count = POLICY_TIMESHARE_INFO_COUNT;
            thread_info(pthread_mach_thread_np(inThread), THREAD_SCHED_TIMESHARE_INFO, (thread_info_t)&(thePolicyInfo.ts), &count);
            if (inWhichPriority) {
                return thePolicyInfo.ts.cur_priority;
            } else {
                return thePolicyInfo.ts.base_priority;
            }
            break;

        case POLICY_FIFO:
            count = POLICY_FIFO_INFO_COUNT;
            thread_info(pthread_mach_thread_np(inThread), THREAD_SCHED_FIFO_INFO, (thread_info_t)&(thePolicyInfo.fifo), &count);
            if ( (thePolicyInfo.fifo.depressed) && (inWhichPriority) ) {
                return thePolicyInfo.fifo.depress_priority;
            }
            return thePolicyInfo.fifo.base_priority;
            break;

        case POLICY_RR:
            count = POLICY_RR_INFO_COUNT;
            thread_info(pthread_mach_thread_np(inThread), THREAD_SCHED_RR_INFO, (thread_info_t)&(thePolicyInfo.rr), &count);
            if ( (thePolicyInfo.rr.depressed) && (inWhichPriority) ) {
                return thePolicyInfo.rr.depress_priority;
            }
            return thePolicyInfo.rr.base_priority;
            break;
    }

    return 0;
}

UInt32 getThreadPolicy(pthread_t inThread)
{
    thread_basic_info_data_t threadInfo;
    unsigned int count;

    // get basic info
    count = THREAD_BASIC_INFO_COUNT;
    thread_info(pthread_mach_thread_np(inThread), THREAD_BASIC_INFO, (thread_info_t)&threadInfo, &count);

    return (threadInfo.policy);
}

void displayThreadInfo(pthread_t inThread)
{
	printf("--------\n");
	
	printf("policy: ");
	UInt32 policy = getThreadPolicy(inThread);
	switch(policy)
	{
		case POLICY_TIMESHARE:
			printf("POLICY_TIMESHARE");
		break;
		case POLICY_FIFO:
			printf("POLICY_FIFO");
		break;
		case POLICY_RR:
			printf("POLICY_RR");
		break;
		default:
			printf("UNKNOWN");
		break;
	}
	printf (", req. prio: %d, new prio: %d \n", 
		(int)getThreadPriority(inThread, 0), (int)getThreadPriority(inThread, 1));
	printf("--------\n");
		
}


float frameDiff(float * p1, float * p2, int frameSize);
float frameDiff(float * p1, float * p2, int frameSize)
{
	float sum = 0.f;
	for(int i=0; i<frameSize; ++i)
	{
		sum += fabs(p2[i] - p1[i]);
	}
	return sum;
}

void dumpFrame(float* frame);
void dumpFrame(float* frame)
{
	for(int j=0; j<kSoundplaneHeight; ++j)
	{
		printf("row %d: ", j);
		for(int i=0; i<kSoundplaneWidth; ++i)
		{
			printf("%f ", frame[j*kSoundplaneWidth + i]);
		}
		printf("\n");
	}
}

// -------------------------------------------------------------------------------
#pragma mark main thread routines

// This thread is responsible for finding and adding USB devices matching the Soundplane.
// Execution is controlled by a Core Foundation (Cocoa) run loop. 
// 
// TODO investigate using a pthread here for consistency and code that looks more
// similar across multiple platforms. 
//
void *soundplaneGrabThread(void *arg)
{
	SoundplaneDriver* k1 = static_cast<SoundplaneDriver*>(arg);
	bool OK = true;
	kern_return_t				kr;
	CFMutableDictionaryRef		matchingDict;
	CFRunLoopSourceRef			runLoopSource;
	CFNumberRef					numberRef;
	SInt32						usbVendor = 0x0451;
	SInt32						usbProduct = 0x5100;

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

		k1->notifyPort = IONotificationPortCreate(kIOMasterPortDefault);
		runLoopSource = IONotificationPortGetRunLoopSource(k1->notifyPort);
		CFRunLoopAddSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopDefaultMode);

		// Set up asynchronous callback to call deviceAdded() when a Soundplane is found.
		// TODO REVIEW: Check out IOServiceMatching() and IOServiceNameMatching()
		kr = IOServiceAddMatchingNotification(  k1->notifyPort,
												kIOFirstMatchNotification,
												matchingDict,
												deviceAdded,
												k1, // refCon
												&(k1->matchedIter) );
												
		// Iterate once to get already-present devices and arm the notification
		deviceAdded(k1, k1->matchedIter);
		
		// Start the run loop. Now we'll receive notifications and remain looping here until the 
		// run loop is stopped with CFRunLoopStop or all the sources and timers are removed from 
		// the default run loop mode.
		// For more information about how Core Foundation run loops behave, see “Run Loops” in 
		// Apple’s Threading Programming Guide.
		CFRunLoopRun();
		
		// clean up
		CFRunLoopRemoveSource(CFRunLoopGetCurrent(), runLoopSource, kCFRunLoopDefaultMode);
	}
	return NULL;
}

// This thread is responsible for collecting all data from the Soundplane. The routines isochComplete()
// and scheduleIsoch() combine to keep data running into the transfer buffers in round-robin fashion. 
// This thread looks at those buffers and attempts to stay in sync with incoming data, first
// bu finding the most recent transfer to start from, then watching as transfers are filled in
// with successive sequence numbers. When a matching pair of endpoints is found, reclockFrameToBuffer()
// is called to send the data to any listeners.
// 
void *soundplaneProcessThread(void *arg)
{
	SoundplaneDriver* k1 = static_cast<SoundplaneDriver*>(arg);
	UInt16 curSeqNum0, curSeqNum1;
	UInt16 maxSeqNum0, maxSeqNum1;	
	UInt16 currentCompleteSequence = 0;		
	UInt16 newestCompleteSequence = 0;
	unsigned char* pPayload0 = 0; 
	unsigned char* pPayload1 = 0;
	UInt16 curBytes0, curBytes1;
	UInt16 nextBytes0, nextBytes1;
	float * pWorkingFrame;
	float * pPrevFrame;
	
	// transaction data buffer index, 0 to kSoundplaneABuffers-1
	int bufferIndex = 0; 
	
	// current frame within transaction buffer, 0 to kSoundplaneANumIsochFrames-1
	int frameIndex = 0; 
	
	int droppedTransactions = 0;
	int framesReceived = 0;
	int badXfers = 0;
	int zeros = 0;
	int gaps = 0;
	int waiting = 0;
	int gotNext = 0;
	bool printThis = false;
	bool lost = true;	
	bool advance = false;
	
	int initCtr = 0;
	const float kMaxFrameDiff = 8.0f;
	
	// init
	size_t outputFrameSize = kSoundplaneWidth * kSoundplaneHeight * sizeof(float);
	pWorkingFrame = (float *)malloc(outputFrameSize);
	if (!pWorkingFrame)
	{
		fprintf(stderr, "soundplaneProcessThread: couldn't create working frame!\n");
	}
	pPrevFrame = (float *)malloc(outputFrameSize);
	if (!pPrevFrame)
	{
		fprintf(stderr, "soundplaneProcessThread: couldn't create frame!\n");
	}
	
	while(k1->getDeviceState() != kDeviceIsTerminating)
	{
		// wait for k1 grab and initialization
		while (k1->getDeviceState() == kNoDevice)
		{
			usleep(100*1000);
		}
		while (k1->getDeviceState() == kDeviceSuspend)
		{
			usleep(100*1000);
		}
		while(k1->getDeviceState() == kDeviceConnected)
		{
			// initialize: find latest scheduled transaction
			UInt64 maxTransactionStartFrame = 0; 
			int maxIdx = -1;
			for(int j=0; j < kSoundplaneABuffers; ++j)
			{
				K1IsocTransaction* t = k1->getTransactionData(0, j);
				UInt64 frame = t->busFrameNumber;
				if(frame > maxTransactionStartFrame)
				{
					maxIdx = j;
					maxTransactionStartFrame = frame;
				}
			}
		
			// found at least one scheduled transaction: read backwards through all transactions
			// to get most recent complete frame.
			if (maxIdx >= 0)
			{
				int seq0, seq1;
				seq0 = seq1 = 0;
				lost = true;
				
				for(bufferIndex=maxIdx; (bufferIndex >= 0) && lost; bufferIndex--)
				{
					for(frameIndex=kSoundplaneANumIsochFrames - 1; (frameIndex >= 0) && lost; frameIndex--)
					{
						int l0 = k1->getSequenceNumber(0, bufferIndex, frameIndex);
						int l1 = k1->getSequenceNumber(1, bufferIndex, frameIndex);
						
						if (l0)
							seq0 = l0;
						if (l1)
							seq1 = l1;
						if (seq0 && seq1)
						{
							bufferIndex++;
							frameIndex++;
							curSeqNum0 = maxSeqNum0 = seq0;
							curSeqNum1 = maxSeqNum1 = seq1;
							currentCompleteSequence = std::min(curSeqNum0, curSeqNum1);
						
							// finally, we have sync.
							//
							k1->setDefaultCarriers();
							k1->setDeviceState(kDeviceHasIsochSync);	
							lost = false;	
						}
					}
				}
				
				if (!lost)
				{
					printf("connecting at sequence numbers: %d / %d\n", curSeqNum0, curSeqNum1);
				}
			}
			usleep(100*1000);
		}
		
		// resume: for now just like kDeviceConnected, but without default carriers
		//
		while(k1->getDeviceState() == kDeviceResume)
		{
			// initialize: find latest scheduled transaction
			UInt64 maxTransactionStartFrame = 0; 
			int maxIdx = -1;
			for(int j=0; j < kSoundplaneABuffers; ++j)
			{
				K1IsocTransaction* t = k1->getTransactionData(0, j);
				UInt64 frame = t->busFrameNumber;
				if(frame > maxTransactionStartFrame)
				{
					maxIdx = j;
					maxTransactionStartFrame = frame;
				}
			}
		
			// found at least one scheduled transaction: read backwards through all transactions
			// to get most recent complete frame.
			if (maxIdx >= 0)
			{
				int seq0, seq1;
				seq0 = seq1 = 0;
				lost = true;
				
				for(bufferIndex=maxIdx; (bufferIndex >= 0) && lost; bufferIndex--)
				{
					for(frameIndex=kSoundplaneANumIsochFrames - 1; (frameIndex >= 0) && lost; frameIndex--)
					{
						int l0 = k1->getSequenceNumber(0, bufferIndex, frameIndex);
						int l1 = k1->getSequenceNumber(1, bufferIndex, frameIndex);
						
						if (l0)
							seq0 = l0;
						if (l1)
							seq1 = l1;
						if (seq0 && seq1)
						{
							bufferIndex++;
							frameIndex++;
							curSeqNum0 = maxSeqNum0 = seq0;
							curSeqNum1 = maxSeqNum1 = seq1;
							currentCompleteSequence = std::min(curSeqNum0, curSeqNum1);
						
							k1->setDeviceState(kDeviceHasIsochSync);	
							lost = false;	
						}
					}
				}
				
				if (!lost)
				{
					printf("resuming at sequence numbers: %d / %d\n", curSeqNum0, curSeqNum1);
				}
			}
			usleep(100*1000);
		}
		
		initCtr = 0;
		while(k1->getDeviceState() == kDeviceHasIsochSync) 
		{		
			// look ahead by 1 frame to see if data was received for either surface of next frame
			//
			int lookahead = 1;
			nextBytes0 = k1->getTransferBytesReceived(0, bufferIndex, frameIndex, lookahead);
			nextBytes1 = k1->getTransferBytesReceived(1, bufferIndex, frameIndex, lookahead);
			
			// does next frame contain something ?
			// if so, we know this frame is done.
			// checking curBytes for this frame is not enough, 
			// because it seems current count may be filled in 
			// before payload.
			advance = (nextBytes0 || nextBytes1);

			if (advance)
			{
				waiting = 0;
				gotNext++;
				
				// see what data was received for current frame.  The Soundplane A always returns
				// either a full packet of 386 bytes, or 0.
				//
				curBytes0 = k1->getTransferBytesReceived(0, bufferIndex, frameIndex);
				curBytes1 = k1->getTransferBytesReceived(1, bufferIndex, frameIndex);
					
				// get sequence numbers from current frame
				if (curBytes0)
				{
					curSeqNum0 = k1->getSequenceNumber(0, bufferIndex, frameIndex);
					maxSeqNum0 = curSeqNum0;
				}
				if (curBytes1)
				{
					curSeqNum1 = k1->getSequenceNumber(1, bufferIndex, frameIndex);				
					maxSeqNum1 = curSeqNum1;
				}
				// get newest sequence number for which we have all surfaces				
				if (maxSeqNum0 && maxSeqNum1) // look for next in sequence
				{
					newestCompleteSequence = std::min(maxSeqNum0, maxSeqNum1);
				}
				else // handle zero wrap
				{
					newestCompleteSequence = std::max(maxSeqNum0, maxSeqNum1);
				}
				
				// if this is different from the most recent sequence sent,
				// send new sequence to buffer.
				if (newestCompleteSequence != currentCompleteSequence)
				{
					// look for gaps
					int sequenceWanted = ((int)currentCompleteSequence + 1)&0x0000FFFF;					
					if (newestCompleteSequence != sequenceWanted)
					{
						k1->reportDeviceError(kDevGapInSequence, currentCompleteSequence, newestCompleteSequence, 0., 0.);
						printThis = true;
						gaps++;
												
						// TODO try to recover if too many gaps near each other
					}
					
					currentCompleteSequence = newestCompleteSequence;
					
					// scan backwards in buffers from current position to get payloads matching currentCompleteSequence
					// TODO save indices
					//
					pPayload0 = pPayload1 = 0;
					int b = bufferIndex; 
					int f = frameIndex; 
					for(int k = 0; k > -kSoundplaneABuffers * kSoundplaneANumIsochFrames; k--)
					{
						int seq0 = k1->getSequenceNumber(0, b, f, k);
						if (seq0 == currentCompleteSequence)
						{
							pPayload0 = k1->getPayloadPtr(0, b, f, k);
						}
						int seq1 = k1->getSequenceNumber(1, b, f, k);
						if (seq1 == currentCompleteSequence)
						{
							pPayload1 = k1->getPayloadPtr(1, b, f, k);
						}
						if (pPayload0 && pPayload1) 
						{
							// printf("[%d] \n", currentCompleteSequence);
							framesReceived++;
							break;
						}
					} 
			
					if (pPayload0 && pPayload1)
					{
						K1_unpack_float2(pPayload0, pPayload1, pWorkingFrame);
						K1_clear_edges(pWorkingFrame);
						if(k1->startupCtr > kSoundplaneStartupFrames)
						{
							float df = frameDiff(pPrevFrame, pWorkingFrame, kSoundplaneWidth * kSoundplaneHeight);
							if (df < kMaxFrameDiff)
							{
								// we are OK, the data gets out normally
								k1->reclockFrameToBuffer(pWorkingFrame);
							}
							else
							{
								// possible sensor glitch.  also occurs when changing carriers.
								k1->reportDeviceError(kDevDataDiffTooLarge, k1->startupCtr, 0, df, 0.);
								k1->dumpDeviceData(pPrevFrame, kSoundplaneWidth * kSoundplaneHeight);
								k1->dumpDeviceData(pWorkingFrame, kSoundplaneWidth * kSoundplaneHeight);
								k1->startupCtr = 0;
							}
						}
						else
						{
							// wait for initialization
							k1->startupCtr++;
						}
						
						memcpy(pPrevFrame, pWorkingFrame, outputFrameSize);						
					}
				}
			
				// increment current frame / buffer position
				if (++frameIndex >= kSoundplaneANumIsochFrames)
				{
					frameIndex = 0;
					bufferIndex++;
					bufferIndex &= kSoundplaneABuffersMask;
				}
			}
			else 
			{
				waiting++;
				// NOT WORKING
				if (waiting > 1000) // delay amount? -- try reconnect
				{
					printf("RESYNCHING\n");
					waiting = 0;
					k1->setDeviceState(kNoDevice);	
				}
			}

			if (printThis)
			{
				if (k1->dev)
				{
		//			AbsoluteTime atTime;
		//			UInt64 bf;
		//			OSErr err = (*k1->dev)->GetBusFrameNumber(k1->dev, &bf, &atTime);		
		//			dumpTransactions(k1, bufferIndex, frameIndex);
					
					printf("current seq num: %d / %d\n", curSeqNum0, curSeqNum1); 
		//			printf("now: %u:%u\n", (int)atTime.hi, (int)atTime.lo);
					printf("process: gaps %d, dropped %d, gotNext %d \n", gaps, droppedTransactions, gotNext);
					printf("process: good frames %d, bad xfers %d \n", framesReceived, badXfers);					
					printf("process: current sequence: [%d, %d] \n", maxSeqNum0, maxSeqNum1);	
					printf("process: transactions in flight: %d\n", k1->mTransactionsInFlight);
					
					droppedTransactions = 0;
					framesReceived = 0;
					badXfers = 0;
					zeros = 0;
					gaps = 0;
					gotNext = 0;
				}
				printThis = false;
			}
					
			// wait .5 ms
			usleep(500);
		}
	}
	
	// confirm termination
	
	return NULL;
}

// -------------------------------------------------------------------------------
#pragma mark transfer utilities

int GetStringDescriptor(IOUSBDeviceInterface187 **dev, UInt8 descIndex, char *destBuf, UInt16 maxLen, UInt16 lang) 
{
    IOUSBDevRequest req;
    UInt8 		desc[256]; // Max possible descriptor length
    UInt8 		desc2[256]; // Max possible descriptor length
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
    
    return destLen;
}

void dumpTransactions(void *arg, int bufferIndex, int frameIndex)
{
	SoundplaneDriver* k1 = static_cast<SoundplaneDriver*>(arg);
	for (int j=0; j<kSoundplaneABuffers; ++j)
	{
		K1IsocTransaction* t0 = k1->getTransactionData(0, j);
		K1IsocTransaction* t1 = k1->getTransactionData(1, j);
		UInt64 b0 = t0->busFrameNumber;
		UInt64 b1 = t1->busFrameNumber;
		printf("\n%d: frame %09llu/%09llu", j, b0, b1);
		if (bufferIndex == j)
		{
			printf(" *current*");
		}
		for(int f=0; f<kSoundplaneANumIsochFrames; ++f)
		{
			IOUSBLowLatencyIsocFrame* frame0, *frame1;
			UInt16 seq0, seq1;
			frame0 = &(t0->isocFrames[f]);
			frame1 = &(t1->isocFrames[f]);
			
			seq0 = getTransactionSequenceNumber(t0, f);
			seq1 = getTransactionSequenceNumber(t1, f);

			if (f % 4 == 0) printf("\n");
			printf("%05d:%d:%d/", (int)seq0, (frame0->frReqCount), (frame0->frActCount));
			printf("%05d:%d:%d", (int)seq1, (frame1->frReqCount), (frame1->frActCount));
			if ((frameIndex == f) && (bufferIndex == j))
			{
				printf("*  ");
			}
			else
			{
				printf("   ");
			}
		}
		printf("\n");				
	}
}

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

