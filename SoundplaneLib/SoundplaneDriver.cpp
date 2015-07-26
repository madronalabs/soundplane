// SoundplaneDriver.cpp
//
// Implement utility methods for the SoundplaneDriver interface

#include "SoundplaneDriver.h"

#include <string>

#include "SoundplaneModelA.h"

float SoundplaneDriver::carrierToFrequency(int carrier)
{
    int cIdx = carrier; // offset?
    float freq = kSoundplaneASampleRate/kSoundplaneAFFTSize*cIdx;
    return freq;
}

int SoundplaneDriver::getSerialNumber()
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
