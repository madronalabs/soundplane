//
//  Controller.h
//  soundplane
//
//  Created by Randy Jones on 1/22/18.
//

#pragma once

#include "MLText.h"

using namespace ml;

constexpr int kMaxControllers = 128;

enum ControllerType
{
    kControllerX = 0,
    kControllerY,
    kControllerXY,
    kControllerZ,
    kControllerToggle,
};

struct Controller
{
    TextFragment name;
    bool active;
    int number1;
    int number2;
    int offset;
    int type;
    float x;
    float y;
    float z;
};

typedef std::array<Controller, kMaxControllers> ControllerArray;


/*
constexpr Controller kDefaultController // can't do this with non-literal types.
{
    .number = 0,
  //  .name = "",
    .value = {0.f}
};
*/
