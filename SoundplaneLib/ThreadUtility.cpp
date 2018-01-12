// ThreadUtility.cpp

#include "ThreadUtility.h"

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_init.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <assert.h>
#include <CoreServices/CoreServices.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <unistd.h>

#include <iostream>

void setThreadPriority(pthread_t inThread, uint32_t inPriority, bool inIsFixed)
{
	// on null thread argument, set the priority of the current thread
	pthread_t threadToAffect = inThread ? inThread : pthread_self();
	
    if (inPriority == 96)
    {
        // REAL-TIME / TIME-CONSTRAINT THREAD
        thread_time_constraint_policy_data_t    theTCPolicy;
		
		// times here are specified in Mach absolute time units.
		mach_timebase_info_data_t sTimebaseInfo;
		mach_timebase_info(&sTimebaseInfo);

		uint64_t period_nano = 1000*1000;
		uint64_t period_abs = (double)period_nano * (double)sTimebaseInfo.denom / (double)sTimebaseInfo.numer;

		theTCPolicy.period = period_abs;
        theTCPolicy.computation = period_abs/64;
        theTCPolicy.constraint = period_abs/4;
        theTCPolicy.preemptible = true;
        
        auto err = thread_policy_set (pthread_mach_thread_np(threadToAffect), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&theTCPolicy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
        
        if (kIOReturnSuccess != err)
       {
           
           
           
       }
    }
    else
    {
        // OTHER THREADS
        thread_extended_policy_data_t   theFixedPolicy;
        thread_precedence_policy_data_t   thePrecedencePolicy;
        int32_t                relativePriority;

        // [1] SET FIXED / NOT FIXED
        theFixedPolicy.timeshare = !inIsFixed;
        thread_policy_set (pthread_mach_thread_np(threadToAffect), THREAD_EXTENDED_POLICY, (thread_policy_t)&theFixedPolicy, THREAD_EXTENDED_POLICY_COUNT);

        // [2] SET PRECEDENCE
        relativePriority = inPriority;
        thePrecedencePolicy.importance = relativePriority;
        thread_policy_set (pthread_mach_thread_np(threadToAffect), THREAD_PRECEDENCE_POLICY, (thread_policy_t)&thePrecedencePolicy, THREAD_PRECEDENCE_POLICY_COUNT);
    }
}


// Enables time-contraint policy and priority suitable for low-latency,
// glitch-resistant audio.
// from adobe/chromium/base/threading/platform_thread_mac.mm

void SetPriorityRealtimeAudio(pthread_t inThread)
{
    kern_return_t result;
    
    // Increase thread priority to real-time.
    
    // Please note that the thread_policy_set() calls may fail in
    // rare cases if the kernel decides the system is under heavy load
    // and is unable to handle boosting the thread priority.
    // In these cases we just return early and go on with life.
    
    mach_port_t mach_thread_id = pthread_mach_thread_np(inThread);

    
    // Make thread fixed priority.
    thread_extended_policy_data_t policy;
    policy.timeshare = 0;  // Set to 1 for a non-fixed thread.
    result = thread_policy_set(mach_thread_id,
                               THREAD_EXTENDED_POLICY,
                               (thread_policy_t)&policy,
                               THREAD_EXTENDED_POLICY_COUNT);
    if (result != KERN_SUCCESS)
    {
        std::cout << "thread_policy_set() failure: " << result;
        return;
    }
    
    // Set to relatively high priority.
    thread_precedence_policy_data_t precedence;
    precedence.importance = 63;
    result = thread_policy_set(mach_thread_id,
                               THREAD_PRECEDENCE_POLICY,
                               (thread_policy_t)&precedence,
                               THREAD_PRECEDENCE_POLICY_COUNT);
    if (result != KERN_SUCCESS)
        {
            std::cout << "thread_policy_set() failure: " << result;
            return;
        }
    
    // Most important, set real-time constraints.
    
    // Define the guaranteed and max fraction of time for the audio thread.
    // These "duty cycle" values can range from 0 to 1.  A value of 0.5
    // means the scheduler would give half the time to the thread.
    // These values have empirically been found to yield good behavior.
    // Good means that audio performance is high and other threads won't starve.
    const double kGuaranteedAudioDutyCycle = 0.75;
    const double kMaxAudioDutyCycle = 0.85;
    
    // Define constants determining how much time the audio thread can
    // use in a given time quantum.  All times are in milliseconds.
    
    // About 128 frames @44.1KHz
    const double kTimeQuantum = 2.9;
    
    // Time guaranteed each quantum.
    const double kAudioTimeNeeded = kGuaranteedAudioDutyCycle * kTimeQuantum;
    
    // Maximum time each quantum.
    const double kMaxTimeAllowed = kMaxAudioDutyCycle * kTimeQuantum;
    
    // Get the conversion factor from milliseconds to absolute time
    // which is what the time-constraints call needs.
    mach_timebase_info_data_t tb_info;
    mach_timebase_info(&tb_info);
    double ms_to_abs_time =
    ((double)tb_info.denom / (double)tb_info.numer) * 1000000;
    
    thread_time_constraint_policy_data_t time_constraints;
    time_constraints.period = kTimeQuantum * ms_to_abs_time;
    time_constraints.computation = kAudioTimeNeeded * ms_to_abs_time;
    time_constraints.constraint = kMaxTimeAllowed * ms_to_abs_time;
    time_constraints.preemptible = 0;
    
    result = thread_policy_set(mach_thread_id,
                               THREAD_TIME_CONSTRAINT_POLICY,
                               (thread_policy_t)&time_constraints,
                               THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (result != KERN_SUCCESS)
    {
        std::cout << "thread_policy_set() failure: " << result;
    }
    
    return;
    
}

    

#else

void setThreadPriority(pthread_t inThread, uint32_t inPriority, bool inIsFixed)
{
    int policy;
    struct sched_param param;

    pthread_getschedparam(inThread, &policy, &param);
    param.sched_priority = sched_get_priority_max(policy);
    pthread_setschedparam(inThread, policy, &param);
}

#endif
