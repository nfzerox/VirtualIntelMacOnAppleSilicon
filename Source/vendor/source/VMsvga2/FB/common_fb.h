/*
 *  common_fb.h
 *  VMsvga2
 *
 *  Created by Zenith432 on July 4th 2009.
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

#ifndef __VMSVGA2_COMMON_FB_H__
#define __VMSVGA2_COMMON_FB_H__

#define NUM_DISPLAY_MODES 26
#define CUSTOM_MODE_ID 1

#ifdef __cplusplus
extern "C" {
#endif

struct DisplayModeEntry
{
	unsigned width;
	unsigned height;
	unsigned flags;
};

struct CustomModeData
{
	unsigned flags;
	unsigned width;
	unsigned height;
};

extern DisplayModeEntry const modeList[NUM_DISPLAY_MODES] __attribute__((visibility("hidden")));

extern int logLevelFB;

#ifdef __cplusplus
}
#endif

#endif /* __VMSVGA2_COMMON_FB_H__ */
