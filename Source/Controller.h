//
//  Controller.h
//  soundplane
//
//  Created by Randy Jones on 1/22/18.
//

#pragma once

#include "MLText.h"

// TODO this goes away
// Model uses Zones to generate messages. messages can be sorted if need be.

using namespace ml;

struct Controller
{
	Symbol name{};
	Symbol type{};
	//bool active{false};
	int number1{0};
	int number2{0};
	int offset{0};
	float x{0.f};
	float y{0.f};
	float z{0.f};
};

inline bool operator==(const Controller& a, const Controller& b)
{
	return (memcmp(&a, &b, sizeof(Controller)) == 0);
}

inline bool operator!=(const Controller& a, const Controller& b)
{
	return !(a == b);
}
