/* $Id: Soundplane.m,v 1.26 2010/10/26 07:19:31 sounds Exp $
 *  Soundplane.m
 *  Soundplane
 *
 *  Created by Sound Consulting on 2010/06/25.
 *  Copyright 2010 Sound Consulting. All rights reserved.
 *
 */

#include "K1Private.h"
#include "display.h"
#include "err.h"
#import <Cocoa/Cocoa.h>
#import <Foundation/NSByteOrder.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <mach/mach.h>
#include <pthread.h>

//#define VERBOSE
//#define CALLBACK_DISPLAY
#define SHOW_BUS_FRAME_NUMBER
#define SELECT_ISO			2

#define printBCD(bcd) printf("%hhx.%hhx.%hhx\n", bcd >> 8, 0x0f & bcd >> 4, 0x0f & bcd)

typedef struct K1Client
{
	void					*ref;
	NSPort					*surfacePort;
	UInt16					*surfaceBuffer;
	MLSP_UserInputCallback	userInputCallback;
} K1Client;

typedef struct
{
	unsigned short	code;
	unsigned short	recipient;
	unsigned short	offset;
	unsigned short	sensor;
	const char *	caption;
} statusMapEntry;

statusMapEntry statusMap[] =
{
	{ codeK1State,		kUSBDevice,	0x0000, 0, "k1_state" },
	{ codeGLoop,		kUSBDevice,	0x0001, 0, "gLoop" },
	{ codeUSBActive,	kUSBDevice,	0x0002, 0, "usb_active" },
	{ codeSeconds,		kUSBDevice,	0x4000, 0, "seconds" },
	{ codeXmitCnt,		kUSBDevice,	0x4001, 0, "xmitCnt" },
	{ codeDMA1Cnt,		kUSBDevice,	0x4002, 0, "dma1_cnt" },
	{ codeTimeoutCnt,	kUSBDevice,	0x4004, 0, "timeout_cnt" },
	{ codeTransCnt,		kUSBDevice,	0x4005, 0, "trans_cnt" },
	{ codeUSBInterrupts,kUSBDevice,	0x8000, 0, "usb_interrupts" },
	{ codeIsoCnt,		kUSBDevice,	0x8001, 0, "iso_cnt" },
	{ codeDropCnt,		kUSBDevice,	0x8002, 0, "drop_cnt" },
	{ codeBuffer8,		kUSBDevice,	0xC001, 0, "buffer(8)" },
	{ codeBuffer16,		kUSBDevice,	0xD001, 0, "buffer(16)" },
	{ codeDest32,		kUSBDevice,	0xE000, 0, "dest(32)" },
	{ codeBuffer32,		kUSBDevice,	0xE001, 0, "buffer(32)" },
	{ codeDeInterleave,	kUSBDevice,	0xE002, 0, "deinterleave" },
	{ codeFreqDom,		kUSBDevice,	0xE003, 0, "freqdom" },
	{ codeBufferA,		kUSBDevice,	0xE004, 0, "bufferA" },
	{ codeSquare,		kUSBDevice,	0xE005, 0, "lmag" },
	{ codeSum,			kUSBDevice,	0xE006, 0, "mag" },
	{ codeRoot,			kUSBDevice,	0xE007, 0, "root" },
	{ codeDest,			kUSBDevice,	0xF000, 0, "dest" },
	{ codeBuffer,		kUSBDevice,	0xF001, 0, "buffer" },
	{ codeSeconds1,		kUSBOther,	0x4000, 0, "seconds1" },
	{ codeRcvCnt1,		kUSBOther,	0x4002, 0, "rcvCnt1" },
	{ codeDropCnt1,		kUSBOther,	0x4003, 0, "drop_cnt1" },
	{ codeSurfaceCnt1,	kUSBOther,	0x4004, 0, "surface_cnt1" },
	{ codeIsoSem1,		kUSBOther,	0x4005, 0, "iso_sem1" },
	{ codeCmdDropCnt1,	kUSBOther,	0x4006, 0, "cmd_drop_cnt1" },
	{ codeIntCnt1,		kUSBOther,	0x8000, 0, "int_cnt1" },
	{ codeDMACnt1,		kUSBOther,	0x8001, 0, "dma_cnt1" },
	{ codeBlockCnt1,	kUSBOther,	0x8002, 0, "block_cnt1" },
	{ codeHalfCnt1,		kUSBOther,	0x8003, 0, "half_cnt1" },
	{ codeCmdIntCnt1,	kUSBOther,	0x8004, 0, "cmd_int_cnt1" },
	{ codePreflight1,	kUSBOther,	0xC000, 0, "preflightResults1" },
	{ codeSensor1h,		kUSBOther,	0xE000, 0, "sensor1(32)" },
	{ codeSensor1,		kUSBOther,	0xF000, 0, "sensor1" },
	{ codeSeconds2,		kUSBOther,	0x4000, 1, "seconds2" },
	{ codeRcvCnt2,		kUSBOther,	0x4002, 1, "rcvCnt2" },
	{ codeDropCnt2,		kUSBOther,	0x4003, 1, "drop_cnt2" },
	{ codeSurfaceCnt2,	kUSBOther,	0x4004, 1, "surface_cnt2" },
	{ codeIsoSem2,		kUSBOther,	0x4005, 1, "iso_sem2" },
	{ codeCmdDropCnt2,	kUSBOther,	0x4006, 1, "cmd_drop_cnt2" },
	{ codeIntCnt2,		kUSBOther,	0x8000, 1, "int_cnt2" },
	{ codeDMACnt2,		kUSBOther,	0x8001, 1, "dma_cnt2" },
	{ codeBlockCnt2,	kUSBOther,	0x8002, 1, "block_cnt2" },
	{ codeHalfCnt2,		kUSBOther,	0x8003, 1, "half_cnt2" },
	{ codeCmdIntCnt2,	kUSBOther,	0x8004, 1, "cmd_int_cnt2" },
	{ codePreflight2,	kUSBOther,	0xC000, 1, "preflightResults2" },
	{ codeSensor2h,		kUSBOther,	0xE000, 1, "sensor2(32)" },
	{ codeSensor2,		kUSBOther,	0xF000, 1, "sensor2" },
	{ 0,				0,			0x0000, 0, NULL }
};


UInt16 StatusLengthAtOffset(UInt16 v)
{
	UInt16 s = 1 << (3 & v >> 14);

	if (8 == s)
		s <<= 3 & v >> 12;
	return s;
}

IOReturn getStatus(IOUSBDeviceInterface187 **dev, statusMapEntry *entry, void *p)
{
    IOUSBDevRequest	request;

    request.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBVendor, entry->recipient);
    request.bRequest = 0;
    request.wValue = entry->offset;
    request.wIndex = entry->sensor;
    request.wLength = StatusLengthAtOffset(entry->offset);
    request.pData = p;

    return (*dev)->DeviceRequest(dev, &request);
}

int MLSP_disableCarriers(void *ref, unsigned long mask)
{
	K1Instance *k1 = ref;
    IOUSBDevRequest	request;

    request.bmRequestType = USBmakebmRequestType(kUSBOut, kUSBVendor, kUSBDevice);
    request.bRequest = 1;
	request.wValue = mask >> 16;
	request.wIndex = mask;
	request.wLength = 0;
	request.pData = NULL;

	return (*k1->dev)->DeviceRequest(k1->dev, &request);
}

Boolean MLSP_getWordStatus(IOUSBDeviceInterface187 **dev, unsigned char code, UInt16 *pStatus)
{
	IOReturn err = 0;
	UInt16 wStatus;	
	int i;

	if (NULL == dev || NULL == *dev)
		return 1;
	for (i = 0; i < sizeof statusMap / sizeof statusMap[0]; i++)
	{
		if (code == statusMap[i].code)
		{
			int len = StatusLengthAtOffset(statusMap[i].offset);
			assert(sizeof(*pStatus) == len);
			err = getStatus(dev, &statusMap[i], &wStatus);
			if (!err)
			{
				*pStatus = CFSwapInt16LittleToHost(wStatus);
				return 0;
			}
		}
	}
	if (err)
		fprintf(stderr, "Control Status Error %x\n", err);
	return 1;
}

Boolean MLSP_getLongStatus(IOUSBDeviceInterface187 **dev, unsigned char code, UInt32 *pStatus)
{
	IOReturn err = 0;
	UInt32 lStatus;	
	int i;
	
	if (NULL == dev || NULL == *dev)
		return 1;
	for (i = 0; i < sizeof statusMap / sizeof statusMap[0]; i++)
	{
		if (code == statusMap[i].code)
		{
			int len = StatusLengthAtOffset(statusMap[i].offset);
			assert(sizeof(*pStatus) == len);
			err = getStatus(dev, &statusMap[i], &lStatus);
			if (!err)
			{
				*pStatus = CFSwapInt32LittleToHost(lStatus);
				return 0;
			}
		}
	}
	if (err)
		fprintf(stderr, "Control Status Error %x\n", err);
	return 1;
}

void MLSP_printStatus(IOUSBDeviceInterface187 **dev, unsigned char code)
{
	IOReturn err = 0;
	UInt8 bStatus;
	UInt16 wStatus;
	UInt32 lStatus;
	UInt8 llStatus[65];
	int i, j;
	
	if (NULL == dev || NULL == *dev)
		return;
	for (i = 0; i < sizeof statusMap / sizeof statusMap[0]; i++)
	{
		if (code == statusMap[i].code)
		{
			int len = StatusLengthAtOffset(statusMap[i].offset);
			switch (len)
			{
				case 1:
					err = getStatus(dev, &statusMap[i], &bStatus);
					if (!err)
						printf("%02x %s\n", bStatus, statusMap[i].caption);
						break;
				case 2:
					err = getStatus(dev, &statusMap[i], &wStatus);
					if (!err)
						printf("%04x %s\n", CFSwapInt16LittleToHost(wStatus), statusMap[i].caption);
						break;
				case 4:
					err = getStatus(dev, &statusMap[i], &lStatus);
					if (!err)
						printf("%08x %s\n", CFSwapInt32LittleToHost(lStatus), statusMap[i].caption);
						break;
				case 8:
				case 16:
				case 32:
				case 64:
					err = getStatus(dev, &statusMap[i], &llStatus);
					if (!err)
					{
						for (j = 0; j < len; j += 2)
							printf("%02x%02x ", llStatus[j + 1], llStatus[j]);
						printf("%s\n", statusMap[i].caption);
					}
						break;
				default:
					break;
			}
		}
	}
	if (err)
		fprintf(stderr, "Control Status Error %x\n", err);
}

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
	// REVIEW: Important: Because a composite class device is configured by the AppleUSBComposite driver, setting the configuration again from your application will result in the destruction of the IOUSBInterface nub objects and the creation of new ones. In general, the only reason to set the configuration of a composite class device thatÕs matched by the AppleUSBComposite driver is to choose a configuration other than the first one.
	// NOTE: This seems to be necessary
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

void InterruptCallback(void *refCon, IOReturn result, void *arg0);

IOReturn ScheduleInterrupt(K1Instance *k1)
{
	IOReturn err;
	Boolean repeat = false;
	assert(k1);
	assert(k1->intf);
	assert(*k1->intf);
	do {
		err = (*k1->intf)->ReadPipeAsync(k1->intf, MLSP_EP_USER, k1->user, MLSP_N_USER_BYTES, InterruptCallback, k1);
		if (kIOUSBPipeStalled == err)
		{
			err = (*k1->intf)->ClearPipeStallBothEnds(k1->intf, MLSP_EP_USER);
			repeat = true;
		}
		else
			repeat = false;
	} while (repeat);
	return err;
}

void InterruptCallback(void *refCon, IOReturn result, void *arg0)
{
	K1Instance *k1 = (K1Instance *)refCon;

	assert(k1);
	if (k1->userInputCallback)
	{
		UInt32 numBytesRead = (UInt32)arg0;

		if (numBytesRead)
		{
			unsigned i, n = (numBytesRead+1)/2;
			unsigned short *packet = k1->user;
			unsigned short inputs[MLK1_N_USER_INPUTS];

			for (i = 0; i < n; i++)
				inputs[i] = USBToHostWord(packet[i]);
			k1->userInputCallback(k1->clientRef, n, inputs);
		}
	}
	if (!k1->isTerminating)
	{
		IOReturn err = ScheduleInterrupt(k1);
		if (kIOReturnSuccess != err)
			show_io_err("EP1 U", err);	// REVIEW: Happens on disconnect
	}
}

IOReturn SetBusFrameNumber(K1Instance *k1)
{
	IOReturn err;
	AbsoluteTime atTime;

	assert(k1);
	assert(k1->dev);
	err = (*k1->dev)->GetBusFrameNumber(k1->dev, &k1->busFrameNumber[0], &atTime);
	if (kIOReturnSuccess != err)
		return err;
#ifdef VERBOSE
	printf("Bus Frame Number: %llu @ %X%08X\n", k1->busFrameNumber[0], atTime.hi, atTime.lo);
#endif
	k1->busFrameNumber[0] += 50;	// 50 ms into the future
	k1->busFrameNumber[1] = k1->busFrameNumber[0];
	return kIOReturnSuccess;
}

UInt64 futureBusFrameNumber(K1Instance *k1, unsigned i)
{
	UInt64 busFrameNumber = k1->busFrameNumber[i];
	k1->busFrameNumber[i] += SP_NUM_FRAMES;
	return busFrameNumber;
}

void IsochCallback(void *refCon, IOReturn result, void *arg0);

IOReturn ScheduleIsoch(K1Instance *k1, K1IsocInstance *k1i)
{
	unsigned k;
	
	if (k1->isTerminating)
		return kIOReturnSuccess;
	assert(k1i);
	assert(k1 == k1i->parent);
	if (k1->displayIndex[k1i->endpointIndex] == k1i->index)
		k1->displayIndex[k1i->endpointIndex] = (k1i->index + 1) & SP_MASK_BUFFERS;
	k1i->hasCompleted = NO;
	k1i->busFrameNumber = futureBusFrameNumber(k1, k1i->endpointIndex);
	for (k = 0; k < SP_NUM_FRAMES; k++)
	{
		k1i->isocFrame[k].frStatus = 0;
		k1i->isocFrame[k].frReqCount = MLSP_N_ISOCH_BYTES;
		k1i->isocFrame[k].frActCount = 0;
		k1i->isocFrame[k].frTimeStamp.hi = 0;
		k1i->isocFrame[k].frTimeStamp.lo = 0;
	}
#ifdef SHOW_BUS_FRAME_NUMBER
	fprintf(stderr, "LowLatencyReadIsochPipeAsync(%p, %d, %p, %llu, %d, %d, %p, %p, %p)\n", k1->intf, k1i->endpointNum, k1i->payload, k1i->busFrameNumber, SP_NUM_FRAMES, SP_UPDATE_FREQUENCY, k1i->isocFrame, IsochCallback, k1i);
#endif
	return (*k1->intf)->LowLatencyReadIsochPipeAsync(k1->intf, k1i->endpointNum, k1i->payload, k1i->busFrameNumber, SP_NUM_FRAMES, SP_UPDATE_FREQUENCY, k1i->isocFrame, IsochCallback, k1i);
}

void IsochCallback(void *refCon, IOReturn result, void *arg0)
{
	IOReturn					err;
	IOUSBLowLatencyIsocFrame	*frameList = (IOUSBLowLatencyIsocFrame *)arg0;
	K1IsocInstance				*k1i = (K1IsocInstance *)refCon;
	K1Instance					*k1 = k1i->parent;

	assert(frameList);
	assert(k1);
	k1i->hasCompleted = YES;
	switch (result)
	{
		case kIOReturnSuccess:
		case kIOReturnUnderrun:
			break;
		case kIOUSBNotSent2Err:
			printf("(iso) Transaction not sent\n");
			break;
		case kIOReturnNoBandwidth:
#ifdef VERBOSE
			printf("No (iso) Bandwidth: bus bandwidth would be exceeded\n");
#endif
			break;
		default:
			// stop on any error (?)
			printf("%s\n", io_err_string(result));
			break;
	}
	k1i = &k1->isocList[k1i->endpointIndex][(k1i->index + SP_N_BUFFERS_IN_FLIGHT) & SP_MASK_BUFFERS];
	err = ScheduleIsoch(k1, k1i);
	if (kIOReturnSuccess != err)
		show_io_err(k1i->endpointIndex ? "EP3" : "EP2", err);	// REVIEW: Happens on disconnect
}

void GeneralInterest(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
	kern_return_t	kr;
	K1Instance		*k1 = (K1Instance *)refCon;

	if (kIOMessageServiceIsTerminated == messageType)
	{
		IOReturn err;

		k1->isTerminating = 1;
		pthread_mutex_lock(&k1->mutex);
        printf("Soundplane A removed.\n");
		if (k1->intf)
		{
			unsigned i, n;
			IOUSBInterfaceInterface192 **intf = k1->intf;

			k1->intf = NULL;
			for (n = 0; n < MLSP_N_ISO_ENDPOINTS; n++)
			{
				for (i = 0; i < SP_N_BUFFERS; i++)
				{
					if (k1->isocList[n][i].payload)
					{
						err = (*intf)->LowLatencyDestroyBuffer(intf, k1->isocList[n][i].payload);
						k1->isocList[n][i].payload = NULL;
					}
					if (k1->isocList[n][i].isocFrame)
					{
						err = (*intf)->LowLatencyDestroyBuffer(intf, k1->isocList[n][i].isocFrame);
						k1->isocList[n][i].isocFrame = NULL;
					}
				}
			}
			kr = (*intf)->Release(intf);
		}
		if (k1->dev)
		{
			IOUSBDeviceInterface187 **dev = k1->dev;
			k1->dev = NULL;
			kr = (*dev)->USBDeviceClose(dev);
			kr = (*dev)->Release(dev);
		}
		kr = IOObjectRelease(k1->notification);
		pthread_mutex_unlock(&k1->mutex);
	}
}

void FirstMatch(void *refCon, io_iterator_t iterator)
{
	kern_return_t				kr;
	IOReturn					err;
	io_service_t				usbDeviceRef;
	io_iterator_t				interfaceIterator;
	io_service_t				usbInterfaceRef;
	K1Instance					*k1 = refCon;
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

	while (usbDeviceRef = IOIteratorNext(iterator))
	{
        printf("Soundplane A added.\n");

        kr = IOCreatePlugInInterfaceForService(usbDeviceRef, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugInInterface, &score);
        if ((kIOReturnSuccess != kr) || !plugInInterface)
        {
            show_kern_err("unable to create a device plugin", kr);
            continue;
        }
        // have device plugin, need device interface
        err = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID), (LPVOID)&dev);
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
		printf("Available Bus Power: %u mA\n", 2*powerAvailable);
		// REVIEW: GetDeviceSpeed()
#ifdef VERBOSE
		printStatus(dev, codeXmitCnt);
		printStatus(dev, codeRcvCnt1);
		printStatus(dev, codeRcvCnt2);
		printStatus(dev, codePreflight1);
		printStatus(dev, codePreflight2);
        // REVIEW: technically should check these err values
        err = (*dev)->GetDeviceVendor(dev, &vendor);
        err = (*dev)->GetDeviceProduct(dev, &product);
        err = (*dev)->GetDeviceReleaseNumber(dev, &release);
		printf("Vendor:%04X Product:%04X Release Number:", vendor, product);
		printBCD(release);
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
			err = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID192), (LPVOID)&intf);
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
#if SELECT_ISO
				err = SelectIsochronousInterface(intf, SELECT_ISO);
				if (kIOReturnSuccess == err)
#endif
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
														  GeneralInterest,		// callback
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
						fprintf(stderr, "create missing async event source\n");
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
#ifdef VERBOSE
					err = ShowInterfaceEndpoints(intf);
#endif
					err = (*intf)->GetNumEndpoints(intf, &n);
					if (kIOReturnSuccess != err)
					{
						show_io_err("could not get number of endpoints in interface", err);
						goto close;
					}
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
						// REVIEW: deal with direction, number, transferType, maxPacketSize, and interval
						if (kUSBInterrupt == transferType && kUSBIn == direction)
						{
							bzero(k1->user, MLSP_N_USER_BYTES);
							err = ScheduleInterrupt(k1);
							if (kIOReturnSuccess != err)
							{
								show_io_err("could not read pipe async", err);
								goto close;
							}
						}
					}
					for (i = 0; i < MLSP_N_ISO_ENDPOINTS; i++)
					{
						for (j = 0; j < SP_N_BUFFERS; j++)
						{
							k1->isocList[i][j].endpointNum = MLSP_EP_SURFACE1 + i;
							k1->isocList[i][j].endpointIndex = i;
							k1->isocList[i][j].index = j;
							k1->isocList[i][j].parent = k1;
							err = (*intf)->LowLatencyCreateBuffer(intf, (void **)&k1->isocList[i][j].payload, MLSP_N_ISOCH_BYTES * SP_NUM_FRAMES, kUSBLowLatencyReadBuffer);
							if (kIOReturnSuccess != err)
							{
								fprintf(stderr, "could not create read buffer #%d (%08x)\n", j, err);
								goto close;
							}
							err = (*intf)->LowLatencyCreateBuffer(intf, (void **)&k1->isocList[i][j].isocFrame, SP_NUM_FRAMES * sizeof(IOUSBLowLatencyIsocFrame), kUSBLowLatencyFrameListBuffer);
							if (kIOReturnSuccess != err)
							{
								fprintf(stderr, "could not create frame list buffer #%d (%08x)\n", j, err);
								goto close;
							}
#ifdef VERBOSE
							// NOTE: isocFrame will be initialized in ScheduleIsoch()
							printf("frame request count: %d\n", MLSP_N_ISOCH_BYTES);
#endif
						}
					}
					err = SetBusFrameNumber(k1);
					assert(!err);
					err = ScheduleIsoch(k1, &k1->isocList[0][0]);
					if (kIOReturnSuccess != err)
					{
						show_io_err("ScheduleIsoch(0.0)", err);
						goto close;
					}
					err = ScheduleIsoch(k1, &k1->isocList[1][0]);
					if (kIOReturnSuccess != err)
					{
						show_io_err("ScheduleIsoch(1.0)", err);
						goto close;
					}
					k1->displayIndex[0] = 0;
					k1->displayIndex[1] = 0;
					k1->isTerminating = NO;
					for (j = 1; j < SP_N_BUFFERS_IN_FLIGHT; j++)
					{
						for (i = 0; i < MLSP_N_ISO_ENDPOINTS; i++)
						{
							err = ScheduleIsoch(k1, &k1->isocList[i][j]);
							if (kIOReturnSuccess != err)
							{
								show_io_err("ScheduleIsoch", err);
								goto close;
							}
						}
					}
				}
#if SELECT_ISO
				else
				{
					//	err = (*intf)->USBInterfaceClose(intf);
					res = (*intf)->Release(intf);	// REVIEW: This kills the async read!!!!!
					intf = NULL;
				}
#endif
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

void pollStatus(CFRunLoopTimerRef timer, void *info)
{
#if 0
	K1Instance *k1 = info;
	printStatus(k1->dev, codeSensor1h);
	printStatus(k1->dev, codeSensor2h);
	printStatus(k1->dev, codeDeInterleave);
	printStatus(k1->dev, codeFreqDom);
	printStatus(k1->dev, codeBufferA);
	printStatus(k1->dev, codeSquare);
	printStatus(k1->dev, codeSum);
	printStatus(k1->dev, codeRoot);
#endif
}

void initialize(K1Instance *k1)
{
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
		return;
	}
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

	// REVIEW: Check out IOServiceMatching() and IOServiceNameMatching()
	kr = IOServiceAddMatchingNotification(  k1->notifyPort,
											kIOFirstMatchNotification,
											matchingDict,
											FirstMatch,
											k1, // refCon
											&k1->matchedIter);
	FirstMatch(k1, k1->matchedIter);// Iterate once to get already-present devices and
									// arm the notification
#ifdef POLL_STATUS
	{
		CFRunLoopTimerRef runLoopTimer = CFRunLoopTimerCreate(kCFAllocatorDefault, CFAbsoluteTimeGetCurrent(), 2., 0, 0, pollStatus, k1);
		CFRunLoopAddTimer(CFRunLoopGetCurrent(), runLoopTimer, kCFRunLoopCommonModes);
	}
#endif
}

void *thread(void *arg)
{
	initialize(arg);
	// Start the run loop. Now we'll receive notifications.
	CFRunLoopRun();
	return NULL;
}

void *MLSP_create(char fUseThreading, MLSP_DisplayThread displayFunc, MLSP_UserInputCallback userFunc, void *clientRef)
{
	K1Instance *k1 = calloc(1, sizeof(K1Instance));
	int err;

	assert(k1);
	err = pthread_mutex_init(&k1->mutex, NULL);
	assert(!err);
	k1->clientRef = clientRef;
	k1->userInputCallback = userFunc;
	if (SP_THREADED == fUseThreading)
	{
		pthread_attr_t attr;
		struct sched_param param;
		pthread_t pthread;

		err = pthread_attr_init(&attr);
		assert(!err);
		err = pthread_attr_getschedparam(&attr, &param);
		assert(!err);
		param.sched_priority += 10;
		err = pthread_attr_setschedparam(&attr, &param);
		assert(!err);
		// schedpolicy?
		err = pthread_create(&pthread, &attr, thread, k1);
		assert(!err);
	}
	else
		initialize(k1);
	if (displayFunc)
	{
		err = pthread_create(&k1->pthreadDisplay, NULL, displayFunc, k1);
		assert(!err);
	}
	return k1;
}

void MLSP_destroy(void *ref)
{
	K1Instance *k1 = ref;
	int err;

#ifdef VERBOSE
	fprintf(stderr, "will destroy\n");
#endif
	assert(k1);
	// Clean up here
	k1->isTerminating = 1;
	k1->terminateDisplay = 1;

	IONotificationPortDestroy(k1->notifyPort);

	if (k1->matchedIter) 
	{
        IOObjectRelease(k1->matchedIter);
        k1->matchedIter = 0;
	}
	if (k1->pthreadDisplay)
	{
		void *exitCode;

		pthread_join(k1->pthreadDisplay, &exitCode);
	}
	err = pthread_mutex_destroy(&k1->mutex);
	assert(!err);
	free(k1);
#ifdef VERBOSE
	fprintf(stderr, "did destroy\n");
#endif
}
