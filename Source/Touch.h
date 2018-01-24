//
//  Touch.h
//  soundplane
//
//  Created by Randy Jones on 1/22/18.
//

#pragma once

static constexpr int kMaxTouches = 16;

enum TouchState
{
    kTouchStateInactive = 0,
    kTouchStateOn,
    kTouchStateContinue,
    kTouchStateOff
};

struct Touch
{
    float x;
    float y;
    float z;
    float dz;
    
    int age;
    int state;

    // store the current key grid location the touch is in, which due to hysteresis may not be
    // the one directly under the position.
    int kx;
    int ky;
    
    float note;
    float vibrato;

    int voiceIdx;
};

typedef std::array<Touch, kMaxTouches> TouchArray;

inline bool touchIsActive(Touch t) { return t.state != kTouchStateInactive; }

