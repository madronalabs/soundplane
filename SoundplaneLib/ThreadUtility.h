
// Driver for Soundplane Model A.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __THREAD_UTILITY__
#define __THREAD_UTILITY__

#include <pthread.h>
#include <stdint.h>

void setThreadPriority(pthread_t inThread, uint32_t inPriority, bool inIsFixed);
void SetPriorityRealtimeAudio(pthread_t inThread);

#endif // __THREAD_UTILITY__
