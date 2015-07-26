
// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __THREAD_UTILITY__
#define __THREAD_UTILITY__

#include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>

void setThreadPriority(pthread_t inThread, UInt32 inPriority, Boolean inIsFixed);

#endif // __THREAD_UTILITY__
