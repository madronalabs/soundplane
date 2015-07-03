
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneMIDIOutput.h"

const std::string kSoundplaneMIDIDeviceName("Soundplane IAC out");

static const int kMPE_MIDI_CC = 127;

// --------------------------------------------------------------------------------
#pragma mark MIDIVoice

MIDIVoice::MIDIVoice() :
	age(0), x(0), y(0), z(0), note(0),
	startX(0), startY(0), startNote(0), vibrato(0),
    mMIDINote(-1), 
	mPreviousMIDINote(-1),
	mMIDIVel(0), mMIDIBend(0), mMIDIXCtrl(0), mMIDIYCtrl(0), mMIDIPressure(0),
	mMIDIChannel(0),
	mSendNoteOff(false),
	mSendNoteOn(false),
	mSendPressure(false),
	mSendPitchBend(false),
	mSendXCtrl(false),
	mSendYCtrl(false),
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
	mPressureActive(true),
	mDataFreq(250.),
	mLastTimeNRPNWasSent(0),
	mLastTimeVerbosePrint(0),
	mGotControllerChanges(false),
	mBendRange(36),
	mTranspose(0),
	mHysteresis(0.5f),
	mMPEChannels(0),
	mChannel(1),
	mKymaPoll(true),
	mVerbose(false)
{
#ifdef DEBUG
	mVerbose = true;
#endif
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

void SoundplaneMIDIOutput::sendMIDIChannelPressure(int chan, int p) 
{
    switch(mMidiMode)
    {
        case single_1:
//            if(note>=0) mpCurrentDevice->sendMessageNow(juce::MidiMessage::aftertouchChange(chan, note, p));
            break;
        case mpe:
        case multi_2:
        case single_2:
            mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, p));
            break;
        case multi_1:
            mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 11, p));
            break;
    }
}

void SoundplaneMIDIOutput::sendAllMIDIChannelPressures(int p)
{
	for(int c=1; c<=kMaxMIDIVoices; ++c)
	{
		sendMIDIChannelPressure(c, p);
	}
}


void SoundplaneMIDIOutput::sendAllMIDINotesOff() 
{
	for(int c=1; c<=kMaxMIDIVoices; ++c)
	{
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::allNotesOff(c));
	}
}

void SoundplaneMIDIOutput::setPressureActive(bool v) 
{ 
//	if(!mpCurrentDevice) return;
//	
//	// when turning pressure off, first send maximum values
//	// so sounds don't get stuck off
//    if(!v && mPressureActive)
//    {
//		sendAllMIDIChannelPressures(127);
//    }
//
//    // when activating pressure, initialise to zero
//    else if(v && !mPressureActive)
//    {
//        sendAllMIDIChannelPressures(0);
//    }
//	
//	mPressureActive = v;
}


void SoundplaneMIDIOutput::setMode(MidiMode v)
{
	mMidiMode = v;
	
	// channels is always 15 now if we are in MPE mode. If we introduce splits or more complex MPE options this may change.
    mMPEChannels = mMidiMode == MidiMode::mpe ? 15 : 0;
	
    if (!mpCurrentDevice) return;
	sendAllMIDINotesOff();
	sendAllMIDIChannelPressures(0);
	sendMPEChannels();
	sendPitchbendRange();
}

void SoundplaneMIDIOutput::setStartChannel(int v)
{
	if(mChannel == v) return;
	mChannel = v;
	if (!mpCurrentDevice) return;
	sendAllMIDINotesOff();
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

int SoundplaneMIDIOutput::getVoiceChannel(int v)
{
    return (mMidiMode==MidiMode::mpe || mMidiMode==MidiMode::multi_1 || mMidiMode==MidiMode::multi_2) ? (getMPEVoiceChannel(v)) : mChannel;
}

int SoundplaneMIDIOutput::getMIDIPitchBend(MIDIVoice* pVoice)
{
	int ip = 8192;
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
	ip = bendAmount;
	ip = clamp(ip, 0, 16383);
	return ip;
}

int SoundplaneMIDIOutput::getMIDIVelocity(MIDIVoice* pVoice)
{
	float fVel = pVoice->dz*100.f;
	fVel *= fVel;
	fVel *= 128.f;
	return clamp((int)fVel, 10, 127);
}

int SoundplaneMIDIOutput::getRetriggerVelocity(MIDIVoice* pVoice)
{
	// get retrigger velocity from current z
	float fVel = pVoice->z;
	fVel *= fVel;
	fVel *= 128.f;
	return clamp((int)fVel, 10, 127);
}

int SoundplaneMIDIOutput::getMostRecentVoice()
{        
	// get active voice played most recently
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
	return newestVoiceIdx;
}

void SoundplaneMIDIOutput::processSoundplaneMessage(const SoundplaneDataMessage* msg)
{
    static const MLSymbol startFrameSym("start_frame");
    static const MLSymbol touchSym("touch");
	static const MLSymbol retrigSym("retrig");
    static const MLSymbol onSym("on");
    static const MLSymbol continueSym("continue");
    static const MLSymbol offSym("off");
    static const MLSymbol controllerSym("controller");
    static const MLSymbol xSym("x");
    static const MLSymbol ySym("y");
    static const MLSymbol xySym("xy");
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
		setupVoiceChannels();
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

			/*
			// before note on, first send note off and pressure to 0 if needed
			if((pVoice->mMIDIVel >= 0) && (pVoice->mPreviousMIDINote >= 0))
			{
				pVoice->mSendNoteOff = true;
			}
			*/
			
			// get nearest integer note
			pVoice->mMIDINote = clamp((int)lround(pVoice->note) + mTranspose, 1, 127);
			pVoice->mMIDIVel = getMIDIVelocity(pVoice);
			pVoice->mSendNoteOn = true;
			
			// send pressure right away at note on
			if(mPressureActive)
			{
				int newPressure = clamp((int)(pVoice->z*128.f), 0, 127);
				if(newPressure != pVoice->mMIDIPressure)
				{
					pVoice->mMIDIPressure = newPressure;
					pVoice->mSendPressure = true;
				}	
			}
		}
        else if(subtype == continueSym)
        {			
			// retrigger notes for glissando mode when sliding from key to key within a zone.
			if(mGlissando)
			{
				int newMIDINote = clamp((int)lround(pVoice->note) + mTranspose, 1, 127);
				if(newMIDINote != pVoice->mMIDINote)
				{
					pVoice->mMIDINote = newMIDINote;
					pVoice->mMIDIVel = getRetriggerVelocity(pVoice);
					pVoice->mSendNoteOff = true;
					pVoice->mSendNoteOn = true;
				}
			}
			
			// whether in MPE mode or not, we may send pressure.
			// get the new MIDI pressure from the z value of the voice
			if(mPressureActive)
			{
				int newPressure = clamp((int)(pVoice->z*128.f), 0, 127);
				if(newPressure != pVoice->mMIDIPressure)
				{
					pVoice->mMIDIPressure = newPressure;
					pVoice->mSendPressure = true;
				}	
			}
			
			// if in MPE mode, or if this is the youngest voice, we may send pitch bend and xy controller data.
            if((getMostRecentVoice() == i) || mMidiMode==MidiMode::mpe || mMidiMode==MidiMode::multi_1 || mMidiMode==MidiMode::multi_2  )
			{
				int ip = getMIDIPitchBend(pVoice);	
				if(ip != pVoice->mMIDIBend)
				{
					pVoice->mMIDIBend = ip;
					pVoice->mSendPitchBend = true;
				}			
				
				int ix = clamp((int)(pVoice->x*128.f), 0, 127);
				if(ix != pVoice->mMIDIXCtrl)
				{                    
					pVoice->mMIDIXCtrl = ix;
					pVoice->mSendXCtrl = true;
				}
				
				int iy = clamp((int)(pVoice->y*128.f), 0, 127);
				if(iy != pVoice->mMIDIYCtrl)
				{
					pVoice->mMIDIYCtrl = iy;
					pVoice->mSendYCtrl = true;
				}
			}
						
            pVoice->age++;
        }
        else if(subtype == offSym)
        {
			pVoice->mState = kVoiceStateOff;
            pVoice->age = 0;
            pVoice->z = 0;
			
			// send quantized pitch on note off
			pVoice->note = (int)lround(pVoice->note);
			int ip = getMIDIPitchBend(pVoice);	
			if(ip != pVoice->mMIDIBend)
			{
				pVoice->mMIDIBend = ip;
				pVoice->mSendPitchBend = true;
			}
			
			pVoice->mSendNoteOff = true;
			
			// send pressure off
			if(mPressureActive)
			{
				pVoice->mMIDIPressure = 0;
				pVoice->mSendPressure = true;
			}
        }
    }
    else if(type == controllerSym)
    {
        // when a controller message comes in, make a local copy of the message and store by zone ID.
        int zoneID = msg->mData[0];
        mMessagesByZone[zoneID] = *msg;
		mGotControllerChanges = true;
    }
    else if(type == matrixSym)
    {
        // nothing to do
    }
    else if(type == endFrameSym)
    {
        sendMIDIVoiceMessages();
		if(mGotControllerChanges && mTimeToSendNewFrame) sendMIDIControllerMessages();
		if(mKymaPoll) pollKyma();
		if(mVerbose) dumpVoices();
		updateVoiceStates();
    }
}

void SoundplaneMIDIOutput::setupVoiceChannels()
{
	for(int i=0; i < mVoices; ++i)
	{
		MIDIVoice* pVoice = &mMIDIVoices[i];
		int chan = getVoiceChannel(i);	
		pVoice->mMIDIChannel = chan;
	}
}

void SoundplaneMIDIOutput::sendMIDIVoiceMessages()
{	
	// send MIDI notes and controllers for each live touch.
	// attempt to translate the notes into MIDI notes + pitch bend.
	for(int i=0; i < mVoices; ++i)
	{
		MIDIVoice* pVoice = &mMIDIVoices[i];
		int chan = pVoice->mMIDIChannel;
		
		if(pVoice->mSendPitchBend)
		{
            int p = pVoice->mMIDIBend;
            switch(mMidiMode)
            {
                case single_1:
                case single_2:
                case mpe:
                case multi_2:
                case multi_1:
                default:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::pitchWheel(chan, p));
                    break;
            }

			mpCurrentDevice->sendMessageNow(juce::MidiMessage::pitchWheel(chan, pVoice->mMIDIBend));
		}
		
		if(pVoice->mSendNoteOff)
		{
			mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOff(chan, pVoice->mPreviousMIDINote));
			//mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, 0));
		}

		if(pVoice->mSendNoteOn)
		{
			mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOn(chan, pVoice->mMIDINote, (unsigned char)pVoice->mMIDIVel));
		}
		
		if(pVoice->mSendPressure)
		{
			int p = pVoice->mMIDIPressure;
            switch(mMidiMode)
            {
                case single_1:
                    if(pVoice->mMIDINote>=0) mpCurrentDevice->sendMessageNow(juce::MidiMessage::aftertouchChange(chan, pVoice->mMIDINote, p));
                    break;
                case mpe:
                case multi_2:
                case single_2:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, p));
                    break;
                case multi_1:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 11, p));
                    break;
            }
		}
		
		if(pVoice->mSendXCtrl)
		{
            int p = pVoice->mMIDIXCtrl;
            switch(mMidiMode)
            {
                case single_1:
                case single_2:
                case mpe:
                case multi_2:
                    break;
                case multi_1:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 73, p));
                    break;
                default:
                    break;
            }
		}
		
		if(pVoice->mSendYCtrl)
		{
            int p = pVoice->mMIDIYCtrl;
            switch(mMidiMode)
            {
                case single_1:
                case single_2:
                case multi_2:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 1, p));
                    break;
                case mpe:
                case multi_1:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 74, p));
                    break;
                default:
                    break;
            }

		}
	}
}

void SoundplaneMIDIOutput::sendMIDIControllerMessages()
{
	static const MLSymbol controllerSym("controller");
	static const MLSymbol xSym("x");
	static const MLSymbol ySym("y");
	static const MLSymbol xySym("xy");
	static const MLSymbol zSym("z");
	static const MLSymbol toggleSym("toggle");
	static const MLSymbol nullSym("");
	
	// for each zone, send and clear any controller messages received since last frame
	for(int i=0; i<kSoundplaneAMaxZones; ++i)
	{
		SoundplaneDataMessage* pMsg = &(mMessagesByZone[i]);
		if(pMsg->mType == controllerSym)
		{
			// controller message data:
			// mZoneID, mChannel, mControllerNumber, mControllerNumber2, mControllerNumber3, x, y, z
			//
			int zoneChan = pMsg->mData[1];
			int ctrlNum1 = pMsg->mData[2];
			int ctrlNum2 = pMsg->mData[3];
			// int ctrlNum3 = pMsg->mData[4];
			float x = pMsg->mData[5];
			float y = pMsg->mData[6];
			float z = pMsg->mData[7];
			
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
	
	mGotControllerChanges = false;
}

void SoundplaneMIDIOutput::pollKyma()
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

void SoundplaneMIDIOutput::updateVoiceStates()
{
	for(int i=0; i < mVoices; ++i)
	{
		MIDIVoice* pVoice = &mMIDIVoices[i];
		if (pVoice->mState == kVoiceStateOn)
		{
			pVoice->mState = kVoiceStateActive;
		}
		else if(pVoice->mState == kVoiceStateOff)
		{
			pVoice->mMIDIVel = 0;
			pVoice->mMIDINote = 0;
			pVoice->mState = kVoiceStateInactive;
		}
		
		// defaults for next frame: don't send any data
		pVoice->mSendNoteOff = false;
		pVoice->mSendNoteOn = false;
		pVoice->mSendPressure = false;
		pVoice->mSendPitchBend = false;
		pVoice->mSendXCtrl = false;
		pVoice->mSendYCtrl = false;
		pVoice->mPreviousMIDINote = pVoice->mMIDINote;
	}
}

void SoundplaneMIDIOutput::setBendRange(int r)
{
    mBendRange = r;
    sendPitchbendRange();
}

void SoundplaneMIDIOutput::setMaxTouches(int t)
{
    mVoices = clamp(t, 0, kMaxMIDIVoices);
    if (mMidiMode==MidiMode::mpe && mpCurrentDevice)
    {
        int globalChannel=mChannel;
        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(globalChannel, kMPE_MIDI_CC, mVoices));
    }
}

void SoundplaneMIDIOutput::sendMPEChannels()
{
	int chan = getMPEMainChannel();
	mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, kMPE_MIDI_CC, mMPEChannels));
}

void SoundplaneMIDIOutput::sendPitchbendRange()
{
	if(!mpCurrentDevice) return;
	int chan = mChannel;
	int quantizedRange = mBendRange;
	
    if(mMidiMode==MidiMode::mpe)
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


void SoundplaneMIDIOutput::dumpVoices()
{
	const UInt64 verbosePeriodMicrosecs = 1000*1000*1;
	if (mCurrFrameStartTime > mLastTimeVerbosePrint + verbosePeriodMicrosecs)
	{
		// dump voices
		debug() << "----------------------\n";
		int newestVoiceIdx = getMostRecentVoice();
		if(newestVoiceIdx >= 0)
			debug() << "newest: " << newestVoiceIdx << "\n";
			
		for(int i=0; i<mVoices; ++i)
		{
			MIDIVoice* pVoice = &mMIDIVoices[i];					
			
			int ip = getMIDIPitchBend(pVoice);						
			int iz = clamp((int)(pVoice->z*128.f), 0, 127);
			
			debug() << "v" << i << ": CHAN=" << getVoiceChannel(i) << " BEND = " << ip << " Z = " << iz << "\n";
		}
		mLastTimeVerbosePrint = mCurrFrameStartTime;
	}
}

