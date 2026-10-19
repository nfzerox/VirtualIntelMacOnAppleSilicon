/*
 * Copyright (c) 1998-2009 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
 
//--------------------------------------------------------------------------------
//
//	File:		AppleUSBAudioCommon.h
//
//	Contains:	Various debug switches for the AppleUSBAudio project
//
//	Technology:	OS X
//
//--------------------------------------------------------------------------------

#ifndef _APPLEUSBAUDIOCOMMON_H
#define _APPLEUSBAUDIOCOMMON_H

#include <libkern/OSTypes.h>

#ifdef DEBUGLOGGING
	#define DEBUG_LEVEL 1
	#ifdef __USE_USBLOG_FOR_AUA_MESSAGES__
		#include <IOKit/usb/IOUSBLog.h>
	#endif
	#ifdef __USE_FIRELOG_FOR_AUA_MESSAGES__
		#include <IOKit/firewire/FireLog.h>
		#ifndef _FIRELOG_H
			#error Must install FireLog to build FireLog builds!
		#endif
	#endif
#endif


//	-----------------------------------------------------------------
#define SoundAssertionMessage( cond, file, line, handler ) \
    "Sound assertion \"" #cond "\" failed in " #file " at line " #line " goto " #handler ""

#define SoundAssertionFailed( cond, file, line, handler ) \
	{debugIOLog( SoundAssertionMessage( cond, file, line, handler )); IOSleep(20);};

//	-----------------------------------------------------------------
#define	FailIf( cond, handler )										\
    if( cond ){														\
        SoundAssertionFailed( cond, __FILE__, __LINE__, handler )	\
        goto handler; 												\
    }

//	-----------------------------------------------------------------
#define	FailWithAction( cond, action, handler )						\
    if( cond ){														\
        SoundAssertionFailed( cond, __FILE__, __LINE__, handler )	\
            { action; }												\
        goto handler; 												\
    }

//	-----------------------------------------------------------------
#define FailMessage(cond)									\
	if (cond) {														\
		SoundAssertionFailed(cond, __FILE__, __LINE__, handler)		\
	}

//  -----------------------------------------------------------------
//
// System Logging or USB Prober Logging
//
//#define sleepTime 20
#define sleepTime 0

#ifdef DEBUGLOGGING /* { */
	#ifdef CONSOLELOGGING /* { */
		#define debugIOLog( message... ) \
			do {IOLog( #message "\n", message ); IOSleep(sleepTime);} while (0)
	#elif defined (__USE_FIREWIRE_KPRINTF_FOR_AUA_MESSAGES__) /* }{ */
			#define debugIOLog( message... ) \
				do { kprintf ( message ); kprintf ( "\n" ); } while (0)
	#elif defined (__USE_FIRELOG_FOR_AUA_MESSAGES__) /* }{ */
			#define debugIOLog( message... ) \
				do { FireLog( message ); FireLog( "\n" ); } while (0)
	#elif defined (__USE_USBLOG_FOR_AUA_MESSAGES__) /* }{ */
		#define debugIOLog( message... ) \
			do {USBLog( DEBUG_LEVEL_DEVELOPMENT, message );} while (0)
	#endif /* } */
#else /* }{ */
	#define debugIOLog( message... ) ;
#endif /* } */

// Following are a list of precompiler variables that affect the way that AppleUSBAudio executes, logs, and compiles.

// kUSBInputRecoveryTimeFraction represents the denominator (with 1 as the numerator) of the fractional multiple of a USB frame we must wait
// before data read via isoc in is available for converting. This must be greater than zero.
#define	kUSBInputRecoveryTimeFraction		4

// kMinimumSyncRefreshInterval is the smallest log base 2 amount of time in ms AppleUSBAudio will honor for a sync endppoint
#define kMinimumSyncRefreshInterval			1

// DEBUGLATENCY enables methods that allow the tracking of how long clipped samples stay in the output buffer prior to DMA
#define DEBUGLATENCY				FALSE

// PRIMEISOCINPUT queues kNumIsocFramesToPrime USB frames to be read and disregarded before attempting to stream samples to CoreAudio
#define PRIMEISOCINPUT				TRUE
#define	kNumUSBFramesToPrime		12

// LOGTIMESTAMPS prints the timestamp in nanoseconds whenever takeTimeStamp it is called
#define LOGTIMESTAMPS				FALSE

// RESETAFTERSLEEP causes a device reset to be issued after waking from sleep for all devices.
#define	RESETAFTERSLEEP				TRUE

// DEBUGANCHORS prints out the last kAnchorsToAccumulate anchors whenever the list fills; used to check anchor accuracy.
#define	DEBUGANCHORS				FALSE
#define	kAnchorsToAccumulate		10

// LOGWALLTIMEPERUSBCYCLE will display mWallTimePerUSBCycle * kExtraPrecision each time updateWallTimePerUSBCycle is executed.

#define LOGWALLTIMEPERUSBCYCLE		FALSE

// LOGDEVICEREQUESTS shows each device request and its result after it has been issued
#define	LOGDEVICEREQUESTS			FALSE

// DEBUGTIMER shows the entry and exit of all USB rate timer functions
#define DEBUGTIMER					FALSE

// DEBUGCONVERT shows the entry and exit of all calls to convertInputSamples
#define	DEBUGCONVERT				FALSE

// STAGGERINTERFACES delays even-numbered streaming interfaces' initHardware by a fixed value (for readability)
#define STAGGERINTERFACES			FALSE

// [rdar://5600254] Log the data cadence for debug purposes.
#define SHOWCADENCE					FALSE

// <rdar://5811247> Poll for the clock status.
#define POLLCLOCKSTATUS				TRUE

#define DEBUGZEROTIME				FALSE
#define DEBUGUSB					FALSE
#define DEBUGISUB					FALSE
#define DEBUGLOADING				FALSE
#define DEBUGTIMESTAMPS				FALSE
#define	DEBUGINPUT					FALSE
#define DEBUGUHCI					FALSE

// The following return codes are used by AppleUSBAudioDevice to detail the status of a format change.
enum 
{
	kAUAFormatChangeNormal				= kIOReturnSuccess,
	kAUAFormatChangeForced,
	kAUAFormatChangeForceFailure,
	kAUAFormatChangeError				= kIOReturnError
	
};

// The following definitions are for lock delay units
enum
{
	kLockDelayUnitMilliseconds			= 1,
	kLockDelayUnitsDecodedPCMSamples	= 2
};


//	<rdar://5131786> Publish property for USB audio plugin.
#define	kIDVendorString				"idVendor"
#define	kIDProductString			"idProduct"

// Sizes in bits for OSNumbers
#define	BITSTOBYTES			8
#define	SIZEINBITS( x )		( BITSTOBYTES * sizeof( x ) )

#endif /* _APPLEUSBAUDIOCOMMON_H */
