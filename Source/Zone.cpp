//
//  Zone.cpp
//  Soundplane
//
//  Created by Randy Jones on 10/18/13.
//
//

#include "Zone.h"

static const MLSymbol zoneTypes[kZoneTypes] = {"note_row", "x", "y", "xy", "z", "toggle"};
static const float kVibratoFreq = 12.0f;

// turn zone type name into enum type. names above must match ZoneType enum.
int Zone::symbolToZoneType(MLSymbol s)
{
    int zoneTypeNum = -1;
    for(int i=0; i<kZoneTypes; ++i)
    {
        if(s == zoneTypes[i])
        {
            zoneTypeNum = i;
            break;
        }
    }
    return zoneTypeNum;
}

Zone::Zone(const SoundplaneListenerList& l) :
mZoneID(0),
mType(-1),
mBounds(0, 0, 1, 1),
mStartNote(60),
mTranspose(0),
mVibrato(0),
mScaleNoteOffset(0),
mControllerNum1(1),
mControllerNum2(2),
mControllerNum3(3),
mChannel(1),
mName("unnamed zone"),
mListeners(l)
{
    mNoteFilters.resize(kSoundplaneMaxTouches);
	mVibratoFilters.resize(kSoundplaneMaxTouches);

    clearTouches();
	for(int i=0; i<kSoundplaneMaxTouches; ++i)
	{
		mNoteFilters[i].setSampleRate(kSoundplaneSampleRate);
		mNoteFilters[i].setOnePole(250.0f);
		mVibratoFilters[i].setSampleRate(kSoundplaneSampleRate);
		mVibratoFilters[i].setOnePole(kVibratoFreq);
	}
    
    for(int i=0; i<kZoneValArraySize; ++i)        
    {
        mValue[i] = 0.;
    }
}

Zone::~Zone()
{
    
}

void Zone::setBounds(MLRect b)
{
    mBounds = b;
    mXRange = MLRange(0., 1., b.left(), b.right());
    mYRange = MLRange(0., 1., b.top(), b.bottom());
    mXRangeInv = MLRange(b.left(), b.right(), 0., 1.);
    mYRangeInv = MLRange(b.top(), b.bottom(), 0., 1.);
    
    
    mScaleMap.setDims(b.width() + 1);
    // setup chromatic scale
    for(int i=0; i<mScaleMap.getWidth(); ++i)
    {
        mScaleMap[i] = i;
    }

}

// input: approx. snap time in ms
void Zone::setSnapFreq(float f)
{
    float snapFreq = 1000.f / (f + 1.);
    snapFreq = clamp(snapFreq, 1.f, 1000.f);
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        mNoteFilters[i].setOnePole(snapFreq);
    }
}

void Zone::clearTouches()
{
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        mTouches0[i].clear();
        mTouches1[i].clear();
    }
}

void Zone::addTouchToFrame(int i, float x, float y, int kx, int ky, float z, float dz)
{
   // debug() << "zone " << mName << " adding touch at " << x << ", " << y << "\n";
    
    // convert to unity range over x and y bounds
    mTouches0[i] = ZoneTouch(mXRangeInv(x), mYRangeInv(y), kx, ky, z, dz);
}

// after all touches or a frame have been sent using addTouchToFrame, generate
// any needed messages about the frame and prepare for the next frame. 
void Zone::processTouches()
{
    // store previous touches and clear incoming for next frame
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        mTouches2[i] = mTouches1[i];
        mTouches1[i] = mTouches0[i];
        mTouches0[i].clear();
    }
    switch(mType)
    {
        case kNoteRow:
            processTouchesNoteRow();
            break;
        case kControllerX:
            processTouchesControllerX();
            break;
        case kControllerY:
            processTouchesControllerY();
            break;
        case kControllerXY:
            processTouchesControllerXY();
            break;
        case kToggle:
            processTouchesControllerToggle();
            break;
        case kControllerZ:
            processTouchesControllerPressure();
            break;
    }
}

void Zone::processTouchesNoteRow()
{
    // for each possible touch, send any active touch or touch off messages to listeners
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        ZoneTouch t1 = mTouches1[i];
        ZoneTouch t2 = mTouches2[i];
        bool isActive = t1.isActive();
        bool wasActive = t2.isActive();
        
        float t1x = t1.pos.x();
        float t1y = t1.pos.y();
        float t1z = t1.pos.z();
        float t1dz = t1.pos.w();
        
        float xPos = mXRange(t1x) - mBounds.left();
        float vibratoX = xPos;
        float scaleNote = mScaleMap.getInterpolated(xPos - 0.5f);
        float scaleNoteQ = mScaleMap[(int)xPos];
        if(mQuantize)
        {
            scaleNote = scaleNoteQ;
        }
        
        // setup filter inputs / outputs
        mNoteFilters[i].setInput(&scaleNote);
        mNoteFilters[i].setOutput(&scaleNote);
        mVibratoFilters[i].setInput(&vibratoX);
        mVibratoFilters[i].setOutput(&vibratoX);
        
        if(isActive && !wasActive)
        {
            // setup filter states for new note and output
            mNoteFilters[i].setState(scaleNote);
            mNoteFilters[i].process(1);
            mVibratoFilters[i].setState(vibratoX);
            mVibratoFilters[i].process(1);            
            sendMessage("touch", "on", i, t1x, t1y, t1z, t1dz, mStartNote + mTranspose + scaleNote);
        }
        else if(isActive)
        {
            // filter ongoing note
            mNoteFilters[i].process(1);
            mVibratoFilters[i].process(1);
            
            // add vibrato to note
            float vibratoHP = (xPos - vibratoX)*mVibrato*kSoundplaneVibratoAmount;
            scaleNote += vibratoHP;
            sendMessage("touch", "continue", i, t1x, t1y, t1z, t1dz, mStartNote + mTranspose + scaleNote);
        }
        else if(wasActive)
        {
            // on note off, send quantized note for release
            sendMessage("touch", "off", i, t2.pos.x(), t2.pos.y(), t2.pos.z(), t2.pos.w(), mStartNote + mTranspose + scaleNoteQ);
        }
    }
}

// process any note offs. called by the model for all zones before processTouches() so that any new
// touches with the same index as an expiring one will have a chance to get started.
void Zone::processTouchesNoteOffs()
{
    // for each possible touch, send any active touch or touch off messages to listeners
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        ZoneTouch t1 = mTouches1[i];
        ZoneTouch t2 = mTouches2[i];
        bool isActive = t1.isActive();
        bool wasActive = t2.isActive();
        
        float t1x = t1.pos.x();
        float xPos = mXRange(t1x) - mBounds.left();
        float scaleNoteQ = mScaleMap[(int)xPos];
        
        if(!isActive && wasActive)
        {
            // on note off, send quantized note for release
            sendMessage("touch", "off", i, t2.pos.x(), t2.pos.y(), t2.pos.z(), t2.pos.w(), mStartNote + mTranspose + scaleNoteQ);
        }
    }
}

int Zone::getNumberOfActiveTouches() const
{
    int activeTouches = 0;
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        ZoneTouch t = mTouches1[i];
        if(t.isActive())
        {
            activeTouches++;
        }
    }
    return activeTouches;
}

int Zone::getNumberOfNewTouches() const
{
    int newTouches = 0;
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        ZoneTouch t1 = mTouches1[i];
        ZoneTouch t2 = mTouches2[i];
        if(t1.isActive() && (!t2.isActive()))
        {
            newTouches++;
        }
    }
    return newTouches;
}

Vec3 Zone::getAveragePositionOfActiveTouches() const
{
    Vec3 avg;
    int activeTouches = 0;
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        ZoneTouch t = mTouches1[i];
        if(t.isActive())
        {
            avg += t.pos;
            activeTouches++;
        }
    }
    if(activeTouches > 0)
    {
        avg *= (1.f / (float)activeTouches);
    }
    return avg;
}

float Zone::getMaxZOfActiveTouches() const
{
    Vec3 avg;
    float maxZ = 0.f;
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        ZoneTouch t = mTouches1[i];
        if(t.isActive())
        {
            float z = t.pos.z();
            if(z > maxZ)
            {
                maxZ = z;
            }
        }
    }
    return maxZ;
}

void Zone::processTouchesControllerX()
{
    if(getNumberOfActiveTouches() > 0)
    {
        Vec3 avgPos = getAveragePositionOfActiveTouches();
        mValue[0] = clamp(avgPos.x(), 0.f, 1.f);
        // TODO add zone attribute to scale value to full range
        sendMessage("controller", "x", mZoneID, mChannel, mControllerNum1, mControllerNum2, mControllerNum3, mValue[0], 0, 0);
    }
}

void Zone::processTouchesControllerY()
{
    if(getNumberOfActiveTouches() > 0)
    {
        Vec3 avgPos = getAveragePositionOfActiveTouches();
        mValue[1] = clamp(avgPos.y(), 0.f, 1.f);
        sendMessage("controller", "y", mZoneID, mChannel, mControllerNum1, mControllerNum2, mControllerNum3, 0, mValue[1], 0);
    }    
}

void Zone::processTouchesControllerXY()
{
    if(getNumberOfActiveTouches() > 0)
    {
        Vec3 avgPos = getAveragePositionOfActiveTouches();
        mValue[0] = clamp(avgPos.x(), 0.f, 1.f);
        mValue[1] = clamp(avgPos.y(), 0.f, 1.f);
        sendMessage("controller", "xy", mZoneID, mChannel, mControllerNum1, mControllerNum2, mControllerNum3, mValue[0], mValue[1], 0);
    }
}

void Zone::processTouchesControllerToggle()
{
    bool touchOn = getNumberOfNewTouches() > 0;
    if(touchOn)
    {
        mValue[0] = !getToggleValue();
        sendMessage("controller", "toggle", mZoneID, mChannel, mControllerNum1, mControllerNum2, mControllerNum3, mValue[0], 0, 0);
    }
}

void Zone::processTouchesControllerPressure()
{
    float z = 0;
    if(getNumberOfActiveTouches() > 0)
    {
        z = getMaxZOfActiveTouches();
    }
    mValue[0] = clamp(z, 0.f, 1.f);
    sendMessage("controller", "z", mZoneID, mChannel, mControllerNum1, mControllerNum2, mControllerNum3, 0, 0, mValue[0]);
}

void Zone::sendMessage(MLSymbol type, MLSymbol type2, float a, float b, float c, float d, float e, float f, float g, float h)
{
    mMessage.mType = type;
    mMessage.mSubtype = type2;
    mMessage.mData[0] = a;
    mMessage.mData[1] = b;
    mMessage.mData[2] = c;
    mMessage.mData[3] = d;
    mMessage.mData[4] = e;
    mMessage.mData[5] = f;
    mMessage.mData[6] = g;
    mMessage.mData[7] = h;
    mMessage.mZoneName = mName;
    sendMessageToListeners();
}

void Zone::sendMessageToListeners()
{
    for(SoundplaneListenerList::const_iterator it = mListeners.begin(); it != mListeners.end(); it++)
    {
        if((*it)->isActive())
        {
            (*it)->processMessage(&mMessage);
        }
    }
}

