/*
 * SendString.c
 * vmutil
 *
 * Created by Zenith432 on August 15th 2009.
 * Copyright 2009-2010 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2007-2009 VMware, Inc.  All rights reserved.
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

#include "VLog.h"

#define BDOOR_MAGIC 0x564D5868U
#define BDOOR_PORT 0x5658U
#define BDOORHB_PORT 0x5659U
#define PROTO 0x49435052U
#define BDOOR_CMD_GETSCREENSIZE 15U
#define BDOOR_CMD_MESSAGE 30U
#define BDOORHB_CMD_MESSAGE 0U

#define BACKDOOR_VARS() \
	unsigned eax = 0U, edx = 0U, edi = 0U; \
	unsigned long ebx = 0U, ecx = 0U, esi = 0U;

#define BACKDOOR_ASM(op, port) \
	{ \
		eax = BDOOR_MAGIC; \
		edx = (edx & 0xFFFF0000U) | port; \
		__asm__ volatile (op : "+a" (eax), "+b" (ebx), \
			"+c" (ecx), "+d" (edx), "+S" (esi), "+D" (edi)); \
	}

#define BACKDOOR_ASM_IN()       BACKDOOR_ASM("in %%dx, %0", BDOOR_PORT)
#define BACKDOOR_ASM_HB_OUT()   BACKDOOR_ASM("cld; rep outsb", BDOORHB_PORT)
#define BACKDOOR_ASM_HB_IN()    BACKDOOR_ASM("cld; rep insb", BDOORHB_PORT)

__attribute__((visibility("hidden")))
char VMLog_SendString(char const* str)
{
	unsigned long size;
	unsigned short channel_id;

	if (!str)
		return 0;

	BACKDOOR_VARS()

	ecx = BDOOR_CMD_MESSAGE | 0x00000000U;  /* Open */
	ebx = PROTO;
	BACKDOOR_ASM_IN()

	if (!(ecx & 0x00010000U)) {
		return 0;
	}

	channel_id = edx >> 16;
	for (size = 0U; str[size]; ++size);

	ecx = BDOOR_CMD_MESSAGE | 0x00010000U;  /* Send size */
	ebx = size;
	edx = channel_id << 16;
	esi = edi = 0U;
	BACKDOOR_ASM_IN()

	/* We only support the high-bandwidth backdoor port. */
	if (((ecx >> 16) & 0x0081U) != 0x0081U) {
		return 0;
	}

	ebx = 0x00010000U | BDOORHB_CMD_MESSAGE;
	ecx = size;
	edx = channel_id << 16;
	esi = (unsigned long) str;
	edi = 0U;
	BACKDOOR_ASM_HB_OUT()

	/* Success? */
	if (!(ebx & 0x00010000U)) {
		return 0;
	}

	ecx = BDOOR_CMD_MESSAGE | 0x00060000U;  /* Close */
	ebx = 0U;
	edx = channel_id << 16;
	esi = edi = 0U;
	BACKDOOR_ASM_IN()

	return 1;
}

__attribute__((visibility("hidden")))
void VMGetScreenSize(unsigned short* width, unsigned short* height)
{
	BACKDOOR_VARS()
	ecx = BDOOR_CMD_GETSCREENSIZE;
	BACKDOOR_ASM_IN()
	*width  = (unsigned short) (eax >> (8U * sizeof(unsigned short)));
	*height = (unsigned short) (eax & ((1U << (8U * sizeof(unsigned short))) - 1U));
}
