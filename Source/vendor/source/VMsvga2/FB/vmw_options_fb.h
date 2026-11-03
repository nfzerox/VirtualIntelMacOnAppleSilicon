/*
 *  vmw_options_fb.h
 *  VMsvga2
 *
 *  Created by Zenith432 on August 20th 2009.
 *  Copyright 2009-2011 Zenith432. All rights reserved.
 *
 *  Permission is hereby granted, free of charge, to any person
 *  obtaining a copy of this software and associated documentation
 *  files (the "Software"), to deal in the Software without
 *  restriction, including without limitation the rights to use, copy,
 *  modify, merge, publish, distribute, sublicense, and/or sell copies
 *  of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be
 *  included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#ifndef __VMW_OPTIONS_FB_H__
#define __VMW_OPTIONS_FB_H__

#define VMW_OPTION_FB_FIFO_INIT			0x01U
#define VMW_OPTION_FB_REFRESH_TIMER		0x02U
#define VMW_OPTION_FB_ACCEL				0x04U
#define VMW_OPTION_FB_CURSOR_BYPASS_2	0x08U
#define VMW_OPTION_FB_REG_DUMP			0x10U

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned vmw_options_fb;

static inline int checkOptionFB(unsigned mask)
{
	return (vmw_options_fb & mask) != 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* __VMW_OPTIONS_FB_H__ */
