
// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __SOUNDPLANE_DRIVER__
#define __SOUNDPLANE_DRIVER__

#include <array>
#include <memory>
#include <string>

#include "SoundplaneModelA.h"

// device states
//
enum
{
	kNoDevice = 0,  // No device is connected
	kDeviceConnected = 1,  // A device has been found by the grab thread, but isochronous transfer isn't yet up
	kDeviceHasIsochSync = 2,  // The main mode, isochronous transfers have been completed.
	kDeviceClosing = 3  // destroying driver, allowing isoch transactions to finish
};

// device errors
//
enum
{
	kDevNoErr = 0,
	kDevNoNewFrame = 1,
	kDevDataDiffTooLarge = 2,
	kDevGapInSequence = 3
};

class SoundplaneDriverListener
{
public:
	virtual ~SoundplaneDriverListener() = default;
    virtual void onStartup(void) = 0;
	virtual void onFrame(const SensorFrame& frame) = 0;
    virtual void onError(int err, const char* errStr) = 0;
    virtual void onClose(void) = 0;
};

class SoundplaneDriver
{
public:
	virtual ~SoundplaneDriver() = default;

	virtual int getDeviceState() const = 0;
	
    virtual void open() = 0;
    virtual void close() = 0;

	/**
	 * Returns the firmware version of the connected Soundplane device. The
	 * return value is undefined if getDeviceState() == kNoDevice
	 */
	virtual uint16_t getFirmwareVersion() const = 0;

	/**
	 * Returns the serial number of the connected Soundplane device. The return
	 * value is undefined if getDeviceState() == kNoDevice
	 */
	virtual std::string getSerialNumberString() const = 0;

	/**
	 * Returns a pointer to the array of current carriers. The array length
	 * is kSoundplaneNumCarriers.
	 */
	virtual const unsigned char *getCarriers() const = 0;

	using Carriers = std::array<unsigned char, kSoundplaneNumCarriers>;
	/**
	 * Calls to setCarriers fail if getDeviceState() == kNoDevice
	 */
	virtual void setCarriers(const Carriers& carriers) = 0;

	/**
	 * Calls to enableCarriers fail if getDeviceState() == kNoDevice
	 */
	virtual void enableCarriers(unsigned long mask) = 0;

	/**
	 * Helper function for getting the serial number as a number rather than
	 * as a string.
	 */
	virtual int getSerialNumber() const final;

	/**
	 * Helper function for printing the carrier frequencies.
	 */
	virtual void dumpCarriers() const final;

	/**
	 * Create a SoundplaneDriver object that is appropriate for the current
	 * platform.
	 */
	static std::unique_ptr<SoundplaneDriver> create(SoundplaneDriverListener& listener);

	static float carrierToFrequency(int carrier);

};

#endif // __SOUNDPLANE_DRIVER__
