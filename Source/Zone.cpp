//
//  Zone.cpp
//  Soundplane
//
//  Created by Randy Jones on 10/18/13.
//
//

#include "Zone.h"

const float kVibratoFilterFreq = 12.0f;
const float kSoundplaneVibratoAmount = 5.;

Zone::Zone()
{
	mNoteFilters.resize(kMaxTouches);
	mVibratoFilters.resize(kMaxTouches);
	
	for(int i=0; i<kMaxTouches; ++i)
	{
		mTouches0[i] = Touch{};
		mTouches1[i] = Touch{};
		mStartTouches[i] = Touch{};
	}
	
	for(int i=0; i<kMaxTouches; ++i)
	{
		mNoteFilters[i].setSampleRate(kSoundplaneFrameRate);
		mNoteFilters[i].setOnePole(250.0f);
		mVibratoFilters[i].setSampleRate(kSoundplaneFrameRate);
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
	for(int i=0; i<kMaxTouches; ++i)
	{
		mNoteFilters[i].setOnePole(snapFreq);
	}
}

void Zone::newFrame()
{
	for(int i=0; i<kMaxTouches; ++i)
	{
		mTouches1[i] = mTouches0[i];
		mTouches0[i] = Touch{};
		mOutputTouches[i] = Touch{};
	}
	//mOutputController = Controller{};
}

void Zone::addTouchToFrame(int i, Touch t)
{
	// MLTEST
	// debug() << "zone " << mName << " adding touch " << t << " at " << x << ", " << y << "\n";
	
	// convert to unity range over x and y bounds
	Touch u = t;
	u.x = mXRangeInv(t.x);
	u.y = mYRangeInv(t.y);
	mTouches0[i] = u;
}

void Zone::storeAnyNewTouches()
{
	for(int i=0; i<kMaxTouches; ++i)
	{
		// store start of touch
		if (touchIsActive(mTouches0[i]) && !(touchIsActive(mTouches1[i])))
		{
			mStartTouches[i] = mTouches0[i];
		}
	}
}

int Zone::getNumberOfActiveTouches() const
{
	int activeTouches = 0;
	for(int i=0; i<kMaxTouches; ++i)
	{
		if(touchIsActive(mTouches0[i]))
		{
			activeTouches++;
		}
	}
	return activeTouches;
}

int Zone::getNumberOfNewTouches() const
{
	int newTouches = 0;
	for(int i=0; i<kMaxTouches; ++i)
	{
		if(touchIsActive(mTouches0[i]) && (!touchIsActive(mTouches1[i])))
		{
			newTouches++;
		}
	}
	return newTouches;
}

Vec3 Zone::getAveragePositionOfActiveTouches() const
{
	Vec2 avg;
	int activeTouches = 0;
	for(int i=0; i<kMaxTouches; ++i)
	{
		Touch t = mTouches0[i];
		if(touchIsActive(t))
		{
			avg += Vec2{t.x, t.y};
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
	float maxZ = 0.f;
	for(int i=0; i<kMaxTouches; ++i)
	{
		Touch t = mTouches0[i];
		if(touchIsActive(t))
		{
			float z = t.z;
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
void Zone::processTouches(const std::bitset<kMaxTouches>& freedTouches)
{
	mOutputController.type = mType;
	mOutputController.name = mName;
	//	mOutputController.active = true;
	
	if(mType == "note_row")
	{
			processTouchesNoteRow(freedTouches);
	}
	else if(mType == "x")
	{
			processTouchesControllerX();
	}
	else if(mType == "y")
	{
			processTouchesControllerY();
	}
	else if(mType == "xy")
	{
			processTouchesControllerXY();
	}
	else if(mType == "toggle")
	{
			processTouchesControllerToggle();
	}
	else if(mType == "z")
	{
			processTouchesControllerPressure();
	}
}

void Zone::processTouchesNoteRow(const std::bitset<kMaxTouches>& freedTouches)
{
	for(int i=0; i<kMaxTouches; ++i)
	{
		Touch t1 = mTouches0[i];
		Touch t2 = mTouches1[i];
		Touch tStart = mStartTouches[i];
		bool isActive = touchIsActive(t1);
		bool wasActive = touchIsActive(t2);
		bool releasing = (!isActive && wasActive);
		
		float t1x, t1y;
		if(releasing)
		{
			// use previous position on release
			t1x = t2.x;
			t1y = t2.y;
		}
		else
		{
			t1x = t1.x;
			t1y = t1.y;
		}
		float t1z = t1.z;
		float t1dz = t1.dz;
		float tStartX = tStart.x;
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
			float note = mStartNote + mTranspose + scaleNote;
			mOutputTouches[i] = Touch{.x = t1x, .y = t1y, .z = t1z, .dz = t1dz, .note = note, .state = kTouchStateOn};
		}
		else if(isActive)
		{
			// filter ongoing note
			scaleNote = mNoteFilters[i].processSample(scaleNote);
			vibratoX = mVibratoFilters[i].processSample(vibratoX);
			
			// subtract low pass filter to get vibrato amount
			float vibratoHP = (currentXPos - vibratoX)*mVibrato*kSoundplaneVibratoAmount;
			
			float note = mStartNote + mTranspose + scaleNote + vibratoHP;
			mOutputTouches[i] = Touch{.x = t1x, .y = t1y, .z = t1z, .dz = t1dz, .note = note, .state = kTouchStateContinue, .vibrato = vibratoHP};
		}
	}
}

void Zone::processTouchesControllerX()
{
	if(getNumberOfActiveTouches() > 0)
	{
		float zVal = ml::clamp(getMaxZOfActiveTouches(), 0.f, 1.f);
		if(zVal > 0.f)
		{
			Vec3 avgPos = getAveragePositionOfActiveTouches();
			float xVal = ml::clamp(avgPos.x(), 0.f, 1.f);
			
			mOutputController.number1 = mControllerNum1;
			mOutputController.x=xVal;
		}
	}
}

void Zone::processTouchesControllerY()
{
	if(getNumberOfActiveTouches() > 0)
	{
		Vec3 avgPos = getAveragePositionOfActiveTouches();
		float yVal = ml::clamp(avgPos.y(), 0.f, 1.f);
		mOutputController.number1 = mControllerNum1;
		mOutputController.y = yVal;
	}
}

void Zone::processTouchesControllerXY()
{
	if(getNumberOfActiveTouches() > 0)
	{
		float zVal = ml::clamp(getMaxZOfActiveTouches(), 0.f, 1.f);
		if(zVal > 0.f)
		{
		Vec3 avgPos = getAveragePositionOfActiveTouches();
		float xVal = ml::clamp(avgPos.x(), 0.f, 1.f);
		float yVal = ml::clamp(avgPos.y(), 0.f, 1.f);
		mOutputController.number1 = mControllerNum1;
		mOutputController.number2 = mControllerNum2;
		mOutputController.x=xVal;
		mOutputController.y = yVal;
		}
	}
}

void Zone::processTouchesControllerToggle()
{
	bool touchOn = getNumberOfNewTouches() > 0;
	if(touchOn)
	{
		mToggleValue = !mToggleValue;
		float xVal = static_cast<float>(mToggleValue);
		mOutputController.number1 = mControllerNum1;
		mOutputController.x=xVal;
	}
}

void Zone::processTouchesControllerPressure()
{
	float zVal = ml::clamp(getMaxZOfActiveTouches(), 0.f, 1.f);
	mOutputController.number1 = mControllerNum1;
	mOutputController.z=zVal;
}


// process any note offs. called by the model for all zones before processTouches() so that any new
// notes with the same index as an expiring one will have a chance to get started.
//
// TODO do we need freedTouches at all if we are re-sorting the touches later in instruments and always have
// say 16 possible touches?
void Zone::processTouchesNoteOffs(std::bitset<kMaxTouches>& freedTouches)
{
	for(int i=0; i<kMaxTouches; ++i)
	{
		Touch t1 = mTouches0[i];
		Touch t2 = mTouches1[i];
		bool isActive = touchIsActive(t1);
		bool wasActive = touchIsActive(t2);
		
		float t2x = t2.x;
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
				
				//							std::cout << "OFF" << i << " \n";
				
				// on note off, retain last note for release
				float lastScaleNote;
				if(mQuantize)
				{
					lastScaleNote = scaleNote;
				}
				else
				{
					float lastX = mXRange(t2.x) - mBounds.left();
					lastScaleNote = mScaleMap.getInterpolatedLinear(lastX - 0.5f);
				}
				freedTouches[i] = true;
				
				// set state
				float note = mStartNote + mTranspose + lastScaleNote;
				mOutputTouches[i] = Touch{.x = t2.x, .y = t2.y, .z = t2.z, .dz = t2.dz, .note = note, .state = kTouchStateOff};
			}
		}
	}
}

