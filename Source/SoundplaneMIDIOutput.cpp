
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "MLDebug.h"
#include "SoundplaneMIDIOutput.h"

const std::string kSoundplaneMIDIDeviceName("Soundplane IAC out");

const int kMPE_MIDI_CC = 127;

const ml::Symbol startFrameSym("start_frame");
const ml::Symbol touchSym("touch");
const ml::Symbol retrigSym("retrig");
const ml::Symbol onSym("on");
const ml::Symbol continueSym("continue");
const ml::Symbol offSym("off");
const ml::Symbol controllerSym("controller");
const ml::Symbol xSym("x");
const ml::Symbol ySym("y");
const ml::Symbol xySym("xy");
const ml::Symbol zSym("z");
const ml::Symbol toggleSym("toggle");
const ml::Symbol endFrameSym("end_frame");
const ml::Symbol matrixSym("matrix");
const ml::Symbol nullSym;


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
	mState(kTouchStateInactive)
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
	mGotControllerChanges(false),
	mPressureActive(false),
	mBendRange(36),
	mTranspose(0),
	mHysteresis(0.5f),
	mMPEExtended(false),
	mMPEMode(true),
	mMPEChannels(0),
	mChannel(1),
	mKymaMode(false),
	mVerbose(false)
{
#ifdef DEBUG
	//mVerbose = true;
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
	if(!mMPEExtended)
	{
		// normal MPE: send pressure as channel pressure
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, p));
	}
	else
	{
		// multi channel, extensions
		if(mPressureActive) mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, p));
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 11, p));
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
	mPressureActive = v;
	if(mpCurrentDevice)
	{	
		// when turning pressure off, first send maximum values
		// so sounds don't get stuck off
		if(!v && mPressureActive)
		{
			sendAllMIDIChannelPressures(127);
		}

		// when activating pressure, initialise to zero
		else if(v && !mPressureActive)
		{
			sendAllMIDIChannelPressures(0);
		}
	}
}

// set MPE extended mode for compatibility with some synths. Not in the regular UI. Note that
// MPE mode must also be enabled for extended mode to work.
void SoundplaneMIDIOutput::setMPEExtended(bool v)
{
	mMPEExtended = v;
    if (!mpCurrentDevice) return;
	sendAllMIDIChannelPressures(0);
}

void SoundplaneMIDIOutput::setMPE(bool v)
{
	mMPEMode = v;
	
	// channels is always 15 now if we are in MPE mode. If we introduce splits or more complex MPE options this may change.
	mMPEChannels = mMPEMode ? 15 : 0;
	
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

void SoundplaneMIDIOutput::setKymaMode(bool v)
{
	MLConsole() << "SoundplaneMIDIOutput: kyma mode " << v << "\n";
	mKymaMode = v; 
}

// MPE spec defines a split mode using main channels 1 and 16. We ignore this for now and use only channel 1
// for the main channel, and 2 upwards for the individual voices.
int SoundplaneMIDIOutput::getMPEMainChannel()
{
	return 1;
}

int SoundplaneMIDIOutput::getMPEVoiceChannel(int voice)
{
	return 2 + ml::clamp(voice, 0, 14);
}

int SoundplaneMIDIOutput::getVoiceChannel(int v)
{
	return (mMPEMode) ? (getMPEVoiceChannel(v)) : mChannel;
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
	ip = ml::clamp(ip, 0, 16383);
	return ip;
}

int SoundplaneMIDIOutput::getMIDIVelocity(MIDIVoice* pVoice)
{
	float fVel = pVoice->dz*20000.f;
	return ml::clamp((int)fVel, 10, 127);
}

int SoundplaneMIDIOutput::getRetriggerVelocity(MIDIVoice* pVoice)
{
	// get retrigger velocity from current z
	float fVel = pVoice->z*127.f;
	return ml::clamp((int)fVel, 10, 127);
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

void SoundplaneMIDIOutput::beginOutputFrame(time_point<system_clock> now)
{
    setupVoiceChannels();
}

void SoundplaneMIDIOutput::processTouch(int i, int offset, const Touch& t)
{
    MIDIVoice* pVoice = &mMIDIVoices[i];
    pVoice->x = t.x;
    pVoice->y = t.y;
    pVoice->z = t.z;
    pVoice->dz = t.dz;
    pVoice->note = t.note;
    pVoice->vibrato = t.vibrato;
    
    switch(t.state)
    {
        case kTouchStateOn:
            pVoice->startX = t.x;
            pVoice->startY = t.y;
            pVoice->startNote = t.note;
            pVoice->mState = kTouchStateOn;
            pVoice->age = 1;
            
            // get nearest integer note
            pVoice->mMIDINote = ml::clamp((int)lround(pVoice->note) + mTranspose, 1, 127);
            pVoice->mMIDIVel = getMIDIVelocity(pVoice);
            pVoice->mSendNoteOn = true;
            
            // send pressure right away at note on
            if(mPressureActive)
            {
                int newPressure = ml::clamp((int)(pVoice->z*128.f), 0, 127);
                if(newPressure != pVoice->mMIDIPressure)
                {
                    pVoice->mMIDIPressure = newPressure;
                    pVoice->mSendPressure = true;
                }
            }
        
            break;
            
            
         case kTouchStateContinue:
            // retrigger notes for glissando mode when sliding from key to key within a zone.
            if(mGlissando)
            {
                int newMIDINote = ml::clamp((int)lround(pVoice->note) + mTranspose, 1, 127);
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
                int newPressure = ml::clamp((int)(pVoice->z*128.f), 0, 127);
                pVoice->mMIDIPressure = newPressure;
                pVoice->mSendPressure = true;
            }
            
            // if in MPE mode, or if this is the youngest voice, we may send pitch bend and xy controller data.
            if((getMostRecentVoice() == i) || mMPEMode)
            {
                int ip = getMIDIPitchBend(pVoice);
                if(ip != pVoice->mMIDIBend)
                {
                    pVoice->mMIDIBend = ip;
                    pVoice->mSendPitchBend = true;
                }
                
                int ix = ml::clamp((int)(pVoice->x*128.f), 0, 127);
                if(ix != pVoice->mMIDIXCtrl)
                {
                    pVoice->mMIDIXCtrl = ix;
                    pVoice->mSendXCtrl = true;
                }
                
                int iy = ml::clamp((int)(pVoice->y*128.f), 0, 127);
                if(iy != pVoice->mMIDIYCtrl)
                {
                    pVoice->mMIDIYCtrl = iy;
                    pVoice->mSendYCtrl = true;
                }
            }
            
            pVoice->age++;
            
            break;
            
    
        case kTouchStateOff:
            pVoice->mState = kTouchStateOff;
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
            break;
    }
}

void SoundplaneMIDIOutput::processController(int zoneID, int h, const Controller& m)
{
    // when a controller message comes in, make a local copy of the message and store by zone ID.
    // store incoming controller by zone ID
    mControllersByZone[zoneID] = m;
    
    mControllersByZone[zoneID].active = true;
    
    // store offset into Controller
    mControllersByZone[zoneID].offset = h;
    
    mGotControllerChanges = true;
}

void SoundplaneMIDIOutput::endOutputFrame()
{
	sendMIDIVoiceMessages();
	if(mGotControllerChanges) sendMIDIControllerMessages();
	if(mVerbose) dumpVoices();
	updateVoiceStates();
}

void SoundplaneMIDIOutput::clear()
{
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
				
		if(pVoice->mSendNoteOff)
		{
			mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOff(chan, pVoice->mPreviousMIDINote));
		}
		
		if(pVoice->mSendNoteOn)
		{
			mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOn(chan, pVoice->mMIDINote, (unsigned char)pVoice->mMIDIVel));
		}
		
		if(pVoice->mSendPitchBend)
		{		
			mpCurrentDevice->sendMessageNow(juce::MidiMessage::pitchWheel(chan, pVoice->mMIDIBend));
		}
		
		if(pVoice->mSendPressure)
		{
			int p = pVoice->mMIDIPressure;
			if(mMPEMode)
			{
				if(!mMPEExtended)
				{
					// normal MPE: send pressure as channel pressure
					mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, p));
				}
				else
				{
					// MPE extensions
					mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, p));	
					mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 11, p));
				}
			}
			else  // for single channel MIDI, send pressure as poly aftertouch
			{					
				mpCurrentDevice->sendMessageNow(juce::MidiMessage::aftertouchChange(chan, pVoice->mMIDINote, p));	
			}
		}
		
		if(pVoice->mSendXCtrl)
		{
			mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 73, pVoice->mMIDIXCtrl));
		}
		
		if(pVoice->mSendYCtrl)
		{
			mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 74, pVoice->mMIDIYCtrl));
		}
	}
}

void SoundplaneMIDIOutput::sendMIDIControllerMessages()
{
	// for each zone, send and clear any controller messages received since last frame
	for(int i=0; i<kSoundplaneAMaxZones; ++i)
	{
		Controller& c = mControllersByZone[i];
        
        if(c.active)
        {
            int ix = ml::clamp((int)(c.x*128.f), 0, 127);
            int iy = ml::clamp((int)(c.y*128.f), 0, 127);
            int iz = ml::clamp((int)(c.z*128.f), 0, 127);
            
            // use channel from zone, or default to channel dial setting.
            int channel = (c.offset > 0) ? (c.offset) : (mChannel);
            
            switch(c.type)
            {
                case kControllerX:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, c.number1, ix));
                    break;
                case kControllerY:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, c.number1, iy));
                    break;
                case kControllerXY:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, c.number1, ix));
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, c.number2, iy));
                    break;
                case kControllerZ:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, c.number1, iz));
                    break;
                case kControllerToggle:
                    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(channel, c.number1, ix));
                    break;
            }
        }
        
        // clear controller message
        mControllersByZone[i].active = false;
	}
	
	mGotControllerChanges = false;
}

void SoundplaneMIDIOutput::doInfrequentTasks()
{
	if(mpCurrentDevice && mKymaMode)
	{
		pollKymaViaMIDI();
	}
}

void SoundplaneMIDIOutput::pollKymaViaMIDI()
{
    // set NRPN
    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 99, 0x53));
    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 98, 0x50));
    
    // data entry -- send # of voices for Kyma
    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 6, mVoices));
    
    // null NRPN
    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 99, 0xFF));
    mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(16, 98, 0xFF));  
    
    // MLTEST Kyma debug
    MLConsole() << "polling Kyma via MIDI: " << mVoices << " voices.\n";
}

void SoundplaneMIDIOutput::updateVoiceStates()
{
	for(int i=0; i < mVoices; ++i)
	{
		MIDIVoice* pVoice = &mMIDIVoices[i];
		if (pVoice->mState == kTouchStateOn)
		{
			pVoice->mState = kTouchStateContinue;
		}
		else if(pVoice->mState == kTouchStateOff)
		{
			pVoice->mMIDIVel = 0;
			pVoice->mMIDINote = 0;
			pVoice->mState = kTouchStateInactive;
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
    mVoices = ml::clamp(t, 0, kMaxMIDIVoices);
    if (mMPEMode && mpCurrentDevice)
    {
        int globalChannel=mChannel;
        mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(globalChannel, kMPE_MIDI_CC, mVoices));
    }
}

void SoundplaneMIDIOutput::sendMPEChannels()
{
	int chan = getMPEMainChannel();
	if(!mpCurrentDevice) return;
	mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, kMPE_MIDI_CC, mMPEChannels));
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

void SoundplaneMIDIOutput::dumpVoices()
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
        int iz = ml::clamp((int)(pVoice->z*128.f), 0, 127);
        
        debug() << "v" << i << ": CHAN=" << getVoiceChannel(i) << " BEND = " << ip << " Z = " << iz << "\n";
    }
}

