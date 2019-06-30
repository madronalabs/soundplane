
// MadronaLib: a C++ framework for DSP applications.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#pragma once

#include <string>
#include <list>
#include <map>
#include "MLMatrix.h"
#include "MLSymbol.h"
#include "MLDebug.h"

// MLProperty: a modifiable property. Properties have four types: undefined, float, string, and signal.

class MLProperty
{
public:
	static const std::string nullString;
	static const ml::Matrix nullSignal;

	enum Type
	{
		kUndefinedProperty	= 0,
		kFloatProperty	= 1,
		kStringProperty = 2,
		kSignalProperty = 3
	};

	MLProperty();
	MLProperty(const MLProperty& other);
	MLProperty& operator= (const MLProperty & other);
	MLProperty(float v);
	MLProperty(const std::string& s);
	MLProperty(const ml::Matrix& s);
	~MLProperty();
    
	const float& getFloatValue() const;
	const std::string& getStringValue() const;
	const ml::Matrix& getSignalValue() const;
    
	// For each type of property, a setValue method must exist
	// to set the value of the property to that of the argument.
	//
	// For each type of property, if the size of the argument is equal to the
	// size of the current value, the value must be modified in place.
	// This guarantee keeps DSP graphs from allocating memory as they run.
	void setValue(const MLProperty& v);
	void setValue(const float& v);
	void setValue(const std::string& v);
	void setValue(const ml::Matrix& v);
	
	bool operator== (const MLProperty& b) const;
	bool operator!= (const MLProperty& b) const;
	Type getType() const { return mType; }
	
	bool operator<< (const MLProperty& b) const;
	
private:
	// TODO reduce storage requirements-- this is a minimal-code start
	// TODO stop using std::string, which seems to be causing threading issues
	Type mType;
	float mFloatVal;
	std::string mStringVal;
	ml::Matrix mSignalVal;
};

// utilities

std::ostream& operator<< (std::ostream& out, const MLProperty & r);
