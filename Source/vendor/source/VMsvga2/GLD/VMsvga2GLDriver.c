/*
 *  VMsvga2GLDriver.c
 *  VMsvga2GLDriver
 *
 *  Created by Zenith432 on December 6th 2009.
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

#include <dlfcn.h>
#include "VMsvga2GLDriver.h"
#include "EntryPointNames.h"
#include "VLog.h"

#if LOGGING_LEVEL >= 1
#define GLDLog(log_level, fmt, ...) do { if (log_level <= logLevel) VLog("GLD: ", fmt, ##__VA_ARGS__); } while(false)
#else
#define GLDLog(log_level, fmt, ...)
#endif

#define GLD_DEFINE_GENERIC(name, index) \
	GLDReturn name(void* arg0, void* arg1, void* arg2, void* arg3, void* arg4, void* arg5) \
	{ \
		GLDLog(2, "%s(%p, %p, %p, %p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2, arg3, arg4, arg5); \
		if (bndl_ptrs[bndl_index][index]) return bndl_ptrs[bndl_index][index](arg0, arg1, arg2, arg3, arg4, arg5); \
		return -1; \
	}

static __attribute__((used)) char const copyright[] = "Copyright 2009-2010 Zenith432";

static char const BNDL1[] = "/System/Library/Extensions/AppleIntelGMA950GLDriver.bundle/Contents/MacOS/AppleIntelGMA950GLDriver";
static char const BNDL2[] = "/System/Library/Frameworks/OpenGL.framework/Resources/GLRendererFloat.bundle/GLRendererFloat";
//static char const BNDL3[] = "/System/Library/Extensions/VMsvga2GLDriver.bundle/Contents/MacOS/GLRendererFloat";

static void* bndl_handle[2];

static GLD_GENERIC_FUNC bndl_ptrs[2][NUM_ENTRIES];

#if LOGGING_LEVEL >= 1
static int logLevel = 5;
#endif

static int bndl_index;

void gldInitializeLibrary(int* psvc, void* arg1, int GLDisplayMask, void* arg3, void* arg4)
{
	void (*addr)(int*, void*, int, void*, void*);
	int i;

	GLDLog(2, "%s(%p, %p, %d, %p, %p)\n", __FUNCTION__, psvc, arg1, GLDisplayMask, arg3, arg4);

	bndl_handle[0] = dlopen(BNDL1, 0);
	if (bndl_handle[0]) {
		addr = (typeof(addr)) dlsym(bndl_handle[0], "gldInitializeLibrary");
		if (addr) {
			for (i = 0; i < NUM_ENTRIES; ++i)
				bndl_ptrs[0][i] = (GLD_GENERIC_FUNC) dlsym(bndl_handle[0], entry_point_names[i]);
			addr(psvc, arg1, GLDisplayMask, arg3, arg4);
		} else {
			dlclose(bndl_handle[0]);
			bndl_handle[0] = 0;
		}
	}
	bndl_handle[1] = dlopen(BNDL2, 0);
	if (bndl_handle[1])
		for (i = 0; i < NUM_ENTRIES; ++i)
			bndl_ptrs[1][i] = (GLD_GENERIC_FUNC) dlsym(bndl_handle[1], entry_point_names[i]);
	bndl_index = 1;
	// TBD: find out how to signal an error
}

void gldTerminateLibrary(void)
{
	void (*addr)(void);

	GLDLog(2, "%s()\n", __FUNCTION__);

	if (bndl_handle[1])
		dlclose(bndl_handle[1]);
	if (bndl_handle[0]) {
		addr = (typeof(addr)) dlsym(bndl_handle[0], "gldTerminateLibrary");
		if (addr)
			addr();
		dlclose(bndl_handle[0]);
	}
}

_Bool gldGetVersion(int* arg0, int* arg1, int* arg2, int* arg3)
{
	_Bool (*addr)(int*, int*, int*, int*);
	_Bool rc = 0;

	GLDLog(2, "%s(%p, %p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2, arg3);

	addr = (typeof(addr)) bndl_ptrs[0][0];
	if (addr)
		rc = addr(arg0, arg1, arg2, arg3);
	if (rc)
		GLDLog(2, "  %s returns (%d, %d, %d, 0x%x)\n", __FUNCTION__, *arg0, *arg1, *arg2, *arg3);
	return rc;
}

GLDReturn gldGetRendererInfo(void* struct_out, int GLDisplayMask)
{
	GLDReturn (*addr)(void*, int);
	int i, rc;
	unsigned* p;

	GLDLog(2, "%s(%p, %d)\n", __FUNCTION__, struct_out, GLDisplayMask);

	addr = (typeof(addr)) bndl_ptrs[0][1];
	if (addr) {
		rc = addr(struct_out, GLDisplayMask);
		GLDLog(2, "  %s: returns %d\n", __FUNCTION__, rc);
		if (rc)
			return rc;
		p = (unsigned *) (((long*) struct_out) + 1);
#if 0
		*p = ((*p) & 0xFFFF0000U) | 0x4000U;
		p[1] = 0x17CDU;
		p[17] = 64;
		p[19] = 64;
#endif
		for (i = 0; i < 32; ++i)
			GLDLog(2, "  %s: [%d] == 0x%x\n", __FUNCTION__, i, p[i]);
		return 0;
	}
	return -1;
}

GLDReturn gldChoosePixelFormat(void** struct_out, int* attributes)
{
	GLDReturn (*addr)(void**, int*);
	int i, rc;
	unsigned* p;

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, struct_out, attributes);

	for (i = 0; attributes[i] != 0; ++i)
		GLDLog(2, "  %s: attribute %d\n", __FUNCTION__, attributes[i]);
	addr = (typeof(addr)) bndl_ptrs[0][2];
	if (addr) {
		rc = addr(struct_out, attributes);
		GLDLog(2, "  %s: returns %d, struct_out is %p\n", __FUNCTION__, rc, *struct_out);
		if (rc != 0 || *struct_out == 0)
			return rc;
		p = (unsigned *) (((long*) *struct_out) + 1);
#if 0
		*p = ((*p) & 0xFFFF0000U) | 0x4000U;
		p[1] = 0x501U;
#endif
		for (i = 0; i < 12; ++i)
			GLDLog(2, "  %s: [%d] == 0x%x\n", __FUNCTION__, i, p[i]);
		return 0;
	}
	return -1;
}

GLDReturn gldDestroyPixelFormat(void* struct_in)
{
	GLDReturn (*addr)(void*);

	GLDLog(2, "%s(%p)\n", __FUNCTION__, struct_in);

	addr = (typeof(addr)) bndl_ptrs[0][3];
	if (addr)
		return addr(struct_in);
	return -1;
}

GLDReturn gldCreateShared(void* arg0, int GLDisplayMask, long arg2)
{
	GLDReturn (*addr)(void*, int, long);

	GLDLog(2, "%s(%p, %d, %ld)\n", __FUNCTION__, arg0, GLDisplayMask, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][4];
	if (addr)
		return addr(arg0, GLDisplayMask, arg2);
	return -1;
}

GLDReturn gldDestroyShared(void* struct_in)
{
	GLDReturn (*addr)(void*);

	GLDLog(2, "%s(%p)\n", __FUNCTION__, struct_in);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][5];
	if (addr)
		return addr(struct_in);
	return -1;
}

GLD_DEFINE_GENERIC(gldCreateContext, 6)

GLDReturn gldReclaimContext(void* struct_in)
{
	GLDReturn (*addr)(void*);

	GLDLog(2, "%s(%p)\n", __FUNCTION__, struct_in);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][7];
	if (addr)
		return addr(struct_in);
	return -1;
}

GLDReturn gldDestroyContext(void* struct_in)
{
	GLDReturn (*addr)(void*);

	GLDLog(2, "%s(%p)\n", __FUNCTION__, struct_in);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][8];
	if (addr)
		return addr(struct_in);
	return -1;
}

GLDReturn gldAttachDrawable(void* context, int surface_type, void* arg2, void* arg3)
{
	GLDReturn (*addr)(void*, int, void*, void*);

	GLDLog(2, "%s(%p, %d, %p, %p)\n", __FUNCTION__, context, surface_type, arg2, arg3);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][9];
	if (addr)
		return addr(context, surface_type, arg2, arg3);
	return -1;
}

GLDReturn gldInitDispatch(void* arg0, void* arg1, void* arg2)
{
	GLDReturn (*addr)(void*, void*, void*);

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][10];
	if (addr)
		return addr(arg0, arg1, arg2);
	return -1;
}

GLDReturn gldUpdateDispatch(void* arg0, void* arg1, void* arg2)
{
	GLDReturn (*addr)(void*, void*, void*);

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][11];
	if (addr)
		return addr(arg0, arg1, arg2);
	return -1;
}

char const* gldGetString(int GLDisplayMask, int string_code)
{
	char const* (*addr)(int, int);
	char const* r = 0;

	GLDLog(2, "%s(%d, 0x%x)\n", __FUNCTION__, GLDisplayMask, string_code);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][12];
	if (addr) {
		r = addr(GLDisplayMask, string_code);
		if (r)
			GLDLog(2, "  %s returns %s\n", __FUNCTION__, r);
	}
	switch (string_code) {
		case 0x1F00:
			r = "Zenith432";
			break;
		case 0x1F01:
			r = "VMware SVGA II OpenGL Engine";
			break;
		case 0x1F04:
			r = "VMsvga2GLDriver";
			break;
	}
	return r;
}

void gldGetError(void* arg0)
{
	void (*addr)(void*);

	GLDLog(2, "%s(%p)\n", __FUNCTION__, arg0);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][13];
	if (addr)
		addr(arg0);
}

GLDReturn gldSetInteger(void* arg0, int arg1, void* arg2)
{
	GLDReturn (*addr)(void*, int, void*);

	GLDLog(2, "%s(%p, %d, %p)\n", __FUNCTION__, arg0, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][14];
	if (addr)
		return addr(arg0, arg1, arg2);
	return -1;
}

GLDReturn gldGetInteger(void* arg0, int arg1, void* arg2)
{
	GLDReturn (*addr)(void*, int, void*);

	GLDLog(2, "%s(%p, %d, %p)\n", __FUNCTION__, arg0, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][15];
	if (addr)
		return addr(arg0, arg1, arg2);
	return -1;
}

GLD_DEFINE_GENERIC(gldFlush, 16)
GLD_DEFINE_GENERIC(gldFinish, 17)
GLD_DEFINE_GENERIC(gldTestObject, 18)
GLD_DEFINE_GENERIC(gldFlushObject, 19)
GLD_DEFINE_GENERIC(gldFinishObject, 20)
GLD_DEFINE_GENERIC(gldWaitObject, 21)

GLDReturn gldCreateTexture(void* arg0, void* arg1, void* arg2)
{
	GLDReturn (*addr)(void*, void*, void*);

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][22];
	if (addr)
		return addr(arg0, arg1, arg2);
	return -1;
}

GLD_DEFINE_GENERIC(gldIsTextureResident, 23)
GLD_DEFINE_GENERIC(gldModifyTexture, 24)
GLD_DEFINE_GENERIC(gldLoadTexture, 25)

void gldUnbindTexture(void* arg0, void* arg1)
{
	void (*addr)(void*, void*);

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][26];
	if (addr)
		addr(arg0, arg1);
}

GLD_DEFINE_GENERIC(gldReclaimTexture, 27)

void gldDestroyTexture(void* arg0, void* arg1)
{
	void (*addr)(void*, void*);

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][28];
	if (addr)
		addr(arg0, arg1);
}

GLD_DEFINE_GENERIC(gldCreateTextureLevel, 29)
GLD_DEFINE_GENERIC(gldGetTextureLevelInfo, 30)
GLD_DEFINE_GENERIC(gldGetTextureLevelImage, 31)
GLD_DEFINE_GENERIC(gldModifyTextureLevel, 32)
GLD_DEFINE_GENERIC(gldDestroyTextureLevel, 33)
GLD_DEFINE_GENERIC(gldCreateBuffer, 34)
GLD_DEFINE_GENERIC(gldLoadBuffer, 35)
GLD_DEFINE_GENERIC(gldFlushBuffer, 36)
GLD_DEFINE_GENERIC(gldPageoffBuffer, 37)
GLD_DEFINE_GENERIC(gldUnbindBuffer, 38)
GLD_DEFINE_GENERIC(gldReclaimBuffer, 39)
GLD_DEFINE_GENERIC(gldDestroyBuffer, 40)
GLD_DEFINE_GENERIC(gldGetMemoryPlugin, 41)
GLD_DEFINE_GENERIC(gldSetMemoryPlugin, 42)
GLD_DEFINE_GENERIC(gldTestMemoryPlugin, 43)
GLD_DEFINE_GENERIC(gldFlushMemoryPlugin, 44)
GLD_DEFINE_GENERIC(gldDestroyMemoryPlugin, 45)
GLD_DEFINE_GENERIC(gldCreateFramebuffer, 46)
GLD_DEFINE_GENERIC(gldUnbindFramebuffer, 47)
GLD_DEFINE_GENERIC(gldReclaimFramebuffer, 48)
GLD_DEFINE_GENERIC(gldDestroyFramebuffer, 49)

GLDReturn gldCreatePipelineProgram(void* arg0, void* arg1, void* arg2)
{
	GLDReturn (*addr)(void*, void*, void*);

	GLDLog(2, "%s(%p, %p, %p)\n", __FUNCTION__, arg0, arg1, arg2);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][50];
	if (addr)
		return addr(arg0, arg1, arg2);
	return -1;
}

GLD_DEFINE_GENERIC(gldGetPipelineProgramInfo, 51)
GLD_DEFINE_GENERIC(gldModifyPipelineProgram, 52)

GLDReturn gldUnbindPipelineProgram(void* arg0, void* arg1)
{
	GLDReturn (*addr)(void*, void*);

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][53];
	if (addr)
		return addr(arg0, arg1);
	return -1;
}

GLDReturn gldDestroyPipelineProgram(void* arg0, void* arg1)
{
	GLDReturn (*addr)(void*, void*);

	GLDLog(2, "%s(%p, %p)\n", __FUNCTION__, arg0, arg1);

	addr = (typeof(addr)) bndl_ptrs[bndl_index][54];
	if (addr)
		return addr(arg0, arg1);
	return -1;
}

GLD_DEFINE_GENERIC(gldCreateProgram, 55)
GLD_DEFINE_GENERIC(gldDestroyProgram, 56)

GLDReturn gldCreateVertexArray(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return 0;
}

GLD_DEFINE_GENERIC(gldModifyVertexArray, 58)
GLD_DEFINE_GENERIC(gldFlushVertexArray, 59)
GLD_DEFINE_GENERIC(gldUnbindVertexArray, 60)
GLD_DEFINE_GENERIC(gldReclaimVertexArray, 61)

GLDReturn gldDestroyVertexArray(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return 0;
}

GLD_DEFINE_GENERIC(gldCreateFence, 63)
GLD_DEFINE_GENERIC(gldDestroyFence, 64)
GLD_DEFINE_GENERIC(gldCreateQuery, 65)
GLD_DEFINE_GENERIC(gldGetQueryInfo, 66)
GLD_DEFINE_GENERIC(gldDestroyQuery, 67)
GLD_DEFINE_GENERIC(gldObjectPurgeable, 68)
GLD_DEFINE_GENERIC(gldObjectUnpurgeable, 69)

GLDReturn gldCreateComputeContext(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return -1;
}

GLDReturn gldDestroyComputeContext(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return -1;
}

GLDReturn gldLoadHostBuffer(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return 0;
}

GLDReturn gldSyncBufferObject(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return 0;
}

GLDReturn gldSyncTexture(void)
{
	GLDLog(2, "%s()\n", __FUNCTION__);
	return 0;
}
