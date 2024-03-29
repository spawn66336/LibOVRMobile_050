/************************************************************************************

PublicHeader:   None
Filename    :   OVR_SensorTimeFilter.cpp
Content     :   Class to filter HMD time and convert it to system time
Created     :   December 20, 2013
Author      :   Michael Antonov
Notes       :

Copyright   :   Copyright 2014 Oculus VR, LLC. All Rights reserved.

************************************************************************************/

#include "OVR_SensorTimeFilter.h"
#include "Kernel/OVR_Log.h"


#include <stdio.h>
#include <math.h>

namespace OVR {  

// Comment out for debug logging to file
//#define OVR_TIMEFILTER_LOG_CODE( code )  code
#define OVR_TIMEFILTER_LOG_CODE( code )

#if defined(OVR_OS_ANDROID)
    #define OVR_TIMEFILTER_LOG_FILENAME "/sdcard/TimeFilterLog.txt"
#elif defined(OVR_OS_WIN32)
    #define OVR_TIMEFILTER_LOG_FILENAME "C:\\TimeFilterLog.txt"
#else
    #define OVR_TIMEFILTER_LOG_FILENAME "TimeFilterLog.txt"
#endif

OVR_TIMEFILTER_LOG_CODE( FILE* pTFLogFile = 0; )


// Ideally, the following would always be true:
//  - NewSampleTime > PrevSample
//  - NewSampleTime < now systemTime
//  - (NewSampleTime - PrevSampleTime) == integration delta, matching
//    HW sample time difference + drift
//
// In practice, these issues affect us:
//  - System thread can be suspended for a while
//  - System de-buffering of recorded samples cause deviceTime to advance up
//    much faster then system time for ~100+ samples
//  - Device (DK1) and system clock granularities are high; this can
//    lead to potentially having estimations in the future
//


// ***** TimerFilter

SensorTimeFilter::SensorTimeFilter(const Settings& settings)
{
    FilterSettings = settings;

    ClockInitialized             = false;
    ClockDelta                   = 0;       
    ClockDeltaDriftPerSecond     = 0;
    ClockDeltaCorrectPerSecond   = 0;
    ClockDeltaCorrectSecondsLeft = 0;
    OldClockDeltaDriftExpire     = 0;

    PrevSampleDeviceTime = 0;
    PrevSystemTime       = 0;
    PrevResult           = 0;

    PastSampleResetTime  = 0;

    MinWindowsCollected  = 0;
    MinWindowDuration    = 0; // assigned later
    MinWindowLastTime    = 0;
    MinWindowSamples     = settings.MinSamples; // Force initialization

    OVR_TIMEFILTER_LOG_CODE( pTFLogFile = fopen(OVR_TIMEFILTER_LOG_FILENAME, "w+"); )
}


double SensorTimeFilter::SampleToSystemTime(double sampleDeviceTime, double systemTime)
{
	//当前系统时间 - 设备时间 + 配置的clockdelta修正值算出当前偏移时间
    double clockDelta      = systemTime - sampleDeviceTime + FilterSettings.ClockDeltaAdjust;
	//此次设备采样时间 - 上次设备采样时间算出 设备采样间隔
    double deviceTimeDelta = sampleDeviceTime - PrevSampleDeviceTime;

   
    // Collect a sample ClockDelta for a "MinimumWindow" or process
    // the window by adjusting drift rates if it's full of samples.
    //   - (deviceTimeDelta < 1.0f) is a corner cases, as it would imply timestamp skip/wrap.

    if (ClockInitialized)
	{//是否为第一次过滤

        // Check logic in case we get bad input - samples in the past.
        // In this case we return prior value for a while, then reset if it keep going.
        if (deviceTimeDelta < 0.0)
        {
            if (PastSampleResetTime < 0.0001)
            {
                PastSampleResetTime = systemTime + FilterSettings.PastSampleResetSeconds;
                return PrevResult;
            }
            else if (systemTime > PastSampleResetTime) 
            {
                OVR_DEBUG_LOG(("SensorTimeFilter - Filtering reset due to samples in the past!\n"));
                initClockSampling(sampleDeviceTime, clockDelta);
                PastSampleResetTime = 0.0;
            }
            else
            {
                return PrevResult;
            }
        }

        // Most common case: Record window sample.
        else if ( (deviceTimeDelta < 1.0f) &&
                  ( (sampleDeviceTime < MinWindowLastTime) ||
                  (MinWindowSamples < FilterSettings.MinSamples) ) )
        {
            // Pick minimum ClockDelta sample.
            if (clockDelta < MinWindowClockDelta)
                MinWindowClockDelta = clockDelta;
            MinWindowSamples++;        
        }
        else
        {
            processFinishedMinWindow(sampleDeviceTime, clockDelta);
        }

        PastSampleResetTime = 0.0;
    }    
    else
    {
        initClockSampling(sampleDeviceTime, clockDelta);
    }
    
    // Clock adjustment for drift.
	//使用当前设备采样间距与时间差漂移速率调整 设备与系统的时间差
    ClockDelta += ClockDeltaDriftPerSecond  * deviceTimeDelta;

    // ClockDelta "nudging" towards last known MinWindowClockDelta.
    if (ClockDeltaCorrectSecondsLeft > 0.000001)
    {
        double correctTimeDelta = deviceTimeDelta;
        if (deviceTimeDelta > ClockDeltaCorrectSecondsLeft)        
            correctTimeDelta = ClockDeltaCorrectSecondsLeft;
        ClockDeltaCorrectSecondsLeft -= correctTimeDelta;
        
        ClockDelta += ClockDeltaCorrectPerSecond  * correctTimeDelta;
    }

    // Our sample time.
    double result =  sampleDeviceTime + ClockDelta;

    OVR_TIMEFILTER_LOG_CODE(
        double savedResult = result;
        )


    // Clamp to ensure that result >= PrevResult.
    // This primarily happens in the very beginning if we are de-queuing system buffer
    // full of samples.
    if (result < PrevResult)
    {
        result = PrevResult;
    }    
    if (result > (systemTime + FilterSettings.FutureClamp))
    {
        result = (systemTime + FilterSettings.FutureClamp);
    }


    OVR_TIMEFILTER_LOG_CODE(

        // Tag lines that were outside desired range, with '<' or '>'.
        char rangeClamp         = ' '; 
        char resultDeltaFar     = ' ';

        if (savedResult > (systemTime + 0.0000001))
            rangeClamp = '>';
        if (savedResult < PrevResult)
            rangeClamp = '<';

        // Tag any result delta outside desired threshold with a '*'.
        if (fabs(deviceTimeDelta - (result - PrevResult)) >= 0.00002)
            resultDeltaFar = '*';
    
        fprintf(pTFLogFile, "Res = %13.7f, dt = % 8.7f,  ClkD = %13.6f  "
                            "sysT = %13.6f, sysDt = %f,  "
                            "sysDiff = % f, devT = %11.6f,  ddevT = %9.6f %c%c\n",
                            result, result - PrevResult, ClockDelta,
                            systemTime, systemTime - PrevSystemTime,
                            -(systemTime - result),  // Negatives in the past, positive > now.
                            sampleDeviceTime,  deviceTimeDelta, rangeClamp, resultDeltaFar);

        ) // OVR_TIMEFILTER_LOG_CODE()
    

    // Record prior values. Useful or logging and clamping.
    PrevSampleDeviceTime = sampleDeviceTime;
    PrevSystemTime       = systemTime;
    PrevResult           = result;

    return result;    
}


void SensorTimeFilter::initClockSampling(double sampleDeviceTime, double clockDelta)
{
    ClockInitialized             = true;
    ClockDelta                   = clockDelta;
    ClockDeltaDriftPerSecond     = 0;
    OldClockDeltaDriftExpire     = 0;
    ClockDeltaCorrectSecondsLeft = 0;
    ClockDeltaCorrectPerSecond   = 0;

    MinWindowsCollected          = 0;
    MinWindowDuration            = 0.25;
    MinWindowClockDelta          = clockDelta;
    MinWindowLastTime            = sampleDeviceTime + MinWindowDuration;
    MinWindowSamples             = 0;
}


void SensorTimeFilter::processFinishedMinWindow(double sampleDeviceTime, double clockDelta)
{
    MinRecord newRec = { MinWindowClockDelta, sampleDeviceTime };
    
    double    clockDeltaDiff    = MinWindowClockDelta - ClockDelta;
    double    absClockDeltaDiff = fabs(clockDeltaDiff);


    // Abrupt change causes Reset of minClockDelta collection.
    //  > 8 ms would a Large jump in a minimum sample, as those are usually stable.
    //  > 1 second intantaneous jump would land us here as well, as that would imply
    //    device being suspended, clock wrap or some other unexpected issue.
    if ((absClockDeltaDiff > 0.008) ||
        ((sampleDeviceTime - PrevSampleDeviceTime) >= 1.0))
    {            
        OVR_TIMEFILTER_LOG_CODE(
            fprintf(pTFLogFile,
                    "\nMinWindow Finished:  %d Samples, MinWindowClockDelta=%f, MW-CD=%f,"
                    "  ** ClockDelta Reset **\n\n",
                    MinWindowSamples, MinWindowClockDelta, MinWindowClockDelta-ClockDelta);
            )

        // Use old collected ClockDeltaDriftPerSecond drift value 
        // up to 1 minute until we collect better samples.
        if (!MinRecords.IsEmpty())
        {
            OldClockDeltaDriftExpire = MinRecords.GetNewest().LastSampleDeviceTime -
                                       MinRecords.GetOldest().LastSampleDeviceTime;
            if (OldClockDeltaDriftExpire > 60.0)
                OldClockDeltaDriftExpire = 60.0;
            OldClockDeltaDriftExpire += sampleDeviceTime;           
        }

        // Jump to new ClockDelta value.
        if ((sampleDeviceTime - PrevSampleDeviceTime) > 1.0)
            ClockDelta = clockDelta;
        else
            ClockDelta = MinWindowClockDelta;

        ClockDeltaCorrectSecondsLeft = 0;
        ClockDeltaCorrectPerSecond   = 0;

        // Reset buffers, we'll be collecting a new MinWindow.
        MinRecords.Reset();
        MinWindowsCollected = 0;
        MinWindowDuration   = 0.25;
        MinWindowSamples    = 0;
    }
    else
    {        
        OVR_ASSERT(MinWindowSamples >= FilterSettings.MinSamples);

        double timeElapsed = 0;

        // If we have older values, use them to update clock drift in 
        // ClockDeltaDriftPerSecond
        if (!MinRecords.IsEmpty() && (sampleDeviceTime > OldClockDeltaDriftExpire))
        {
            MinRecord rec = MinRecords.GetOldest();

            // Compute clock rate of drift.            
            timeElapsed = sampleDeviceTime - rec.LastSampleDeviceTime;

            // Check for divide by zero shouldn't be necessary here, but just be be safe...
            if (timeElapsed > 0.000001)
            {
                ClockDeltaDriftPerSecond = (MinWindowClockDelta - rec.MinClockDelta) / timeElapsed;
                ClockDeltaDriftPerSecond = clampRate(ClockDeltaDriftPerSecond,
                                                      FilterSettings.MaxChangeRate);
            }
            else
            {
                ClockDeltaDriftPerSecond = 0.0;
            }
        }

        MinRecords.AddRecord(newRec);


        // Catchup correction nudges ClockDelta towards MinWindowClockDelta.
        // These are needed because clock drift correction alone is not enough
        // for past accumulated error/high-granularity clock delta changes.
        // The further away we are, the stronger correction we apply.
        // Correction has timeout, as we don't want it to overshoot in case
        // of a large delay between samples.
       
        if (absClockDeltaDiff >= 0.00125)
        {
            // Correct large discrepancy immediately.
            if (absClockDeltaDiff > 0.00175)
            {
                if (clockDeltaDiff > 0)
                    ClockDelta += (clockDeltaDiff - 0.00175);
                else
                    ClockDelta += (clockDeltaDiff + 0.00175);

                clockDeltaDiff = MinWindowClockDelta - ClockDelta;
            }

            ClockDeltaCorrectPerSecond   = clockDeltaDiff;
            ClockDeltaCorrectSecondsLeft = 1.0;
        }            
        else if (absClockDeltaDiff > 0.0005)
        {                
            ClockDeltaCorrectPerSecond   = clockDeltaDiff / 8.0;
            ClockDeltaCorrectSecondsLeft = 8.0;
        }
        else
        {                
            ClockDeltaCorrectPerSecond   = clockDeltaDiff / 15.0;
            ClockDeltaCorrectSecondsLeft = 15.0;
        }

        ClockDeltaCorrectPerSecond = clampRate(ClockDeltaCorrectPerSecond,
                                               FilterSettings.MaxCorrectRate);

        OVR_TIMEFILTER_LOG_CODE(
            fprintf(pTFLogFile,
                    "\nMinWindow Finished:  %d Samples, MinWindowClockDelta=%f, MW-CD=%f,"
                    " tileElapsed=%f, ClockChange=%f, ClockCorrect=%f\n\n",
                    MinWindowSamples, MinWindowClockDelta, MinWindowClockDelta-ClockDelta,
                    timeElapsed, ClockDeltaDriftPerSecond, ClockDeltaCorrectPerSecond);
            )                           
    }

    // New MinClockDelta collection window.
    // Switch to longer duration after first few windows.
    MinWindowsCollected ++;
    if (MinWindowsCollected > 5)
        MinWindowDuration = 0.5; 

    MinWindowClockDelta = clockDelta;
    MinWindowLastTime   = sampleDeviceTime + MinWindowDuration;
    MinWindowSamples    = 0;
}


} // namespace OVR

