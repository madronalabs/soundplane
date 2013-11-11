
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "SoundplaneMIDIOutput.h"

const std::string kSoundplaneMIDIDeviceName("Soundplane IAC out");

// --------------------------------------------------------------------------------
#pragma mark MIDIVoice

MIDIVoice::MIDIVoice() :
	mAge(0), mX(0), mY(0), mZ(0), mDz(0), mNote(0),
	mMIDINote(0), mMIDIVel(0), 
	mMIDIBend(0), mMIDIPressure(0), mMIDIYCtrl(0),
	mStartNote(0), mStartX(0), mStartY(0)
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
	mBendRange(1.),
	mTranspose(0),
	mHysteresis(0.5f),
	mMultiChannel(true),
	mStartChannel(1),
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

std::vector<std::string>& SoundplaneMIDIOutput::getDeviceList()
{
	return mDeviceList;
}

void SoundplaneMIDIOutput::setActive(bool v) 
{ 
	mActive = v; 
}

void SoundplaneMIDIOutput::setPressureActive(bool v) 
{ 
	// when turning pressure off, first send maximum values 
	// so sounds don't get stuck off
	mPressureActive = v; 
			
	if (!mpCurrentDevice) return;
	for(int c=1; c<=16; ++c)
	{				
		int maxPressure = 127;
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(c, maxPressure));
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(c, 11, maxPressure));
	}
}

void SoundplaneMIDIOutput::setMultiChannel(bool v)
{
	if(mMultiChannel == v) return;
	mMultiChannel = v;
	if (!mpCurrentDevice) return;
	for(int i=1; i<=16; ++i)
	{
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::allNotesOff(i));
	}
}

void SoundplaneMIDIOutput::setStartChannel(int v)
{
	if(mStartChannel == v) return;
	mStartChannel = v;
	if (!mpCurrentDevice) return;
	for(int i=1; i<=16; ++i)
	{
		mpCurrentDevice->sendMessageNow(juce::MidiMessage::allNotesOff(i));
	}
}

void SoundplaneMIDIOutput::modelStateChanged()
{

}

void SoundplaneMIDIOutput::processMessage(const SoundplaneDataMessage* msg)
{
   // debug() << "SoundplaneMIDIOutput msg:" << msg->mType << "\n";
}

/*
void SoundplaneMIDIOutput::processFrame(const MLSignal& touchFrame)
{
	if (!mActive) return;
	if (!mpCurrentDevice) return;

	float x, y, z, dz, note;

	UInt64 now = getMicroseconds();
	const UInt64 dataPeriodMicrosecs = 1000*1000 / mDataFreq;
	const UInt64 nrpnPeriodMicrosecs = 1000*1000*4;
	bool sendData = false;
	
	// get most recent active voice played
	unsigned int minAge = (unsigned int)-1;
	int newestVoiceIdx = -1;
	for(int i=0; i<mVoices; ++i)
	{
		int age = touchFrame(ageColumn, i);
		if(age > 0)
		{
			if(age < minAge)
			{
				minAge = age;
				newestVoiceIdx = i;
			}
		}
	}
	
	// store touch ages. 
	// if there are any note-ons, send this frame of data.
	for(int i=0; i<mVoices; ++i)
	{
		MIDIVoice* pVoice = &mMIDIVoices[i];
		pVoice->mNoteOn = false;
		pVoice->mNoteOff = false;
		int age = touchFrame(ageColumn, i);

		// note-on. 
		if (age == 1)
		{
			sendData = true;
			pVoice->mNoteOn = true;
			pVoice->mStartNote = touchFrame(noteColumn, i); 
			pVoice->mStartX = touchFrame(xColumn, i);
			pVoice->mStartY = touchFrame(yColumn, i);
		}
		else if ((age == 0) && (pVoice->mMIDIVel != 0))
		{
			sendData = true;
			pVoice->mNoteOff = true;
		}
	}
	
	// if not already sending data, look at the time.
	if (!sendData)
	{
		if (now > mLastTimeDataWasSent + (UInt64)dataPeriodMicrosecs)
		{
			mLastTimeDataWasSent = now;
			sendData = true;
		}
	}	

	// send data for all active touches
	// 
	if (sendData)
	{
		for(int i=0; i < mVoices; ++i)
		{
			MIDIVoice* pVoice = &mMIDIVoices[i];
			x = touchFrame(xColumn, i);
			y = touchFrame(yColumn, i);
			z = touchFrame(zColumn, i);
			dz = touchFrame(dzColumn, i);
			note = touchFrame(noteColumn, i);
			int prevNote = pVoice->mMIDINote;
			
			int chan = mMultiChannel ? (((mStartChannel + i - 1)%15) + 1) : mStartChannel;
						
			if(pVoice->mNoteOff)
			{
				// note off
				mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOff(chan, pVoice->mMIDINote));
				pVoice->mMIDIVel = 0;
				pVoice->mMIDINote = 0;		
			}
			else if (pVoice->mNoteOn)
			{				
				// turn note off if needed
				if(prevNote)
				{
					mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOff(chan, prevNote));				
				}
								
				// get nearest integer note
				pVoice->mMIDINote = clamp((int)lround(note) + mTranspose, 1, 127);
				
				// store start position
				pVoice->mStartX = x;
				pVoice->mStartY = y;

				float fVel = dz*128.f;
				pVoice->mMIDIVel = clamp((int)fVel, 16, 127);

				// reset pitch at note on!
				mpCurrentDevice->sendMessageNow(juce::MidiMessage::pitchWheel(chan, 8192));
				mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOn(chan, pVoice->mMIDINote, (unsigned char)pVoice->mMIDIVel));				
			}						
			
			// if note is on, send controllers.
			if (pVoice->mMIDIVel > 0)
			{
				int iy;
				int newNote = prevNote;
				
				// hysteresis: make it harder to move out of current key.
				// needed for retrig mode when model is not quantizing x
				// and applying its own hysteresis.
				if(!prevNote)
				{
					newNote = clamp((int)lround(note) + mTranspose, 1, 127);
				}
				else
				{
					float newNoteF = clamp(note + (float)mTranspose, 1.f, 127.f);
					float dNote = newNoteF - prevNote;
					float hystThresh = 0.5f + mHysteresis*0.5f;
					if(fabs(dNote) > hystThresh)
					{
						newNote = lround(newNoteF);
					}					
				}
					
				// retrigger notes when sliding from key to key	
				if ((newNote != pVoice->mMIDINote) && mRetrig)
				{
					// get retrigger velocity from current z
					float fVel = z*128.f;
					pVoice->mMIDIVel = clamp((int)fVel, 16, 127);
					
					// retrigger 
					mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOff(chan, pVoice->mMIDINote));
					pVoice->mMIDINote = newNote;		
					mpCurrentDevice->sendMessageNow(juce::MidiMessage::noteOn(chan, pVoice->mMIDINote, (unsigned char)pVoice->mMIDIVel));				
				}
				
				// send controllers etc. if in multichannel mode, or if this is the youngest voice. 
				if((newestVoiceIdx == i) || mMultiChannel)
				{
					// get pitch bend for note difference 
					//
					float fp = note - pVoice->mStartNote;		
					if (mBendRange > 0)
					{
						fp *= 8192.f;
						fp /= (float)mBendRange;
					}
					else
					{
						fp = 0.;
					}
					fp += 8192.f;
					
					// TODO if want pitch bend > 8192 distance from current note, 
					// get closest note to new pitch, find remainder pitch bend and retrigger note. 
				
					int ip = fp;
					ip = clamp(ip, 0, 16383);
					if(ip != pVoice->mMIDIBend)
					{
						mpCurrentDevice->sendMessageNow(juce::MidiMessage::pitchWheel(chan, ip));
						pVoice->mMIDIBend = ip;
					}
					
					// send y controller absolute
					iy = y*127.f;
					iy = clamp(iy, 0, 127);
					if(iy != pVoice->mMIDIYCtrl)
					{
						mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, kSoundplaneMIDIControllerY, iy));
						pVoice->mMIDIYCtrl = iy;
					}
							
					if(mPressureActive)
					{				
						// send pressure
						int press = 127. * z;
						press = clamp(press, 0, 127);
						if(press != pVoice->mMIDIPressure)
						{
							mpCurrentDevice->sendMessageNow(juce::MidiMessage::channelPressureChange(chan, press));
							mpCurrentDevice->sendMessageNow(juce::MidiMessage::controllerEvent(chan, 11, press));
							pVoice->mMIDIPressure = press;
						}
					}
				}
			}		
		}
	}
	
	if(mKymaPoll)
	{
		// send NRPN with Soundplane identifier every few secs. for Kyma.
		if (now > mLastTimeNRPNWasSent + nrpnPeriodMicrosecs)
		{
			mLastTimeNRPNWasSent = now;
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
*/

