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
 
//-----------------------------------------------------------
// iSubTypes.h
// AppleUSBAudio
// 
// Types used in engines, clip routines
//
// Created by Aram Lindahl on Fri Mar 01 2002.
// Copyright (c) 2002 Apple Computer. All rights reserved.
//-----------------------------------------------------------
#ifndef __ISUB_TYPES__
#define __ISUB_TYPES__

// describes the interfaces the iSub supports 
typedef enum {							
    e_iSubAltInterface_8bit_Mono = 1,
    e_iSubAltInterface_8bit_Stereo,
    e_iSubAltInterface_16bit_Mono,
    e_iSubAltInterface_16bit_Stereo,
    e_iSubAltInterface_20bit_Mono,
    e_iSubAltInterface_20bit_Stereo,
} iSubAltInterfaceType;

// describes the iSub audio format
typedef struct _iSubAudioFormat {
    iSubAltInterfaceType	altInterface;
    UInt32 			numChannels;		
    UInt32 			bytesPerSample;		
    UInt32 			outputSampleRate;		
} iSubAudioFormatType;

#endif
