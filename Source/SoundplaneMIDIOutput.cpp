
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneMIDIOutput.h"

const std::string kSoundplaneMIDIDeviceName("Soundplane IAC out");

#define INVALID_NOTE -1
#define MIDI_MPE_MODE_CC 127

// --------------------------------------------------------------------------------
#pragma mark MIDIVoice

MIDIVoice::MIDIVoice() :
	age(0), x(0), y(0), z(0), note(0),
	startX(0), startY(0), startNote(0), vibrato(0),
    mMIDINote(-1), mMIDIVel(0), mMIDIBend(0), mMIDIXCtrl(0), mMIDIYCtrl(0), mMIDIPressure(0),
    mState(kVoiceStateInactive)
{
}

MIDIVoice::~MIDIVoice()
{
}

// --------------------------------------------------------------------------------
#pragma mark MIDIDevice

MIDIDevice::MIDIDevice(const std::string& name, int index = -1) :
	mName(name), 
	mIndex(index)
{
	// if we don't give the device an index, it uses the interapp driver
	mIsInternal = (index < 0);
}

MIDIDevice::~MIDIDevice()
{
}

juce::MidiOutput* MIDIDevice::open()
{
	return 0;
}

void MIDIDevice::close()
{
	
}

// return MidiOutput ptr, to be disposed of by the caller.
//
juce::MidiOutput* MIDIDevice::getDevice()
{
	juce::MidiOutput* ret = nullptr;
	close();
	
	if(mIsInternal)
	{
		ret = juce::MidiOutput::createNewDevice (kSoundplaneMIDIDeviceName.c_str());
	}
	else
	{
		ret = juce::MidiOutput::openDevice(mIndex);
	}
	return ret;
}


// --------------------------------------------------------------------------------
#pragma mark SoundplaneMIDIOutput

SoundplaneMIDIOutput::SoundplaneMIDIOutput() :
	mpCurrentDevice(0),
	mPressureActive(false),
	mDataFreq(250.),
	mLastTimeDataWasSent(0),
	mLastTimeNRPNWasSent(0),
    mGotNoteChangesThisFrame(false),
	mBendRange(36),
	mTranspose(0),
	mHysteresis(0.5f),
	mMPEExtended(false),
	mMPEMode(true),
	mMPEChannels(0),
	mChannel(1),
	mKymaPoll(true)
{
	findMIDIDevices();
}

SoundplaneMIDIOutput::~SoundplaneMIDIOutput()
{
	if(mpCurrentDevice)
	{
		delete mpCurrentDevice;
	}
}

void SoundplaneMIDIOutput::initialize()
{
}

void SoundplaneMIDIOutput::findMIDIDevices ()
{
	mDevices.clear();
	mDeviceList.clear();
	
	// this will create IAC device using JUCE
	mDevices.push_back(MIDIDevicePtr(new MIDIDevice(kSoundplaneMIDIDeviceName)));
	mDeviceList.push_back(kSoundplaneMIDIDeviceName);	
	
	// get hardware device indices using JUCE for now. 
	// don't open the devices.
	juce::StringArray devices = juce::MidiOutput::getDevices();
	int s = devices.size();
	for(int i=0; i<s; ++i)
	{
		std::string nameStr(devices[i].toUTF8());
		mDevices.push_back(MIDIDevicePtr(new MIDIDevice(nameStr, i)));
		mDeviceList.push_back(nameStr);
	}
}

void SoundplaneMIDIOutput::setDevice(int deviceIdx)
{
	if(mpCurrentDevice)
	{
		delete mpCurrentDevice;
		mpCurrentDevice = 0;
	}

	if(deviceIdx < mDevices.size())
	{
		mpCurrentDevice = mDevices[deviceIdx]->getDevice();
		sendMPEChannels();
		sendPitchbendRange();
	}
}

void SoundplaneMIDIOutput::setDevice(const std::string& deviceStr)
{
	if(mpCurrentDevice)
	{
		delete mpCurrentDevice;
		mpCurrentDevice = 0;
	}
	
	for(int i=0; i<mDevices.size(); ++i)
	{
		if (mDevices[i]->getName() == deviceStr)
		{
			mpCurrentDevice = mDevices[i]->getDevice();
			sendMPEChannels();
			sendPitchbendRange();
		}
	}
}

int SoundplaneMIDIOutput::getNumDevices()
{
	return mDevices.size();
}

const std::string& SoundplaneMIDIOutput::getDeviceName(int d)
{
	return mDevices[d]->getName();
}

const std::vector<std::string>& SoundplaneMIDIOutput::getDeviceList()
{
	return mDeviceList;
}

void SoundplaneMIDIOutput::setActive(bool v) 
{ 
	mActive = v; 
}

void SoundplaneMIDIOutput::setPressureActive(bool v) 
{ 
    if(mMPEMode) return; // not used for MPE mode
    if(mPressureActive==v) return;
    
	// when turning pressure off, first send maximum values
	// so sounds don't get stuck off
    if( mpCurrentDevice && mPressureActive)
    {
        for(int c=1; c<=16; ++c)
        {
            sendPressure(c, INVALID_NOTE, 127);
        }
    }
    
	mPressureActive = v;

    // when activating pressure, initialise to zero
    if( mpCurrentDevice && mPressureActive)
    {
        for(int c=1; c<=16; ++c)
        {
            sendPressure(c, INVALID_NOTE, 0);
        }
    }
}

void SoundplaneMIDIOutput::setMPEExtended(bool v)
{
	// if setMPEExtended is set, we can count on being in MPE mode
	if(!mMPEMode)
	{
		setMPE(true);
	}
	
	mMPEExtended = v;
	
    if (!mpCurrentDevice) return;
    
	for(int i=1; i<=16; ++i)
	{
        // just in case something is using 11/75
		sendPressure(i, INVALID_NOTE, 0);
	}
}

void SoundplaneMIDIOutput::setMPE(bool v)
{
	mMPEMode = v;
	
	// channels is always 15 now if we are in MPE mode. If we introduce splits or more complex MPE options this may change.
	mMPEChannels = mMPEMode ? 15 : 0;
	
    if (!mpCurrentDevice) return;

	for(int i=1; i<=16; ++i)
	{
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::allNotesOff(i));
        sendPressure(i, INVALID_NOTE,0);
	}
	
	sendMPEChannels();
	sendPitchbendRange();

}

void SoundplaneMIDIOutput::setStartChannel(int v)
{
	if(mChannel == v) return;
	mChannel = v;
	if (!mpCurrentDevice) return;
	for(int i=1; i<=16; ++i)
	{
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::allNotesOff(i));
	}
}

// MPE spec defines a split mode using main channels 1 and 16. We ignore this for now and use only channel 1
// for the main channel, and 2 upwards for the individual voices.
int SoundplaneMIDIOutput::getMPEMainChannel()
{
	return 1;
}

int SoundplaneMIDIOutput::getMPEVoiceChannel(int voice)
{
	return 2 + clamp(voice, 0, 14);
}

void SoundplaneMIDIOutput::processSoundplaneMessage(const SoundplaneDataMessage* msg)
{
    static const MLSymbol startFrameSym("start_frame");
    static const MLSymbol touchSym("touch");
    static const MLSymbol onSym("on");
    static const MLSymbol continueSym("continue");
    static const MLSymbol offSym("off");
    static const MLSymbol controllerSym("controller");
    static const MLSymbol xSym("x");
    static const MLSymbol ySym("y");
    static const MLSymbol xySym("xy");
    static const MLSymbol xyzSym("xyz");
    static const MLSymbol zSym("z");
    static const MLSymbol toggleSym("toggle");
    static const MLSymbol endFrameSym("end_frame");
    static const MLSymbol matrixSym("matrix");
    static const MLSymbol nullSym;
    
	if (!mActive) return;
	if (!mpCurrentDevice) return;
    MLSymbol type = msg->mType;
    MLSymbol subtype = msg->mSubtype;
    
    int i;
	float x, y, z, dz, note, vibrato;
    
    if(type == startFrameSym)
    {
        const UInt64 dataPeriodMicrosecs = 1000*1000 / mDataFreq;
        mCurrFrameStartTime = getMicroseconds();
        if (mCurrFrameStartTime > mLastFrameStartTime + (UInt64)dataPeriodMicrosecs)
        {
            mLastFrameStartTime = mCurrFrameStartTime;
            mTimeToSendNewFrame = true;
        }
        else
        {
            mTimeToSendNewFrame = false;
        }
        mGotNoteChangesThisFrame = false;
    }
    else if(type == touchSym)
    {
        // get touch data
        i = msg->mData[0];
        x = msg->mData[1];
        y = msg->mData[2];
        z = msg->mData[3];
        dz = msg->mData[4];
		note = msg->mData[5];
		vibrato = msg->mData[6];
        
        MIDIVoice* pVoice = &mMIDIVoices[i];
        pVoice->x = x;
        pVoice->y = y;
        pVoice->z = z;
        pVoice->dz = dz;
		pVoice->note = note;
		pVoice->vibrato = vibrato;
               
        if(subtype == onSym)
        {
            pVoice->startX = x;
            pVoice->startY = y;
            pVoice->startNote = note;
            pVoice->mState = kVoiceStateOn;
            pVoice->age = 1;
            mGotNoteChangesThisFrame = true;
        }
        else if(subtype == continueSym)
        {
            pVoice->mState = kVoiceStateActive;
            pVoice->age++;
        }
        else if(subtype == offSym)
        {
            pVoice->mState = kVoiceStateOff;
            pVoice->age = 0;
            pVoice->z = 0;
            mGotNoteChangesThisFrame = true;
        }
        //debug() << i << " " << note << "\n";
    }
    else if(type == controllerSym)
    {
        // when a controller message comes in, make a local copy of the message and store by zone ID.
        int zoneID = msg->mData[0];
        mMessagesByZone[zoneID] = *msg;
    }
    else if(type == matrixSym)
    {
        // nothing to do
    }
    else if(type == endFrameSym)
    {
        // get most recent active voice played
        unsigned int minAge = (unsigned int)-1;
        int newestVoiceIdx = -1;
        for(int i=0; i<mVoices; ++i)
        {
            MIDIVoice* pVoice = &mMIDIVoices[i];
            int a = pVoice->age;
            if(a > 0)
            {
                if(a < minAge)
                {
                    minAge = a;
                    newestVoiceIdx = i;
                }
            }
        }
        
        if(mGotNoteChangesThisFrame || mTimeToSendNewFrame)
        {            
            // for each zone, send and clear any controller messages received since last frame
            for(int i=0; i<kSoundplaneAMaxZones; ++i)
            {
                SoundplaneDataMessage* pMsg = &(mMessagesByZone[i]);
                if(pMsg->mType == controllerSym)
                {
                    // message data:
                    // mZoneID, mChannel, mControllerNumber, mControllerNumber2, mControllerNumber3, x, y, z
                    //
                    int zoneChan = pMsg->mData[1];
                    int ctrlNum1 = pMsg->mData[2];
                    int ctrlNum2 = pMsg->mData[3];
                    int ctrlNum3 = pMsg->mData[4];
                    x = pMsg->mData[5];
                    y = pMsg->mData[6];
                    z = pMsg->mData[7];
                    
                    int ix = clamp((int)(x*128.f), 0, 127);
                    int iy = clamp((int)(y*128.f), 0, 127);
                    int iz = clamp((int)(z*128.f), 0, 127);
                    
                    // use channel from zone, or default to channel dial setting.
                    int channel = (zoneChan > 0) ? (zoneChan) : (mChannel);
                    
                    // get control data by type and send controller message
                    if(pMsg->mSubtype == xSym)
                    {
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum1, ix));
                    }
                    else if(pMsg->mSubtype == ySym)
                    {
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum1, iy));
                    }
                    else if (pMsg->mSubtype == xySym)
                    {
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum1, ix));
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum2, iy));
                    }
                    else if (pMsg->mSubtype == xyzSym)
                    {
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum1, ix));
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum2, iy));
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum3, iz));
                    }
                    else if (pMsg->mSubtype == zSym)
                    {
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum1, iz));
                    }
                    else if (pMsg->mSubtype == toggleSym)
                    {
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, ctrlNum1, ix));
                    }
                    
                    // clear controller message
                    mMessagesByZone[i].mType = nullSym;
                }
            }

            // send MIDI notes and controllers for each live touch.
            // attempt to translate the notes into MIDI notes + pitch bend.
            for(int i=0; i < mVoices; ++i)
            {
                MIDIVoice* pVoice = &mMIDIVoices[i];
                int chan = (mMPEMode) ? (getMPEVoiceChannel(i)) : mChannel;
				
                if (pVoice->mState == kVoiceStateOn)
                {
                    // before note on, first send note off if needed
                    if((pVoice->mMIDINote >= 0) && (pVoice->mMIDIVel > 0))
                    {
                        sendPressure(chan, pVoice->mMIDINote, 0);
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOff(chan, pVoice->mMIDINote));
                    }
                    
                    // get nearest integer note
                    pVoice->mMIDINote = clamp((int)lround(pVoice->note) + mTranspose, 1, 127);
                    
                    float fVel = pVoice->dz*100.f*128.f;
                    pVoice->mMIDIVel = clamp((int)fVel, 16, 127);

					// debug() << "voice " << i << " ON: DZ = " << pVoice->dz << " P: " << pVoice->mMIDINote << ", V " << pVoice->mMIDIVel << "\n";
                    
                    // reset pitch at note on!
                    sendPitchbend(chan, 8192);
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOn(chan, pVoice->mMIDINote, (unsigned char)pVoice->mMIDIVel));
                    
                    pVoice->mState = kVoiceStateActive;
                }
                else if(pVoice->mState == kVoiceStateOff && pVoice->mMIDIVel)
                {
                    // send pressure off
                    sendPressure(chan, pVoice->mMIDINote, 0);
                    pVoice->mMIDIPressure = 0;
                    
                    // send note off
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOff(chan, pVoice->mMIDINote));
                    pVoice->mMIDIVel = 0;
                    pVoice->mMIDINote = 0;
                    pVoice->mState = kVoiceStateInactive;
                    
					//debug() << "voice " << i << " OFF: DZ = " << pVoice->dz << " P: " << pVoice->mMIDINote << ", V " << pVoice->mMIDIVel << "\n";                    
                }
                
                // if note is on, send pitch bend / controllers.
                if (pVoice->mMIDIVel > 0)
                {
                    int newMIDINote = clamp((int)lround(pVoice->note) + mTranspose, 1, 127);
                    
                    // retrigger notes for glissando mode when sliding from key to key
                    if ((newMIDINote != pVoice->mMIDINote) && mGlissando)
                    {
                        // get retrigger velocity from current z
                        float fVel = pVoice->z*128.f;
                        pVoice->mMIDIVel = clamp((int)fVel, 16, 127);
                        
                        // retrigger
                        sendPressure(chan, pVoice->mMIDINote, 0);
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOff(chan, pVoice->mMIDINote));
                        pVoice->mMIDINote = newMIDINote;
                        mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOn(chan, pVoice->mMIDINote, (unsigned char)pVoice->mMIDIVel));
                        sendPressure(chan, pVoice->mMIDINote, pVoice->z);
                        
						//debug() << "voice " << i << " RETRIG: Z = " << pVoice->z << " P: " << pVoice->mMIDINote << ", V " << pVoice->mMIDIVel << "\n";
                        
                    }
                    
                    // send controllers etc. if in MPE mode, or if this is the youngest voice.
                    if((newestVoiceIdx == i) || mMPEMode)
                    {
                        // get pitch bend for note difference
                        //
//						float fp = mGlissando ? 0 : (pVoice->note - pVoice->startNote);
						float bendAmount;
						if(mGlissando)
						{
							bendAmount = pVoice->vibrato;
						}
						else
						{
							bendAmount = pVoice->note - pVoice->startNote + pVoice->vibrato;	
						}
						
                        if (mBendRange > 0)
                        {
                            bendAmount *= 8192.f;
                            bendAmount /= (float)mBendRange;
                        }
                        else 
                        {
                            bendAmount = 0.;
                        }
                        bendAmount += 8192.f;
                        
                        int ip = bendAmount;
                        ip = clamp(ip, 0, 16383);
                        if(ip != pVoice->mMIDIBend)
                        {                            
      //  debug() << "voice " << i << " BEND = " << ip << "\n";                            
                            sendPitchbend(chan,ip);
                            pVoice->mMIDIBend = ip;
                        }
                        
                        // send voice absolute x, y, z values to controllers 73, 74, 75
                        int ix = clamp((int)(pVoice->x*127.f), 0, 127);
                        if(ix != pVoice->mMIDIXCtrl)
                        {                            
                            //debug() << "channel " << chan << ", x = " << ix << "\n";
                            sendX(chan,ix);
                            pVoice->mMIDIXCtrl = ix;
                        }
                        
                        int iy = clamp((int)(pVoice->y*127.f), 0, 127);
                        if(iy != pVoice->mMIDIYCtrl)
                        {
                            sendY(chan,iy);
                            pVoice->mMIDIYCtrl = iy;
                        }
                        
                        int iz = clamp((int)(pVoice->z*127.f), 0, 127);
                        if(iz != pVoice->mMIDIPressure)
                        {  
                            sendPressure(chan, pVoice->mMIDINote,  iz);
                            pVoice->mMIDIPressure = iz;
                        }
                    }
                }		
            }
        }
        if(mKymaPoll)
        {
            // send NRPN with Soundplane identifier every few secs. for Kyma.
            const UInt64 nrpnPeriodMicrosecs = 1000*1000*4;
            if (mCurrFrameStartTime > mLastTimeNRPNWasSent + nrpnPeriodMicrosecs)
            {
                mLastTimeNRPNWasSent = mCurrFrameStartTime;
                // set NRPN
                mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 99, 0x53));
                mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 98, 0x50));
                
                // data entry -- send # of voices for Kyma
                mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 6, mVoices));
                
                // null NRPN
                mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 99, 0xFF));
                mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 98, 0xFF));                
            }
        }
    }
}

void SoundplaneMIDIOutput::sendPressure(int chan, int note, float p)
{
    //NOTE note -1 = undefined
    if(mMPEMode)
    {
        if(!mMPEExtended)
        {
            mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, p));
            return;
        }
        else
        {
            // multi channel, extensions
            if(mPressureActive) mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, p));
            mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 11, p));
            //mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 75, p));
        }
    }
    else  // single channel, poly aftertouch
    {
        if(mPressureActive&& note>=0) mpCurrentDevice->sendMessageNow(juce::MidiMessage::aftertouchChange(chan, note, p));
    }
    
}

void SoundplaneMIDIOutput::sendX(int chan, float p)
{
    if(mMPEExtended)
    {
        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 73, p));
    }
}

void SoundplaneMIDIOutput::sendY(int chan, float p)
{
    if (mMPEMode)
    {
        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 74, p));
    }
}

void SoundplaneMIDIOutput::sendPitchbend(int chan, float p)
{
    mpCurrentDevice->sendMessageNow(juce::MidiMessage::pitchWheel(chan, p));
}

void SoundplaneMIDIOutput::setBendRange(int r)
{
    mBendRange = r;
    sendPitchbendRange();
}

void SoundplaneMIDIOutput::setMaxTouches(int t)
{
    mVoices = clamp(t, 0, kMaxMIDIVoices);
    if (mMPEMode && mpCurrentDevice)
    {
        int globalChannel=mChannel;
        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(globalChannel, MIDI_MPE_MODE_CC, mVoices));
    }
}

void SoundplaneMIDIOutput::sendMPEChannels()
{
	int chan = getMPEMainChannel();
	mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, MIDI_MPE_MODE_CC, mMPEChannels));
}

void SoundplaneMIDIOutput::sendPitchbendRange()
{
	if(!mpCurrentDevice) return;
	int chan = mChannel;
	int quantizedRange = mBendRange;
	
	if(mMPEMode)
	{
		chan = getMPEVoiceChannel(0);
		
		// MPE spec requires a multiple of 12
		quantizedRange = (quantizedRange/12)*12;
	}

	mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 100, 0));
	mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 101, 0));
	mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 6, quantizedRange));
	mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 38, 0));
}



