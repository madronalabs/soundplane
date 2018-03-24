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

// #define SHOW_BUS_FRAME_NUMBER
// #define SHOW_SEQUENCE_NUMBERS

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
      case kIOReturnNotReady:
        return "Not ready";
      case kIOReturnNotAttached:
        return "Unplugged";
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
mListener(m)
{
  printf("creating SoundplaneDriver...\n");
  
  for(int i=0; i < kSoundplaneNumCarriers; ++i)
  {
    mCurrentCarriers[i] = kDefaultCarriers[i];
  }
}

void MacSoundplaneDriver::start()
{
  printf("starting SoundplaneDriver...\n");
  
  // create device grab thread
  mGrabThread = std::thread(&MacSoundplaneDriver::grabThread, this);
  mGrabThread.detach();  // REVIEW: mGrabThread is leaked
  
  // create isochronous read and process thread
  mProcessThread = std::thread(&MacSoundplaneDriver::processThread, this);
  SetPriorityRealtimeAudio(mProcessThread.native_handle());
}

MacSoundplaneDriver::~MacSoundplaneDriver()
{
  kern_return_t  kr;
  
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
  
  mTerminating = true;
  destroyDevice();
  
  if (mProcessThread.joinable())
  {
    mProcessThread.join();
  }
  
  printf("frames: %d\n", mFrameCounter);
  printf("wait: %d\n", mTotalWaitCounter);
  printf("error: %d\n", mTotalErrorCounter);
  printf("gaps: %d\n", mGaps);
}


int MacSoundplaneDriver::createLowLatencyBuffers()
{
  IOReturn err;
  const int payloadSize = sizeof(SoundplaneADataPacket)*kIsochFramesPerTransaction;
  
  // create and setup transaction data structures for each buffer in each isoch endpoint
  for (int i = 0; i < kSoundplaneANumEndpoints; i++)
  {
    for (int j = 0; j < kNumIsochBuffers; j++)
    {
      K1IsocTransaction* t = getTransactionData(i, j);
      
      t->endpointIndex = i;
      t->bufIndex = j;
      t->parent = this;
      
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
  IOUSBDevRequest  request;
  
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

void prepareFrameForRequest(IOUSBLowLatencyIsocFrame& frame)
{
  frame.frStatus = 0;
  frame.frReqCount = sizeof(SoundplaneADataPacket);
  frame.frActCount = 0;
  frame.frTimeStamp.hi = 0;
  frame.frTimeStamp.lo = 0;
}

void MacSoundplaneDriver::clearPayloads(K1IsocTransaction *t)
{
  bzero(t->payloads, kPayloadBufferSize);
}

void MacSoundplaneDriver::clearAllPayloads()
{
  for(int endpointIndex=0; endpointIndex < kSoundplaneANumEndpoints; ++endpointIndex)
  {
    for(int bufferIndex = 0; bufferIndex < kNumIsochBuffers; ++bufferIndex)
    {
      K1IsocTransaction *t = getTransactionData(endpointIndex, bufferIndex);
      clearPayloads(t);
    }
  }
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
IOReturn MacSoundplaneDriver::scheduleIsoch(int endpointIndex)
{
  if(mUnplugged)
  {
    return kIOReturnNotAttached;
  }
  if ((!dev) || (!intf)) return kIOReturnNoDevice;
  
  IOReturn err = kIOReturnNoDevice;
  
  if(mDeviceState >= kDeviceConnected)
  {
    int bufferIndex = getNextBufferIndex(endpointIndex);
    K1IsocTransaction *t = getTransactionData(endpointIndex, bufferIndex);
    
    t->parent = this;
    t->busFrameNumber = mNextBusFrameNumber[endpointIndex];
    mNextBusFrameNumber[endpointIndex] = t->busFrameNumber + kIsochFramesPerTransaction;
    
    for (int frame = 0; frame < kIsochFramesPerTransaction; frame++)
    {
      prepareFrameForRequest(t->isocFrames[frame]);
    }
    
    clearPayloads(t);
    
#ifdef SHOW_BUS_FRAME_NUMBER
    int f = mTransactionsInFlight;
    fprintf(stderr, "read(%d, %p, %llu, %p, %p) [%d]\n", endpointIndex, t->payloads, t->busFrameNumber, t->isocFrames, t, f);
#endif
    
    err = (*intf)->LowLatencyReadIsochPipeAsync(intf, endpointIndex + kSoundplaneAEndpointStartIdx, t->payloads,
                                                t->busFrameNumber, kIsochFramesPerTransaction, kIsochUpdateFrequency, t->isocFrames, isochComplete, t);
    
    
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
      // when mystery error 0xe000400e is returned, one or more packets have
      // been lost from the given endpoint, after which normal
      // operation seems possible, therefore do fall through.
      
    case kIOReturnUnderrun:
      // This error is often received when all payloads are present
      // and frActCounts are as expected. So fall through to success.
      
    case kIOReturnSuccess:
      // if not shutting down, schedule another transaction and set the device
      // state to isoch sync if needed.
    {
      // we don't take the mutex to get the device state.
      // the reasoning is that we want to return quickly, and since all transfers should
      // complete within a known amount of time, on shutdown we can set the state to kDeviceClosing
      // then wait for all transfers in progress to finish.
      int state = k1->getDeviceState();
      if(state >= kDeviceConnected)
      {
        err = k1->scheduleIsoch(t->endpointIndex);
        if(state != kDeviceHasIsochSync)
        {
          // again don't take the mutex because this should only collide with another
          // attempt to set the state in really pathological situations.
          k1->setDeviceState(kDeviceHasIsochSync);
        }
      }
    }
      break;
      
    case kIOReturnIsoTooOld:
      // calling this is slow, but at this point we are hosed anyway.
      k1->resetBusFrameNumbers();
      k1->clearAllPayloads();
      break;
      
    case kIOReturnAborted:
    case kIOReturnNotAttached:
      // returned when Soundplane is unplugged
      k1->mUnplugged = true;
      break;
  }
}

// advance the buffer index to use for the next transaction at the endpoint.
// since it should be impossible for two transactions to complete at the same endpoint
// simultaneously, there is no lock.
int MacSoundplaneDriver::getNextBufferIndex(int endpoint)
{
  int i = mNextBufferIndex[endpoint];
  i++;
  i &= kIsochBuffersMask;
  mNextBufferIndex[endpoint] = i;
  return i;
}

void MacSoundplaneDriver::resetBusFrameNumbers()
{
  // we will try to schedule transactions starting this many frames in the future.
  // This doens't mean we will have this much latency, only that isoch data will not
  // be flowing for at least this many frames from now.
  const int kIsochStartupFrames = 50;
  
  // get current bus frame number from USB device
  IOReturn err;
  UInt64 frameNum;
  AbsoluteTime atTime;
  err = (*dev)->GetBusFrameNumber(dev, &frameNum, &atTime);
  if (kIOReturnSuccess != err)
    return;
  
  // set new frame # for each endpoint.
  for(int i=0; i < kSoundplaneANumEndpoints; ++i)
  {
    mNextBusFrameNumber[i] = frameNum + kIsochStartupFrames;
  }
  
  //printf("resetting bus frame: %llu @ %X%08X\n", frameNum, (int)atTime.hi, (int)atTime.lo);
}

void MacSoundplaneDriver::resetIsochTransactions()
{
  IOReturn err;
  
  // reset transaction numbers
  mTransactionsInFlight = 0;
  for (int i = 0; i < kSoundplaneANumEndpoints; i++)
  {
    mNextBufferIndex[i] = 0;
  }
  
  resetBusFrameNumbers();
  
  // for each endpoint, schedule first transaction and
  // a few buffers into the future
  for (int j = 0; j < kIsochBuffersInFlight; j++)
  {
    for (int i = 0; i < kSoundplaneANumEndpoints; i++)
    {
      err = scheduleIsoch(i);
      
#if DEBUG
      if (kIOReturnSuccess != err)
      {
        show_io_err("scheduleIsoch", err);
      }
#endif
    }
  }
}

// --------------------------------------------------------------------------------
#pragma mark transfer utilities

uint16_t MacSoundplaneDriver::getTransferBytesReceived(int endpoint, int buffer, int frame)
{
  if(getDeviceState() < kDeviceConnected) return 0;
  uint16_t b = 0;
  K1IsocTransaction* t = getTransactionData(endpoint, buffer);
  
  if (t)
  {
    IOUSBLowLatencyIsocFrame* pf = &t->isocFrames[frame];
    b = pf->frActCount;
  }
  return b;
}

unsigned char MacSoundplaneDriver::getPayloadLastByte(int endpoint, int buffer, int frame)
{
  if(getDeviceState() < kDeviceConnected) return 0;
  unsigned char* payloadPtr = 0;
  unsigned char r = 0;
  
  K1IsocTransaction* t = getTransactionData(endpoint, buffer);
  if (t && t->payloads)
  {
    payloadPtr = t->payloads + frame*sizeof(SoundplaneADataPacket);
    unsigned char* lastBytePtr = payloadPtr + kSoundplaneAPackedDataSize - 1;
    r = *lastBytePtr;
  }
  return r;
}


// return true if something was received in the frame given by the parameters.
// Looking at the last byte of the payload appears to offer lower latency than using frActCount,
// because frActCounts are not written until the entire transaction buffer is complete.
bool MacSoundplaneDriver::frameWasReceived(int endpoint, int buffer, int frame)
{
  if(mUnplugged) return false;
  return (getPayloadLastByte(endpoint, buffer, frame) > 0);
  //   return (getTransferBytesReceived(endpoint, buffer, frame) > 0);
}

AbsoluteTime MacSoundplaneDriver::getTransferTimeStamp(int endpoint, int buffer, int frame)
{
  if(mUnplugged) return AbsoluteTime();
  if(getDeviceState() < kDeviceConnected) return AbsoluteTime();
  AbsoluteTime b;
  K1IsocTransaction* t = getTransactionData(endpoint, buffer);
  
  if (t)
  {
    IOUSBLowLatencyIsocFrame* pf = &t->isocFrames[frame];
    b = pf->frTimeStamp;
  }
  return b;
}

IOReturn MacSoundplaneDriver::getTransferStatus(int endpoint, int buffer, int frame)
{
  if(mUnplugged) return kIOReturnNoDevice;
  if(getDeviceState() < kDeviceConnected) return kIOReturnNoDevice;
  IOReturn b = kIOReturnSuccess;
  K1IsocTransaction* t = getTransactionData(endpoint, buffer);
  
  if (t)
  {
    IOUSBLowLatencyIsocFrame* pf = &t->isocFrames[frame];
    b = pf->frStatus;
  }
  return b;
}

uint16_t MacSoundplaneDriver::getSequenceNumber(int endpoint, int buffer, int frame)
{
  if(mUnplugged) return 0;
  uint16_t s = 0;
  K1IsocTransaction* t = getTransactionData(endpoint, buffer);
  
  if (t && t->payloads)
  {
    SoundplaneADataPacket* p = (SoundplaneADataPacket*)t->payloads;
    s = p[frame].seqNum;
  }
  return s;
}

unsigned char* MacSoundplaneDriver::getPayloadPtr(int endpoint, int buffer, int frame)
{
  if(mUnplugged) return 0;
  unsigned char* p = 0;
  
  if(getDeviceState() < kDeviceConnected) return 0;
  
  K1IsocTransaction* t = getTransactionData(endpoint, buffer);
  if (t && t->payloads)
  {
    p = t->payloads + frame*sizeof(SoundplaneADataPacket);
  }
  return p;
}

MacSoundplaneDriver::FramePosition MacSoundplaneDriver::getPositionOfSequence(int endpoint, uint16_t seq)
{
  if(mUnplugged) return FramePosition{};
  const int totalFrames = kIsochFramesPerTransaction*kNumIsochBuffers;
  FramePosition pos{};
  for(int frame = 0; frame < totalFrames; ++frame)
  {
    if(frameWasReceived(endpoint, pos.buffer, pos.frame))
    {
      int seqNum = getSequenceNumber(endpoint, pos.buffer, pos.frame);
      if(seqNum == seq)
      {
        break;
      }
    }
    pos = advance(pos);
  }
  return pos;
}

// --------------------------------------------------------------------------------
#pragma mark device utilities

namespace {
  
  IOReturn ConfigureDevice(IOUSBDeviceInterface187 **dev)
  {
    uint8_t              numConf;
    IOReturn            err;
    IOUSBConfigurationDescriptorPtr  confDesc;
    
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
    IOReturn  err;
    
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

// deviceAdded() is called by the callback set up in the grab thread when a new Soundplane device is found.
//
void MacSoundplaneDriver::deviceAdded(void *refCon, io_iterator_t iterator)
{
  MacSoundplaneDriver      *k1 = static_cast<MacSoundplaneDriver *>(refCon);
  kern_return_t        kr;
  IOReturn          err;
  io_service_t        usbDeviceRef;
  io_iterator_t        interfaceIterator;
  io_service_t        usbInterfaceRef;
  IOCFPlugInInterface      **plugInInterface = NULL;
  IOUSBDeviceInterface187    **dev = NULL;  // 182, 187, 197
  
  IOUSBInterfaceInterface192  **intf; // ?!
  
  IOUSBFindInterfaceRequest  req;
  ULONG            res;
  SInt32            score;
  UInt32            powerAvailable;
#ifdef VERBOSE
  uint16_t          vendor;
  uint16_t          product;
  uint16_t          release;
#endif
  int              i;
  uint8_t            n;
  
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
          kr = IOServiceAddInterestNotification(k1->notifyPort,    // notifyPort
                                                usbDeviceRef,      // service
                                                kIOGeneralInterest,  // interestType
                                                deviceNotifyGeneral,  // callback
                                                k1,          // refCon
                                                &(k1->notification)  // notification
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
            uint8_t    direction;
            uint8_t    number;
            uint8_t    transferType;
            uint16_t  maxPacketSize;
            uint8_t    interval;
            
            err = (*intf)->GetPipeProperties(intf, i, &direction, &number, &transferType, &maxPacketSize, &interval);
            if (kIOReturnSuccess != err)
            {
              fprintf(stderr, "endpoint %d - could not get endpoint properties (%08x)\n", i, err);
              goto close;
            }
          }
          
          if(!k1->createLowLatencyBuffers())
          {
            goto close;
          }
          
          {
            std::lock_guard<std::mutex> lock(k1->getDeviceStateMutex());
            k1->setDeviceState(kDeviceConnected);
          }
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
    k1->mUnplugged = false;
    k1->mListener.onStartup();
    k1->resetIsochTransactions();
  }
}

// set the unplugged flag if device is unplugged. leave low latency buffers in place.
//
void MacSoundplaneDriver::deviceNotifyGeneral(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
  MacSoundplaneDriver *k1 = static_cast<MacSoundplaneDriver *>(refCon);
  if (kIOMessageServiceIsTerminated == messageType)
  {
    k1->mUnplugged = true;
  }
}

// This thread is responsible for finding and adding USB devices matching the Soundplane.
// Execution is controlled by a Core Foundation (Cocoa) run loop.
//
void MacSoundplaneDriver::grabThread()
{
  bool OK = true;
  kern_return_t        kr;
  CFMutableDictionaryRef    matchingDict;
  CFRunLoopSourceRef      runLoopSource;
  CFNumberRef          numberRef;
  SInt32            usbVendor = kSoundplaneUSBVendor;
  SInt32            usbProduct = kSoundplaneUSBProduct;
  
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
  kern_return_t  kr;
  {
    std::lock_guard<std::mutex> lock(mDeviceStateMutex);
    setDeviceState(kDeviceClosing);
    
    // wait for any pending transactions to finish
    int totalWaitTime = 0;
    int waitTimeMicrosecs = 10*1000;
    while (mTransactionsInFlight > 0)
    {
      int t = mTransactionsInFlight;
      usleep(waitTimeMicrosecs);
      totalWaitTime += waitTimeMicrosecs;
      
      // waiting too long-- bail without cleaning up buffers
      if (totalWaitTime > 1000*1000)
      {
        printf("WARNING: Soundplane driver could not finish pending transactions: %d remaining\n", t);
        intf = NULL;
        break;
      }
    }
    
    // wait some more
    usleep(100*1000);
    setDeviceState(kNoDevice);
  }
  
  // notify listener
  mListener.onClose();
  
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
    kr = (*dev)->USBDeviceClose(dev);
    kr = (*dev)->Release(dev);
    dev = NULL;
  }
}

uint16_t wrapDistance(uint16_t a, uint16_t b)
{
  uint16_t c = a - b;
  uint16_t d = b - a;
  return std::min(c, d);
}

uint16_t MacSoundplaneDriver::mostRecentSequenceNum(int endpoint)
{
  if(mUnplugged) return 0;
  
  const int maxUInt16 = 0xFFFF;
  const int totalFrames = kIsochFramesPerTransaction*kNumIsochBuffers;
  const int slack = 2; // allows for gaps in sequence
  
  // scan for wrap
  bool wrapHi = false;
  bool wrapLo = false;
  FramePosition pos{};
  for(int frame = 0; frame < totalFrames; ++frame)
  {
    if(frameWasReceived(endpoint, pos.buffer, pos.frame))
    {
      int seqNum = getSequenceNumber(endpoint, pos.buffer, pos.frame);
      if(seqNum > maxUInt16 - totalFrames*slack)
      {
        wrapHi = true;
      }
      else if(seqNum < totalFrames*slack)
      {
        wrapLo = true;
      }
      else //if((seqNum > totalFrames*slack) && (seqNum < maxUInt16 - totalFrames*slack))
      {
        // no wrap is possible if we have sequence numbers in this range
        break;
      }
    }
    pos = advance(pos);
  }
  bool wrap = wrapHi && wrapLo;
  
  uint16_t mostRecentSeqNum = 0;
  if(!wrap)
  {
    FramePosition pos{};
    for(int frame = 0; frame < totalFrames; ++frame)
    {
      if(frameWasReceived(endpoint, pos.buffer, pos.frame))
      {
        int seqNum = getSequenceNumber(endpoint, pos.buffer, pos.frame);
        if(seqNum > mostRecentSeqNum)
        {
          mostRecentSeqNum = seqNum;
        }
      }
      pos = advance(pos);
    }
  }
  else
  {
    FramePosition pos{};
    for(int frame = 0; frame < totalFrames; ++frame)
    {
      if(frameWasReceived(endpoint, pos.buffer, pos.frame))
      {
        uint16_t seqNum = getSequenceNumber(endpoint, pos.buffer, pos.frame);
        
        // we know there is a wrap, so the ost recent seq num will necessarily be below totalFrames*slack
        if(seqNum < totalFrames*slack)
        {
          if(seqNum > mostRecentSeqNum)
          {
            mostRecentSeqNum = seqNum;
          }
        }
      }
      pos = advance(pos);
    }
  }
  
  return mostRecentSeqNum;
}

bool MacSoundplaneDriver::endpointDataHasSequence(int endpoint, uint16_t seq)
{
  if(mUnplugged) return false;
  
  const int totalFrames = kIsochFramesPerTransaction*kNumIsochBuffers;
  FramePosition pos{};
  for(int frame = 0; frame < totalFrames; ++frame)
  {
    if(frameWasReceived(endpoint, pos.buffer, pos.frame))
    {
      uint16_t seqNum = getSequenceNumber(endpoint, pos.buffer, pos.frame);
      if(seqNum == seq)
      {
        return true;
      }
    }
    pos = advance(pos);
  }
  return false;
}

bool MacSoundplaneDriver::sequenceIsComplete(uint16_t seq)
{
  if(mUnplugged) return false;
  
  int endpointsMatching = 0;
  for(int endpoint = 0; endpoint < kSoundplaneANumEndpoints; ++endpoint)
  {
    if(endpointDataHasSequence(endpoint, seq))
    {
      endpointsMatching++;
    }
  }
  return endpointsMatching == kSoundplaneANumEndpoints;
}

int MacSoundplaneDriver::mostRecentCompleteSequence()
{
  if(mUnplugged) return -1;
  
  const int kMaxDifferenceBetweenEndpoints = 4;
  uint16_t mostRecent = mostRecentSequenceNum(0);
  uint16_t seq = mostRecent;
  int d = 0;
  for(; d < kMaxDifferenceBetweenEndpoints; ++d)
  {
    if(sequenceIsComplete(seq))
    {
      return seq;
    }
    
    seq--; // wraps
  }
  
  return -1;
}

void MacSoundplaneDriver::printTransactions()
{
  for(int endpointIdx = 0; endpointIdx < kSoundplaneANumEndpoints; ++endpointIdx)
  {
    std::cout << "\nendpoint " << endpointIdx << ": \n" ;
    for(int bufIdx = 0; bufIdx < kNumIsochBuffers; ++bufIdx)
    {
      std::cout << "    buffer " << bufIdx << ": " ;
      for(int frameIdx=0; frameIdx<kIsochFramesPerTransaction; ++frameIdx)
      {
        int frameSeqNum = getSequenceNumber(endpointIdx, bufIdx, frameIdx);
        //  uint8_t actCount = getTransferBytesReceived(endpointIdx, bufIdx, frameIdx);
        uint8_t lastByte = getPayloadLastByte(endpointIdx, bufIdx, frameIdx);
        printf("%05d[%02x]", frameSeqNum, lastByte);
      }
      std::cout << "\n";
    }
  }
}

// This thread is responsible for collecting all data from the Soundplane. The routines isochComplete()
// and scheduleIsoch() combine to keep data running into the transfer buffers in round-robin fashion.
// This thread looks at those buffers and attempts to stay in sync with incoming data, watching as
// transfers are filled in with successive sequence numbers.
//
void MacSoundplaneDriver::process()
{
  if(mUnplugged) return;
  int mostRecentSeq = mostRecentCompleteSequence();
  
  if(mostRecentSeq < 0)
  {
    mErrorCounter++;
    mTotalErrorCounter++;
  }
  else if(mostRecentSeq != mPreviousSeq)
  {
    mWaitCounter = 0;
    mErrorCounter = 0;
    
    uint16_t nextSeq = mPreviousSeq;
    nextSeq++; // wraps
    
    while(nextSeq != mostRecentSeq)
    {
      // look for the next complete sequence in between previous and most recent.
      if(sequenceIsComplete(nextSeq))
      {
        break;
      }
      else
      {
        nextSeq++; // wraps
      }
    }
    
    // make frame
    std::array<unsigned char *, kSoundplaneANumEndpoints> payloads;
    bool payloadsOK = true;
    for(int i=0 ; i<kSoundplaneANumEndpoints; ++i)
    {
      FramePosition pos = getPositionOfSequence(i, nextSeq);
      
      payloads[i] = getPayloadPtr(i, pos.buffer, pos.frame);
      if(!payloads[i])
      {
        payloadsOK = false;
        // diff too large, alert client
        snprintf(mErrorBuf, kMaxErrorStringSize, "(%d)", nextSeq);
        mListener.onError(kDevPayloadFailed, mErrorBuf);
        break;
      }
    }
    
    if (payloadsOK)
    {
      if(mStartupCtr >= kIsochStartupFrames)
      {
        // assemble endpoints into frame
        // for two endpoints only
        K1_unpack_float2(payloads[0], payloads[1], mWorkingFrame);
        K1_clear_edges(mWorkingFrame);
        
        bool firstFrame = (mStartupCtr == kIsochStartupFrames);
        float diff = frameDiff(mWorkingFrame, mPrevFrame);
        if((diff < kMaxFrameDiff) || firstFrame)
        {
          // new frame is OK, add sequence # and call client callback
          // TODO mWorkingFrame.seqNum = nextSeq;
          
          mListener.onFrame(mWorkingFrame);
        }
        else
        {
          // diff too large, alert client
          snprintf(mErrorBuf, kMaxErrorStringSize, "(%f)", diff);
          mListener.onError(kDevDataDiffTooLarge, mErrorBuf);
        }
        mPrevFrame = mWorkingFrame;
      }
      else
      {
        // wait for startup
        mStartupCtr++;
      }
    }
    
    if(nextSeq != mPreviousSeq + 1)
    {
      uint16_t gapSize = wrapDistance(mPreviousSeq, nextSeq);
      if(gapSize > 1)
      {
        snprintf(mErrorBuf, kMaxErrorStringSize, "%d -> %d (%d)", mPreviousSeq, nextSeq, gapSize);
        mListener.onError(kDevGapInSequence, mErrorBuf);
      }
      mGaps++;
    }
    mFrameCounter++;
    
    mPreviousSeq = nextSeq;
  }
  else
  {
    // waiting
    mWaitCounter++;
    mTotalWaitCounter++;
  }
}

// If we are waiting too long or seeing too many errors, reset the isoch layer.
//
void MacSoundplaneDriver::resetIsochIfStalled()
{
  const int kMaxWait = 16;
  const int kMaxError = 16;
  
  if((mWaitCounter > kMaxWait) || (mErrorCounter > kMaxError))
  {
    // wait for transactions to finish
    const int waitTimeMicros = kSoundplaneFrameIntervalMicros*kNumIsochBuffers*kIsochFramesPerTransaction;
    usleep(waitTimeMicros);
    
    resetIsochTransactions();
    
    snprintf(mErrorBuf, kMaxErrorStringSize,"at bus frame: %llu ", mNextBusFrameNumber[0]);
    mListener.onError(kDevReset, mErrorBuf);
    
    mWaitCounter = 0;
    mErrorCounter = 0;
  }
}

void MacSoundplaneDriver::processThread()
{
  while(!mTerminating)
  {
    if(mDeviceState >= kDeviceConnected)
    {
      if(!mUnplugged)
      {
        resetIsochIfStalled();
        process();
      }
#ifdef SHOW_SEQUENCE_NUMBERS
      mTestCtr++;
      if(mTestCtr > 4000)
      {
        printTransactions();
        std::cout << "wait: " <<mWaitCounter << ", error: " <<mErrorCounter << "\n";
        mTestCtr = 0;
      }
#endif
    }
    
    usleep(250); // 0.25 ms
  }
}

// -------------------------------------------------------------------------------
#pragma mark transfer utilities

int MacSoundplaneDriver::getStringDescriptor(IOUSBDeviceInterface187 **dev, uint8_t descIndex, char *destBuf, uint16_t maxLen, UInt16 lang)
{
  IOUSBDevRequest req;
  uint8_t     desc[256]; // Max possible descriptor length
  uint8_t     desc2[256]; // Max possible descriptor length
  int stringLen;
  IOReturn err;
  if (lang == 0) // set default langID
    lang=0x0409;
  
  bzero(&req, sizeof(req));
  req.bmRequestType = USBmakebmRequestType(kUSBIn, kUSBStandard, kUSBDevice);
  req.bRequest = kUSBRqGetDescriptor;
  req.wValue = (kUSBStringDesc << 8) | descIndex;
  req.wIndex = lang;  // English
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
  req.wIndex = lang;  // English
  req.wLength = stringLen;
  req.pData = desc2;
  
  (err = (*dev)->DeviceRequest(dev, &req));
  if ( err ) return -1;
  
  // copy to output buffer
  size_t destLen = std::min((int)req.wLenDone, (int)maxLen);
  std::copy((const char *)desc2, (const char *)desc2 + destLen, destBuf);
  
  return static_cast<int>(destLen);
}


int MacSoundplaneDriver::getSerialNumber() const
{
  const auto state = getDeviceState();
  if (state == kDeviceConnected || state == kDeviceHasIsochSync)
  {
    const auto serialString = getSerialNumberString();
    try
    {
      return stoi(serialString);
    }
    catch (...)
    {
      return 0;
    }
  }
  return 0;
}

