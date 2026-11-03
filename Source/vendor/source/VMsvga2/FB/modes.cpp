/*
 *  modes.cpp
 *  VMsvga2
 *
 *  Created by Zenith432 on July 11th 2009.
 *  Copyright 2009-2011 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include <IOKit/graphics/IOGraphicsTypes.h>
#include "common_fb.h"

DisplayModeEntry const modeList[NUM_DISPLAY_MODES] =
{
	 800,  600, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3 Note: reserved for Custom Mode
	 800,  600, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	1024,  768, kDisplayModeValidFlag | kDisplayModeSafeFlag | kDisplayModeDefaultFlag,	// 4x3
	1152,  720, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 8x5
	1152,  864, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	1152,  900, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 32x25
	1280,  720, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 16x9
	1280,  768, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 5x3
	1280,  800, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 8x5
	1280,  960, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	1280, 1024, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 5x4
	1366,  768, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 16x9
	1376, 1032, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	1400, 1050, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	1440,  900, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 8x5 Note: was 1400x900
	1600,  900, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 16x9
	1600, 1200, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	1680, 1050, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 8x5
	1920, 1080, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 16x9
	1920, 1200, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 8x5
	1920, 1440, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	2048, 1152, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 16x9
	2048, 1536, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3
	2360, 1770, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 4x3 Note: was 2364x1773
	2560, 1440, kDisplayModeValidFlag | kDisplayModeSafeFlag,	// 16x9
	2560, 1600, kDisplayModeValidFlag | kDisplayModeSafeFlag	// 8x5
};
