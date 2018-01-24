//
//  Zone.h
//  Soundplane
//
//  Created by Randy Jones on 10/18/13.
//
//

#pragma once

#include "MLModel.h"
#include "SoundplaneModelA.h"
#include "SoundplaneDriver.h"
#include "MLOSCListener.h"
#include "OSC/zeroconf/NetService.h"
#include "OSC/zeroconf/NetServiceBrowser.h"
#include "TouchTracker.h"
#include "Controller.h"
#include "SoundplaneMIDIOutput.h"
#include "SoundplaneOSCOutput.h"
#include "MLSymbol.h"
#include "MLParameter.h"
#include "MLFileCollection.h"

#include <list>
#include <map>

#include "cJSON/cJSON.h"

enum ZoneType
{
    kZoneNoteRow,
    kZoneControllerX,
    kZoneControllerY,
    kZoneControllerXY,
    kZoneControllerZ,
    kZoneToggle,
    kZoneTypes
};

const int kZoneValArraySize = 8;

class Zone
{
    friend class SoundplaneModel;
public:
    Zone();
    ~Zone() {}

    static int symbolToZoneType(ml::Symbol s);

    void newFrame();
    void addTouchToFrame(int i, Touch t);
    void storeAnyNewTouches();
    
    void processTouches(const std::bitset<kMaxTouches>& freedTouches);
    
    void processTouchesNoteRow(const std::bitset<kMaxTouches>& freedTouches);
    void processTouchesNoteOffs(std::bitset<kMaxTouches>& freedTouches);

    const Touch touchToKeyPos(const Touch& t) const
    {
        return Touch{.x = mXRange(t.x), .y = mYRange(t.y), .kx = t.kx, .ky = t.ky, .z = t.z};
    }
    
    void setSampleRate(float r);
    
    const Touch getTouch(int i) const { return mTouches1[i]; }
    
    bool needsRedraw() const { return mNeedsRedraw; }
    const ml::TextFragment getName() const { return mName; }
    MLRect getBounds() const { return mBounds; }
	int getType() const { return mType; }
	int getOffset() const { return mOffset; }

    // return values on [0..1]
	//float getValue(int i) const { return mValue[ml::clamp(i, 0, kZoneValArraySize - 1)]; }
   // float getXValue() const { return getValue(0); }
   // float getYValue() const { return getValue(1); }

    // return values scaled to key grid
  //  float getXKeyPos() const { return mXRange(getValue(0)); }
  //  float getYKeyPos() const { return mYRange(getValue(1)); }

    // setters
    void setZoneID(int z) { mZoneID = z; }
    void setSnapFreq(float f);
    void setBounds(MLRect b);
    void setNeedsRedraw(bool b) { mNeedsRedraw = b; }
    
    // TODO look at usage wrt. x/y/z display and make these un-public again
    MLRect mBounds;
    MLRange mXRange;
    MLRange mYRange;
    MLRange mXRangeInv;
    MLRange mYRangeInv;
    
protected:
    
    int mZoneID{0};
    int mType{-1};
    int mStartNote{60};
    
    float mVibrato{0};
    float mHysteresis{0};
    bool mQuantize{false};
    bool mNoteLock{false};
    int mTranspose{0};
    
    // start note falls on this degree of scale-- for diatonic and other non-chromatic scales
    int mScaleNoteOffset = 0;
    
    MLSignal mScaleMap{};
    int mControllerNum1{0};
    int mControllerNum2{0};
    int mControllerNum3{0};

    bool mToggleValue{};
    int mOffset{0};
    ml::TextFragment mName{"unnamed zone"};
    
    // states read by the Model to generate output
    TouchArray mOutputTouches{};
    Controller mOutputController;
    
private:
    int getNumberOfActiveTouches() const;
    int getNumberOfNewTouches() const;
    Vec3 getAveragePositionOfActiveTouches() const;
    float getMaxZOfActiveTouches() const;
    
    void processTouchesControllerX();
    void processTouchesControllerY();
    void processTouchesControllerXY();
    void processTouchesControllerToggle();
    void processTouchesControllerPressure();

    bool mNeedsRedraw;
//    float mValue[kZoneValArraySize];

    // touch locations are stored scaled to [0..1] over the Zone boundary.
    // incoming touches 
    TouchArray mTouches0{};
    // touch positions this frame
    TouchArray mTouches1{};
    // touch positions saved at touch onsets
    TouchArray mStartTouches{};
    
	std::vector<MLBiquad> mNoteFilters;
	std::vector<MLBiquad> mVibratoFilters;
};


