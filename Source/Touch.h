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
    
    int note;
    float vibrato;

    int voiceIdx;
};

typedef std::array<Touch, kMaxTouches> TouchArray;

// C++11 does not permit aggregates to have default member initializers, assigning to this is
// a succinct way to make a non-zero initializer
constexpr Touch kDefaultTouch
{
    .x = 0.f, .y = 0.f,
    .kx = -1, .ky = -1,
    .z = 0.f, .dz = 0.f,
    .age = 0,
    .state = 0,
    .note = 0,
    .vibrato = 0.,

};

inline bool touchIsActive(Touch t) { return t.state != kTouchStateInactive; }

