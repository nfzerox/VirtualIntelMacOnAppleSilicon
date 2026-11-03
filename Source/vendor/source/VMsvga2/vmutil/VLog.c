/*
 *  VLog.c
 *  vmutil
 *
 *  Created by Zenith432 on October 13th 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
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

#include <stdarg.h>
#include <string.h>
#ifdef KERNEL
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#else
#include <stdio.h>
#endif
#include "VLog.h"

#define VLOG_BUF_SIZE 256

__attribute__((visibility("hidden"), format(printf, 2, 3)))
void VLog(char const* prefix_str, char const* fmt, ...)
{
	va_list ap;
	size_t l;
	char print_buf[VLOG_BUF_SIZE];

	va_start(ap, fmt);
	l = strlcpy(&print_buf[0], "log ", sizeof print_buf);
	l += strlcpy(&print_buf[l], prefix_str, sizeof print_buf - l);
	vsnprintf(&print_buf[l], sizeof print_buf - l, fmt, ap);
	va_end(ap);
#if defined(KERNEL) && defined(VLOG_LOCAL)
	IOLog("%s", &print_buf[4]);
#endif
	VMLog_SendString(&print_buf[0]);
}
