/*
 * Copyright (c) 1998-2008 Apple Computer, Inc. All rights reserved.
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
 
#ifndef _APPLEUSBAUDIOCLIP_H
#define _APPLEUSBAUDIOCLIP_H

#include <libkern/OSTypes.h>

#include "iSubTypes.h"	// aml 3.01.02 added to get iSub typdefs

extern "C" {
//	floating point types
typedef	float				Float32;
typedef double				Float64;

typedef struct _sPreviousValues {
    Float32	xl_1;
    Float32	xr_1;
    Float32	xl_2;
    Float32	xr_2;
    Float32	yl_1;
    Float32	yr_1;
    Float32	yl_2;
    Float32	yr_2;
} PreviousValues;

// aml 2.21.02 added structure for 1st order phase compensator
// use in case of 2nd order crossover filter
typedef struct _sPreviousValues1stOrder {
    Float32	xl_1;
    Float32	xr_1;
    Float32	yl_1;
    Float32	yr_1;
} PreviousValues1stOrder;

UInt32 CalculateOffset (UInt64 nanoseconds, UInt32 sampleRate);

IOReturn clipAppleUSBAudioToOutputStreamiSub (const void *mixBuf,
											 void *sampleBuf,
											 PreviousValues *filterState,
                                                                                         // aml 2.21.02 adding extra state for 4th order filter and
                                                                                         // phase compensator
											 PreviousValues *filterState2,
											 PreviousValues *phaseCompState,
											 float *low,
											 float *high,
											 UInt32 firstSampleFrame,
											 UInt32 numSampleFrames,
											 UInt32 sampleRate,
											 const IOAudioStreamFormat *streamFormat,
											 SInt16 * iSubBufferMemory,
											 UInt32 *loopCount,
											 SInt32 *iSubBufferOffset,
											 UInt32 iSubBufferLen,
											 // aml 3.01.02 adding format type
											 iSubAudioFormatType* iSubFormat,
											 float* srcPhase,	// aml 3.5.02
											 float* srcState);	// aml 3.6.02

IOReturn	clipAppleUSBAudioToOutputStream (const void *mixBuf,
											void *sampleBuf,
											UInt32 firstSampleFrame,
											UInt32 numSampleFrames,
											const IOAudioStreamFormat *streamFormat);

IOReturn	convertFromAppleUSBAudioInputStream_NoWrap (const void *sampleBuf,
														void *destBuf,
														UInt32 firstSampleFrame,
														UInt32 numSampleFrames,
														const IOAudioStreamFormat *streamFormat);
}

#endif
