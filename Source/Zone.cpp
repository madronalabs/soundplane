//
//  Zone.cpp
//  Soundplane
//
//  Created by Randy Jones on 10/18/13.
//
//

#include "Zone.h"

const ml::Symbol zoneTypes[kZoneTypes] = {"note_row", "x", "y", "xy", "z", "toggle"};
const float kVibratoFilterFreq = 12.0f;
const float kSoundplaneVibratoAmount = 5.;

// turn zone type name into enum type. names above must match ZoneType enum.
int Zone::symbolToZoneType(ml::Symbol s)
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

Zone::Zone() :
	mZoneID(0),
	mType(-1),
	mBounds(0, 0, 1, 1),
	mStartNote(60),
	mVibrato(0),
	mHysteresis(0),
	mQuantize(0),
	mNoteLock(0),
	mTranspose(0),
	mScaleNoteOffset(0),
	mControllerNum1(1),
	mControllerNum2(2),
	mControllerNum3(3),
	mOffset(0),
	mName("unnamed zone")
{
    mNoteFilters.resize(kSoundplaneMaxTouches);
	mVibratoFilters.resize(kSoundplaneMaxTouches);

    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        mTouches0[i].clear();
        mTouches1[i].clear();
        mStartTouches[i].clear();
    }
    
	for(int i=0; i<kSoundplaneMaxTouches; ++i)
	{
        float kDefaultSampleRate = 100.f;
		mNoteFilters[i].setSampleRate(kDefaultSampleRate);
		mNoteFilters[i].setOnePole(250.0f);
		mVibratoFilters[i].setSampleRate(kDefaultSampleRate);
		mVibratoFilters[i].setOnePole(kVibratoFilterFreq);
	}
    
    for(int i=0; i<kZoneValArraySize; ++i)        
    {
        mValue[i] = 0.;
    }
}

void Zone::setSampleRate(float r)
{
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        mNoteFilters[i].setSampleRate(r);
        mVibratoFilters[i].setSampleRate(r);
        
        // scalar biquads need freq reset after sample rate change
        mNoteFilters[i].setOnePole(250.0f);
        mVibratoFilters[i].setOnePole(kVibratoFilterFreq);
    }
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
    snapFreq = ml::clamp(snapFreq, 1.f, 1000.f);
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        mNoteFilters[i].setOnePole(snapFreq);
    }
}

void Zone::newFrame()
{
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        mTouches1[i] = mTouches0[i];
        mTouches0[i].clear();
    }
}

void Zone::addTouchToFrame(int t, float x, float y, int kx, int ky, float z, float dz)
{
    // MLTEST
    // debug() << "zone " << mName << " adding touch at " << x << ", " << y << "\n";
    // convert to unity range over x and y bounds
    mTouches0[t] = ZoneTouch(mXRangeInv(x), mYRangeInv(y), kx, ky, z, dz);
}

void Zone::storeAnyNewTouches()
{
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        // store start of touch
        if (mTouches0[i].isActive() && !(mTouches1[i].isActive()))
        {
            mStartTouches[i] = mTouches0[i];
        }
    }
}

int Zone::getNumberOfActiveTouches() const
{
    int activeTouches = 0;
    for(int i=0; i<kSoundplaneMaxTouches; ++i)
    {
        ZoneTouch t = mTouches0[i];
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
        ZoneTouch t1 = mTouches0[i];
        ZoneTouch t2 = mTouches1[i];
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
        ZoneTouch t = mTouches0[i];
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
        ZoneTouch t = mTouches0[i];
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

// after all touches for a frame have been received using addTouchToFrame, generate
// any needed messages about the frame and prepare for the next frame. 
SoundplaneZoneMessage Zone::processTouches()
{
    SoundplaneZoneMessage r;

    switch(mType)
    {
        case kNoteRow:
            assert(false);
            break;
        case kControllerX:
            r = processTouchesControllerX();
            break;
        case kControllerY:
            r = processTouchesControllerY();
            break;
        case kControllerXY:
            r = processTouchesControllerXY();
            break;
        case kToggle:
            r = processTouchesControllerToggle();
            break;
        case kControllerZ:
            r = processTouchesControllerPressure();
            break;
    }

    return r;
}

SoundplaneZoneMessage Zone::processTouchNoteRow(int i, const std::vector<bool>& freedTouches)
{
    SoundplaneZoneMessage r;
 
    ZoneTouch t1 = mTouches0[i];
    ZoneTouch t2 = mTouches1[i];
    ZoneTouch tStart = mStartTouches[i];
    bool isActive = t1.isActive();
    bool wasActive = t2.isActive();
    bool releasing = (!isActive && wasActive);
    
    float t1x, t1y;
    if(releasing)
    {
        // use previous position on release
        t1x = t2.pos.x();
        t1y = t2.pos.y();
    }
    else
    {
        t1x = t1.pos.x();
        t1y = t1.pos.y();
    }
    float t1z = t1.pos.z();
    float t1dz = t1.pos.w();
    float tStartX = tStart.pos.x();
    float currentXPos = mXRange(t1x) - mBounds.left();
    float startXPos = mXRange(tStartX) - mBounds.left();
    float vibratoX = currentXPos;
    float touchPos, scaleNote;
    
    if(mNoteLock)
    {
        touchPos = startXPos;
    }
    else
    {
        touchPos = currentXPos;
    }
    
    if(mQuantize)
    {
        scaleNote = mScaleMap[(int)touchPos];
    }
    else
    {
        scaleNote = mScaleMap.getInterpolatedLinear(touchPos - 0.5f);
    }

    if(isActive && !wasActive)
    {
        // if touch i was freed on the frame preceding this one, it moved
        // from zone to zone.
        bool retrig = (freedTouches[i]);

        // setup filter states for new note and output
        mNoteFilters[i].setState(scaleNote);
        mVibratoFilters[i].setState(vibratoX);
        
        if(retrig)
        {
            // sliding from key to key- get retrigger velocity from current z
            t1dz = ml::clamp(t1z * 0.01f, 0.0001f, 1.f);
        }
        else
        {
            // clamp note-on dz for use as velocity later.
            t1dz = ml::clamp(t1dz, 0.0001f, 1.f);
        }
        
        // MLTEST
        std::cout << "zone ID " << mZoneID << " ON " << i << " : " << t1dz << "\n";
        
        r = buildMessage("touch", "on", i, t1x, t1y, t1z, t1dz, mStartNote + mTranspose + scaleNote);
    }
    else if(isActive)
    {
        // filter ongoing note
        scaleNote = mNoteFilters[i].processSample(scaleNote);
        vibratoX = mVibratoFilters[i].processSample(vibratoX);
        
        // subtract low pass filter to get vibrato amount
        float vibratoHP = (currentXPos - vibratoX)*mVibrato*kSoundplaneVibratoAmount;
        

        // MLTEST
        //std::cout << "zone ID " << mZoneID << " CN " << i << " : " << t1x << "\n";

        //  std::cout << "t" << i << ": " <<mCurrentKeyX[i] << ", " << mCurrentKeyY[i] << "\n";
        
        
        // hysteresis
        
        
        
        // send continue touch message
        // is dz useful here or should it be 0?
        r = buildMessage("touch", "continue", i, t1x, t1y, t1z, t1dz, mStartNote + mTranspose + scaleNote, vibratoHP);
    }

    return r;
}

// process any note offs. called by the model for all zones before processTouches() so that any new
// notes with the same index as an expiring one will have a chance to get started.
SoundplaneZoneMessage Zone::processTouchesNoteOffs(int i, std::vector<bool>& freedTouches)
{
    SoundplaneZoneMessage r;

    ZoneTouch t1 = mTouches0[i];
    ZoneTouch t2 = mTouches1[i];
    bool isActive = t1.isActive();
    bool wasActive = t2.isActive();
    
    float t2x = t2.pos.x();
    float xPos = mXRange(t2x) - mBounds.left();
    xPos = ml::clamp(xPos, 0.f, mBounds.width());
    float scaleNote;
    if(mQuantize)
    {
        scaleNote = mScaleMap[(int)xPos];
    }
    else
    {
        scaleNote = mScaleMap.getInterpolatedLinear(xPos - 0.5f);
    }
    if(wasActive)
    {
        if(!isActive)
        {
            // on note off, retain last note for release
            float lastScaleNote;
            if(mQuantize)
            {
                lastScaleNote = scaleNote;
            }
            else
            {
                float lastX = mXRange(t2.pos.x()) - mBounds.left();
                lastScaleNote = mScaleMap.getInterpolatedLinear(lastX - 0.5f);
            }
            freedTouches[i] = true;
            r = buildMessage("touch", "off", i, t2.pos.x(), t2.pos.y(), t2.pos.z(), t2.pos.w(), mStartNote + mTranspose + lastScaleNote);
        }
    }

    return r;
}

SoundplaneZoneMessage Zone::processTouchesControllerX()
{
    SoundplaneZoneMessage r;
    if(getNumberOfActiveTouches() > 0)
    {
        Vec3 avgPos = getAveragePositionOfActiveTouches();
        mValue[0] = ml::clamp(avgPos.x(), 0.f, 1.f);
        // TODO add zone attribute to scale value to full range
        r = buildMessage("controller", "x", mZoneID, 0, mControllerNum1, mControllerNum2, mControllerNum3, mValue[0], 0, 0);
    }
    return r;
}

SoundplaneZoneMessage Zone::processTouchesControllerY()
{
    SoundplaneZoneMessage r;
    if(getNumberOfActiveTouches() > 0)
    {
        Vec3 avgPos = getAveragePositionOfActiveTouches();
        mValue[1] = ml::clamp(avgPos.y(), 0.f, 1.f);
        r = buildMessage("controller", "y", mZoneID, 0, mControllerNum1, mControllerNum2, mControllerNum3, 0, mValue[1], 0);
    }    
    return r;
}

SoundplaneZoneMessage Zone::processTouchesControllerXY()
{
    SoundplaneZoneMessage r;
    if(getNumberOfActiveTouches() > 0)
    {
        Vec3 avgPos = getAveragePositionOfActiveTouches();
        mValue[0] = ml::clamp(avgPos.x(), 0.f, 1.f);
        mValue[1] = ml::clamp(avgPos.y(), 0.f, 1.f);
        r = buildMessage("controller", "xy", mZoneID, 0, mControllerNum1, mControllerNum2, mControllerNum3, mValue[0], mValue[1], 0);
    }
    return r;
}

SoundplaneZoneMessage Zone::processTouchesControllerToggle()
{
    SoundplaneZoneMessage r;
    bool touchOn = getNumberOfNewTouches() > 0;
    if(touchOn)
    {
        mValue[0] = !getToggleValue();
        r = buildMessage("controller", "toggle", mZoneID, 0, mControllerNum1, mControllerNum2, mControllerNum3, mValue[0], 0, 0);
    }
    return r;
}

SoundplaneZoneMessage Zone::processTouchesControllerPressure()
{
    SoundplaneZoneMessage r;
    float z = 0;
    if(getNumberOfActiveTouches() > 0)
    {
        z = getMaxZOfActiveTouches();
    }
    mValue[0] = ml::clamp(z, 0.f, 1.f);
    r = buildMessage("controller", "z", mZoneID, 0, mControllerNum1, mControllerNum2, mControllerNum3, 0, 0, mValue[0]);
    return r;
}

SoundplaneZoneMessage Zone::buildMessage(ml::Symbol type, ml::Symbol subtype, float a, float b, float c, float d, float e, float f, float g, float h)
{
    SoundplaneZoneMessage m;
    m.mType = type;
    m.mSubtype = subtype;
	m.mOffset = mOffset;			// send port offset of this zone
	m.mZoneName = mName;
    m.mData[0] = a;
    m.mData[1] = b;
    m.mData[2] = c;
    m.mData[3] = d;
    m.mData[4] = e;
    m.mData[5] = f;
    m.mData[6] = g;
    m.mData[7] = h;
    return m;
}

