/*
 *  VMsvga2GA.cpp
 *  VMsvga2GA
 *
 *  Created by Zenith432 on August 6th 2009.
 *  Copyright 2009-2011 Zenith432. All rights reserved.
 *  Portions Copyright (c) Apple Computer, Inc.
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

#include <CoreFoundation/CFNumber.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <IOKit/graphics/IOGraphicsInterface.h>

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#else
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#endif
#include "VMsvga2GA.h"
#include "BlitHelper.h"
#include "UCMethods.h"
#include "VLog.h"
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <limits.h>
#include "gli-cpu-copy.h"

#define kVMGAFactoryID \
	(CFUUIDGetConstantUUIDWithBytes(NULL, 0x03, 0x46, 0x3B, 0x45, 0x6F, 0xDD, 0x47, 0x49, 0xB6, 0xB7, 0x15, 0xEB, 0x76, 0xBA, 0xA2, 0x2F))

#if LOGGING_LEVEL >= 1
#define GALog(log_level, fmt, ...) do { if (log_level <= logLevel) VLog("GA: ", fmt, ##__VA_ARGS__); } while(false)
#else
#define GALog(log_level, fmt, ...)
#endif

#define FMT_D(x) static_cast<int>(x)
#define FMT_U(x) static_cast<unsigned>(x)

static __attribute__((used)) char const copyright[] = "Copyright 2009-2011 Zenith432";

static IOGraphicsAcceleratorInterface ga;
static int ga_initialized = 0;
static io_connect_t this_ga_ctx;
static struct _GAType* this_ga;
#if LOGGING_LEVEL >= 1
static int logLevel = LOGGING_LEVEL;
#endif

typedef struct _GAType {	// GeForceGA size 176
	IOGraphicsAcceleratorInterface* _interface;
	CFUUIDRef _factoryID;
	ULONG _refCount;
	io_connect_t _context;			// offset 0xC
	io_service_t _accelerator;		// offset 0x10
									// offset 0x14 - 0x44
	UInt32 _buffer[11];				// offset 0x48
									// offset 0x74 - 0x78
	UInt32 _framebufferIndex;		// offset 0x7C
	UInt32 _config_val_1;			// offset 0x80
	void* _surface;					// offset 0x84
									// offset 0x88
	UInt32 _config_Ex_1;			// offset 0x8C
	UInt32 _config_Ex_2;			// offset 0x90
	UInt32 _config_val_2;			// offset 0x94
} GAType;

typedef struct _SurfaceInfo {
	UInt32* d0;
	vm_address_t addr;
	uintptr_t cgsSurfaceID;
	UInt32 d2[11];
} SurfaceInfo;

/* Never load or inject a renderer into a 2D-only process. Resolve only the
 * already-loaded companion, with a per-thread positive/negative cache. A
 * later renderer load invalidates a negative lookup via dyld image count. */
struct VIMGLIHooks {uint32_t image_count;bool searched;VIMGLICPUCopyProc notify;VIMGLIGPUCopyProc copy;};
#include "period-thread-storage.h"
#if VIM_PERIOD_PTHREAD_TLS
VIM_DEFINE_PERIOD_STORAGE(VIMGLIHooks,vim_ga_hooks)
#endif
static VIMGLIHooks* vmGLIHooks()
{
#if VIM_PERIOD_PTHREAD_TLS
	VIMGLIHooks& hooks=*vim_ga_hooks_storage();
#else
	static __thread VIMGLIHooks hooks;
#endif
	if (!hooks.notify && (!hooks.searched || hooks.image_count != _dyld_image_count())) {
		hooks.image_count = _dyld_image_count(); hooks.searched = true;
		const char* resources = getenv("GL_RESOURCES");
		if (!resources) resources = "/System/Library/Frameworks/OpenGL.framework/Resources";
		char path[PATH_MAX];
		int length = snprintf(path, sizeof path, "%s/GLEngine.bundle/GLEngine", resources);
		if (length > 0 && static_cast<size_t>(length) < sizeof path) {
			void* engine = dlopen(path, RTLD_LAZY | RTLD_LOCAL | RTLD_NOLOAD);
			if (engine) {
				hooks.notify = reinterpret_cast<VIMGLICPUCopyProc>(dlsym(engine, VIM_GLI_CPU_COPY_SYMBOL));
				hooks.copy = reinterpret_cast<VIMGLIGPUCopyProc>(dlsym(engine, VIM_GLI_GPU_COPY_SYMBOL));
				if (!hooks.notify && !hooks.copy) dlclose(engine); // Positive hooks retain their engine.
			}
		}
	}
	return &hooks;
}
static IOReturn vmNotifyCPUSurfaceCopy(GAType* me,const IOAccelDeviceRegion* region,int32_t x,int32_t y)
{
	if (!me->_surface) return kIOReturnSuccess;
	VIMGLICPUCopyProc notify=vmGLIHooks()->notify;
	if (!notify) return kIOReturnSuccess;
	IOBlitSurface* surface = static_cast<IOBlitSurface*>(me->_surface);
	SurfaceInfo* info = reinterpret_cast<SurfaceInfo*>(surface->interfaceRef);
	if (!info || info->cgsSurfaceID > UINT32_MAX) return kIOReturnBadArgument;
	return notify(static_cast<uint32_t>(info->cgsSurfaceID), region, x, y);
}
#ifdef __cplusplus
extern "C" {
#endif

#if LOGGING_LEVEL >= 1
static void printSurface(int log_level, IOBlitSurface const* surface);
#endif
static bool vmDecodePixelFormat(IOFourCharCode pixelFormat, ...);
static GAType* _allocGAType(CFUUIDRef factoryID);
static void _deallocGAType(GAType *myInstance);
static void _buildGAFTbl();
void* VMsvga2GAFactory(CFAllocatorRef allocator, CFUUIDRef typeID);

#ifdef __cplusplus
}
#endif

#if LOGGING_LEVEL >= 1
static void printSurface(int log_level, IOBlitSurface const* surface)
{
	size_t i;
	unsigned v;

	if (!surface || logLevel < log_level)
		return;
	for (i = 0; i < sizeof(IOBlitSurface) / sizeof(UInt32); ++i) {
		v = reinterpret_cast<UInt32 const*>(surface)[i];
		GALog(log_level, "%s:   surface[%lu] == %#x\n", __FUNCTION__, i, v);
	}
}
#endif

static bool vmDecodePixelFormat(IOFourCharCode pixelFormat, ...)
{
	return true;
}

#pragma mark -
#pragma mark IUnknown
#pragma mark -

static HRESULT vmQueryInterface(void *myInstance, REFIID iid, LPVOID *ppv)
{
	GAType* me = static_cast<GAType*>(myInstance);

	CFUUIDRef interfaceID = CFUUIDCreateFromUUIDBytes(NULL, iid);
	if(CFEqual(interfaceID, kIOGraphicsAcceleratorInterfaceID) ||
	   CFEqual(interfaceID, kIOCFPlugInInterfaceID) ||
	   CFEqual(interfaceID, IUnknownUUID)) {
		if (me && me->_interface)
			me->_interface->AddRef(myInstance);
		if (ppv)
			*ppv = myInstance;
		CFRelease(interfaceID);
		return S_OK;
    }
	if (ppv)
	   *ppv = NULL;
	CFRelease(interfaceID);
	return E_NOINTERFACE;
}

static ULONG vmAddRef(void *myInstance)
{
	GAType* me = static_cast<GAType*>(myInstance);

	if (!me)
		return 0;
	++me->_refCount;
	return me->_refCount;
}

static ULONG vmRelease(void *myInstance)
{
	GAType* me = static_cast<GAType*>(myInstance);

	if (!me)
		return 0;
	--me->_refCount;
	if (!me->_refCount) {
		_deallocGAType(me);
		return 0;
    }
	return me->_refCount;
}

#pragma mark -
#pragma mark IOCFPlugin
#pragma mark -

static IOReturn vmProbe(void* myInstance, CFDictionaryRef propertyTable, io_service_t service, SInt32* order)
{
	if (order)
	   *order = 2000;
	return kIOReturnSuccess;
}

static IOReturn vmStart(void* myInstance, CFDictionaryRef propertyTable, io_service_t service)
{
	GAType* me = static_cast<GAType*>(myInstance);
	IOReturn rc;
	io_service_t accelerator = 0;
	io_connect_t context = 0;
	uint32_t output_cnt;
	uint64_t output[2];
	UInt32 input_struct;
	UInt32 output_struct;
	size_t output_struct_cnt;

	if (!me || !service)
		return kIOReturnBadArgument;
	rc = IOAccelFindAccelerator(service, &accelerator, &me->_framebufferIndex);
	if (rc != kIOReturnSuccess)
		return rc;
	rc = IOServiceOpen(accelerator, mach_task_self(), 2 /* 2D Context */, &context);
	if (rc != kIOReturnSuccess)
		goto cleanup;
	me->_accelerator = accelerator;
	me->_context = context;
	output_cnt = 2;
	rc = IOConnectCallMethod(context,
							 kIOVM2DGetConfig,
							 0, 0,
							 0, 0,
							 &output[0], &output_cnt,
							 0, 0);
	me->_config_val_1 = static_cast<UInt32>(output[0]);
	if (rc != kIOReturnSuccess)
		goto cleanup;
#if LOGGING_LEVEL >= 1
	if (static_cast<int>(output[1]) >= 0)			// Added
		logLevel = static_cast<int>(output[1]);
#endif
	input_struct = 2;
	output_struct_cnt = sizeof output_struct;
	rc = IOConnectCallMethod(context,
							 kIOVM2DReadConfigs,
							 0, 0,
							 &input_struct, sizeof input_struct,
							 0, 0,
							 &output_struct, &output_struct_cnt);
	if (rc != kIOReturnSuccess)
		goto cleanup;
	me->_config_val_2 = output_struct;
	rc = vmReset(me, 0);
	if (rc != kIOReturnSuccess)
		goto cleanup;
	useAccelUpdates(context, 1);
	goto good_exit;

cleanup:
	if (context)
		IOServiceClose(context);
	if (accelerator)
		IOObjectRelease(accelerator);
good_exit:
	this_ga = me;
	this_ga_ctx = me->_context;
	return rc;
}

static IOReturn vmStop(void* myInstance)
{
	GAType* me = static_cast<GAType*>(myInstance);

	if (!me)
		return kIOReturnBadArgument;
	vmFlush(me, 0);
	this_ga = 0;
	this_ga_ctx = 0;
	if (!me->_accelerator)
		return kIOReturnSuccess;
	if (me->_context)
		IOServiceClose(me->_context);
	IOObjectRelease(me->_accelerator);
	me->_accelerator = 0;
	me->_context = 0;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark IOGraphicsAccelerator
#pragma mark -

/*
 * Note: All reference to GeForceGA.plugin are for version
 *   GeForceGA 1.5.48.6 (17.5.7f10) from OS 10.5.8
 */

static IOReturn vmReset(void* myInstance, IOOptionBits options)
{
	GAType* me = static_cast<GAType*>(myInstance);

	GALog(2, "%s(%p, %#x)\n", __FUNCTION__, myInstance, FMT_U(options));

	if (!me)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	/*
	 * TBD GeForceGA 0x1920 - 0x1B07
	 */
	return kIOReturnSuccess;
}

static IOReturn vmGetCapabilities(void* myInstance, FourCharCode select, CFTypeRef* capabilities)
{
	GAType* me = static_cast<GAType*>(myInstance);
	uint64_t input;

	GALog(2, "%s(%p, %#x, %p)\n", __FUNCTION__, myInstance, FMT_U(select), capabilities);

#ifdef SUPPORT_CGLS
	if (select == IO_FOUR_CHAR_CODE('cgls') ||
		select == kIO32BGRAPixelFormat) {
		*capabilities = kCFBooleanTrue;
		return kIOReturnSuccess;
	}
#endif
	if (select == IO_FOUR_CHAR_CODE('smvl')) {
		if (!me)
			return kIOReturnBadArgument;
		if (!me->_context)
			return kIOReturnNotReady;
		input = reinterpret_cast<uint64_t>(*capabilities);
		return IOConnectCallMethod(me->_context, kIOVM2DSetMacrovision,
								   &input, 1,
								   0, 0,
								   0, 0,
								   0, 0);
	}
	return kIOReturnUnsupported;
}

static IOReturn vmFlush(void* myInstance, IOOptionBits options)
{
	GALog(3, "%s(%p, %#x)\n", __FUNCTION__, myInstance, FMT_U(options));

#if 0
	/*
	 * GeForceGA code
	 */
	GAType* me = static_cast<GAType*>(myInstance);
	IOReturn rc;

	if (!me)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	// edx = me->offset0x18
	if (me->offset0x78 == edx + 0x80U)
		return kIOReturnSuccess;
	if ((options & kIOBlitFlushWithSwap) != 0 &&
		me->_surface != 0) {
		// TBD GeForceGA 0x2C19 - 0x2C34
	}
	// dword ptr [ebp-0x20] = edx
	if (_config_val_2 <= 63) {
		// TBD GeForceGA 0x2CD0 - 0x2CE2
	} else {
		// TBD GeForceGA 0x2C44 - 0x2C79
	}
	rc = IOConnectMapMemory(me->_context, 0,
							mach_task_self(), &ebp_less_0x20,
							&ebp_less_0x1C, 1);
	if (rc != kIOReturnSuccess) {
		me->offset0x18 = 0;
		me->offset0x74 = 0;
		me->offset0x78 = 0;
	} else {
		// TBD GeForceGA 0x2CF0 - 0x2D78
	}
#endif

	return kIOReturnSuccess;
}

static IOReturn vmSynchronize(void* myInstance, UInt32 options, UInt32 x, UInt32 y, UInt32 w, UInt32 h)
{
	GAType* me = static_cast<GAType*>(myInstance);
	UInt32 rect[4];

	/*
	 * Note: GeForceGA code does nothing
	 */

	GALog(3, "%s(%p, %#x, %u, %u, %u, %u)\n", __FUNCTION__,
		  myInstance, FMT_U(options), FMT_U(x), FMT_U(y), FMT_U(w), FMT_U(h));

	if (!(options & kIOBlitSynchronizeFlushHostWrites))
		return kIOReturnSuccess;
	if (!me)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	if (w <= x || h <= y)
		return kIOReturnBadArgument;
	rect[0] = x;
	rect[1] = y;
	rect[2] = w - x;
	rect[3] = h - y;
	return UpdateFramebuffer(me->_context, &rect[0]);
}

static IOReturn vmGetBeamPosition(void* myInstance, IOOptionBits options, SInt32* position)
{
	GALog(3, "%s(%p, %#x, %p)\n", __FUNCTION__, myInstance, FMT_U(options), position);

	if (position)
		*position = 0;
	return kIOReturnSuccess;

#if 0
	/*
	 * GeForceGA code
	 */
	GAType* me = static_cast<GAType*>(myInstance);
	IOReturn rc;
	UInt32 struct_in[2];
	UInt32 struct_out[2];
	size_t struct_out_cnt;

	if (!me)
		return kIOReturnBadArgument;
	if (me->_context)
		return kIOReturnNotReady;
	struct_out_cnt = sizeof struct_out;
	struct_in[0] = 144;
	struct_in[1] = me->_framebufferIndex;
	rc = IOConnectCallMethod(me->_context,
							 kIOVM2DReadConfigEx,
							 0, 0,
							 &struct_in[0], sizeof struct_in,
							 0, 0,
							 &struct_out[0], &struct_out_cnt);
	if (position)
		*position = struct_out[1];
	return rc;
#endif
}

static IOReturn vmAllocateSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, void* cgsSurfaceID)
{
	GAType* me = static_cast<GAType*>(myInstance);
	SInt32 max_w = 4096, max_h = 4096;
	bool flag;
	SurfaceInfo* si;
	IOReturn rc;
	UInt32 tmp[5];

	GALog(2, "%s(%p, %#x, %p, %p)\n", __FUNCTION__, myInstance, FMT_U(options), surface, cgsSurfaceID);

	if (!me || !surface)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	GALog(2, "%s:  pixelFormat == %#x, width = %d, height = %d\n", __FUNCTION__,
		  FMT_U(surface->pixelFormat), FMT_D(surface->size.width), FMT_D(surface->size.height));
	if (me->_config_val_2 <= 79) {
		max_w = 2046; max_h = 2047;
	}
	flag = (options & kIOBlitFixedSource) != 0;
	if (flag) {
		if (surface->size.width > max_w || surface->size.height > max_h)
			return kIOReturnUnsupported;
	}
	si = static_cast<SurfaceInfo*>(malloc(sizeof *si));
	if (!si)
		return kIOReturnNoMemory;
	bzero(si, sizeof *si);
	if (options & kIOBlitHasCGSSurface) {
		si->cgsSurfaceID = reinterpret_cast<uintptr_t>(cgsSurfaceID);
		surface->interfaceRef = reinterpret_cast<IOBlitMemoryRef>(si);
		rc = vmSetSurface(myInstance, 0x800U, surface);
		if (rc != kIOReturnSuccess)
			goto free_si_and_exit;
		if (flag) {
			uint64_t input[3];
			input[0] = options;
			input[1] = surface->size.width;		// Note: source sign extends
			input[2] = surface->size.height;	// Note: source sign extends
			// Note: sets dword ptr [ebp-0xC] to 0
			rc = IOConnectCallMethod(me->_context, kIOVM2DScaleSurface,
									 &input[0], 3,
									 0, 0,
									 0, 0,
									 0, 0);
			if (rc != kIOReturnSuccess)
				goto free_si_and_exit;
		}
#if 0
		if ((options & 0x10U) != 0 &&
			(me->_config_val_1 & 0x42U) != 0) {
			if (!vmDecodePixelFormat(surface->pixelFormat,
									 &si->d2[5] /* ecx */,
									 &si->d2[10] /* edx */,
									 0,
									 &si->d2[6],
									 &tmp /* [ebp-0x10] */)) {
				rc = kIOReturnSuccess;
				goto set_si_and_exit;
			}
			// TBD GeForceGA 0x24F4 - 0x25E6
		}
#endif
		rc = kIOReturnSuccess;
		goto set_si_and_exit;
	}
	if (!(options & kIOBlitReferenceSource)) {
		vmDecodePixelFormat(surface->pixelFormat /* eax */,
							&si->d2[5] /* ecx */,
							&si->d2[10] /* edx */,
							&tmp,	/* [ebp-0x18] */
							&si->d2[6],
							0);
		rc = kIOReturnUnsupported;
		goto free_si_and_exit;
	}
	if (me->_config_val_1 & 0x42U) {
		// TBD GeForceGA 0x23A0 - 0x249E
	}
	rc = kIOReturnUnsupported;
free_si_and_exit:
	free(si);
	si = 0;
set_si_and_exit:
	surface->interfaceRef = reinterpret_cast<IOBlitMemoryRef>(si);
	return rc;
}

static IOReturn vmFreeSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface)
{
	GAType* me = static_cast<GAType*>(myInstance);
	SurfaceInfo* si;
	IOReturn rc;
	uint64_t input;

	GALog(2, "%s(%p, %#x, %p)\n", __FUNCTION__, myInstance, FMT_U(options), surface);

	if (!me || !surface)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	si = reinterpret_cast<SurfaceInfo*>(surface->interfaceRef);
	if (!si)
		return kIOReturnNotReady;
	rc = kIOReturnSuccess;
	if (me->_surface) {
		si->cgsSurfaceID = 0;
		rc = vmSetDestination(myInstance, 0, 0);
	}
	if (si->d0) {
		input = *(si->d0);
		rc = IOConnectCallMethod(me->_context, kIOVM2DDeleteImage,
								 &input, 1,
								 0, 0,
								 0, 0,
								 0, 0);
	}
	free(si);
	surface->interfaceRef = 0;
	return rc;
}

static IOReturn vmLockSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, vm_address_t* address)
{
	GAType* me = static_cast<GAType*>(myInstance);
	SurfaceInfo* si;
	IOReturn rc;
	uint64_t input;
	uint64_t struct_out[2];
	size_t struct_out_cnt;

	GALog(2, "%s(%p, %#x, %p, %p)\n", __FUNCTION__, myInstance, FMT_U(options), surface, address);

	if (!me || !surface || !address)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	si = reinterpret_cast<SurfaceInfo*>(surface->interfaceRef);
	if (!si)
		return kIOReturnNotReady;
	if (si->addr) {
		*address = si->addr;
		return kIOReturnSuccess;
	}
	input = options;
	struct_out_cnt = sizeof struct_out;
	rc = IOConnectCallMethod(me->_context, kIOVM2DLockMemory,
							 &input, 1,
							 0, 0,
							 0, 0,
							 &struct_out[0], &struct_out_cnt);
	if (rc != kIOReturnSuccess)
		return rc;
	*address = static_cast<vm_address_t>(struct_out[0]);
	surface->rowBytes = static_cast<UInt32>(struct_out[1]);
	/*
	 * It took me over a week to come up with the following line because
	 *   QuickTime refuses to run under gdb -- Zenith432, 8/27/2009
	 */
	surface->accessFlags = 2;
	return kIOReturnSuccess;
}

static IOReturn vmUnlockSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, IOOptionBits* swapFlags)
{
	GAType* me = static_cast<GAType*>(myInstance);
	SurfaceInfo* si;
	IOReturn rc;
	uint64_t input;
	uint64_t output;
	uint32_t output_cnt;

	GALog(2, "%s(%p, %#x, %p, %#x)\n", __FUNCTION__,
		  myInstance, FMT_U(options), surface, FMT_U(swapFlags ? *swapFlags : 0));

	if (!me || !surface)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	si = reinterpret_cast<SurfaceInfo*>(surface->interfaceRef);
	if (!si)
		return kIOReturnNotReady;
	if (si->addr)
		return kIOReturnSuccess;
	input = options;
	output_cnt = 1;
	rc = IOConnectCallMethod(me->_context, kIOVM2DUnlockMemory,
							 &input, 1,
							 0, 0,
							 &output, &output_cnt,
							 0, 0);
	if (swapFlags)
		*swapFlags = static_cast<IOOptionBits>(output);
	return rc;
}

static IOReturn vmSwapSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, IOOptionBits* swapFlags)
{
	GAType* me = static_cast<GAType*>(myInstance);
	SurfaceInfo* si;
	IOReturn rc;
	uint64_t input, output;
	uint32_t output_cnt;

	GALog(3, "%s(%p, %#x, %p, %#x)\n", __FUNCTION__,
		  myInstance, FMT_U(options), surface, FMT_U(swapFlags ? *swapFlags : 0));

	if (!me || !surface)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;

	si = reinterpret_cast<SurfaceInfo*>(surface->interfaceRef);
	if (!si)
		return kIOReturnNotReady;
	if (si->d0 && si->cgsSurfaceID != 0) {
		switch (surface->pixelFormat) {
			case kIOUYVY422PixelFormat:
			case kIO2vuyPixelFormat:
			case kIOYUVSPixelFormat:
				// TBD GeForceGA 0x2F60 - 0x351F (!)
				break;
		}
	}
	output_cnt = 1;
	input = options;
	rc = IOConnectCallMethod(me->_context, kIOVM2DSwapSurface,
							 &input, 1,
							 0, 0,
							 &output, &output_cnt,
							 0, 0);
	if (swapFlags)
		*swapFlags = static_cast<IOOptionBits>(output);
	return rc;
}

static IOReturn vmSetDestination(void* myInstance, IOOptionBits options, IOBlitSurface* surface)
{
	GALog(2, "%s(%p, %#x, %p)\n", __FUNCTION__, myInstance, FMT_U(options), surface);

	if (options & kIOBlitSurfaceDestination)
		options = 0x800U;
	else
		options = kIOBlitFramebufferDestination;
	return vmSetSurface(myInstance, options, surface);
}

static IOReturn vmGetBlitter(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitterPtr* blitter)
{
	IOReturn rc;

	GALog(2, "%s(%p, %#x, %#x, %#x, %p)\n", __FUNCTION__,
		  myInstance, FMT_U(options), FMT_U(type), FMT_U(sourceType), blitter);

	if (!blitter)
		return kIOReturnBadArgument;
	*blitter = 0;
	type &= kIOBlitTypeVerbMask;
	sourceType &= 0x7FFFF000U;
	rc = kIOReturnUnsupported;
	switch (type) {
		case kIOBlitTypeRects:
			switch (sourceType) {
				case kIOBlitSourceSolid:
					*blitter = &vmFill;
					rc = kIOReturnSuccess;
					break;
			}
			break;
		case kIOBlitTypeCopyRects:
			switch (sourceType) {
				case kIOBlitSourceDefault:
				case kIOBlitSourceFramebuffer:
					*blitter = &vmCopy;
					rc = kIOReturnSuccess;
					break;
				case kIOBlitSourceMemory:
					// vmMemCopy is an unimplemented stub. Declining here lets
					// CoreGraphics choose its fallback; promising a blitter then
					// failing at execution silently loses ordinary window layers.
					break;
			}
			break;
		case kIOBlitTypeCopyRegion:
			switch (sourceType) {
				case kIOBlitSourceDefault:
				case kIOBlitSourceFramebuffer:
				case kIOBlitSourceCGSSurface:
					*blitter = &vmCopyRegion;
					rc = kIOReturnSuccess;
					break;
#ifdef SUPPORT_CGLS
				case kIOBlitSourceMemory:
				case kIOBlitSourceOOLMemory:
					// vmMemCopyRegion is also unimplemented; do not advertise it.
					break;
#endif
			}
			break;
	}
	/*
	 * Note: GeForceGA supports a lot of other blitters,
	 *   including some whose type doesn't appear in the header file
	 */
	return rc;
}

static IOReturn vmWaitComplete(void* myInstance, IOOptionBits options)
{
	GAType* me = static_cast<GAType*>(myInstance);
	uint64_t input;

	GALog(3, "%s(%p, %#x)\n", __FUNCTION__, myInstance, FMT_U(options));

	if (!me)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
#if 0
	vmFlush(myInstance, 0);
#endif
	input = options;
	return IOConnectCallMethod(me->_context, kIOVM2DFinish,
							   &input, 1,
							   0, 0,
							   0, 0,
							   0, 0);
}

static IOReturn vmWaitSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface)
{
	GAType* me = static_cast<GAType*>(myInstance);
	SurfaceInfo* si;
	uint64_t input;

	GALog(2, "%s(%p, %#x, %p)\n", __FUNCTION__, myInstance, FMT_U(options), surface);

	if (!me || !surface)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	si = reinterpret_cast<SurfaceInfo*>(surface->interfaceRef);
	if (!si)
		return kIOReturnNotReady;
	if (si->d0 == 0)
		return kIOReturnSuccess;
	input = *(si->d0);
	return IOConnectCallMethod(me->_context, kIOVM2DWaitImage,
							   &input, 1,
							   0, 0,
							   0, 0,
							   0, 0);
}

static IOReturn vmSetSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface)
{
	GAType* me = static_cast<GAType*>(myInstance);
	SurfaceInfo* si;
	IOReturn rc;
	uint64_t input[2];
	size_t output_struct_cnt;
	UInt32 input_struct[2];
	UInt32 output_struct[3];

	GALog(2, "%s(%p, %#x, %p)\n", __FUNCTION__, myInstance, FMT_U(options), surface);

	if (!me)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	if (options & 0x800U) {
		si = reinterpret_cast<SurfaceInfo*>(surface->interfaceRef);
		if (!si)
			return kIOReturnNotReady;
		switch (surface->pixelFormat) {
			case kIO2vuyPixelFormat:
			case kIOUYVY422PixelFormat:
				options |= 0x400U;
				break;
			case kIOYUVSPixelFormat:
				options |= 0x200U;
				break;
			case kIO32BGRAPixelFormat:
				options |= 0x100U;
				break;
		}
		input[0] = static_cast<uint64_t>(si->cgsSurfaceID);	// Note: source sign-extends this
		input[1] = options;
	} else {
		input[0] = me->_framebufferIndex;
		input[1] = 0;
	}
	output_struct_cnt = 11U * sizeof(UInt32);
	rc = IOConnectCallMethod(me->_context, kIOVM2DSetSurface,
							 &input[0], 2,
							 0, 0,
							 0, 0,
							 &me->_buffer, &output_struct_cnt);
	if (rc != kIOReturnSuccess)
		return rc;
	if (input[0] != 0 && input[1] != 0)
		me->_surface = surface;
	else
		me->_surface = 0;
	output_struct_cnt = sizeof output_struct;
	input_struct[0] = 288;
	rc = IOConnectCallMethod(me->_context, kIOVM2DReadConfigEx,
							 0, 0,
							 &input_struct[0], sizeof input_struct,
							 0, 0,
							 &output_struct[0], &output_struct_cnt);
	if (rc == kIOReturnSuccess) {
		me->_config_Ex_1 = output_struct[0];
		me->_config_Ex_2 = output_struct[2];
	} else {
		me->_config_Ex_1 = 64;
		me->_config_Ex_2 = 16;
	}
	return rc;
}

#pragma mark -
#pragma mark Blitters
#pragma mark -

static IOReturn vmCopy(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
	GAType* me = static_cast<GAType*>(myInstance);
	IOBlitCopyRectangles* copy_rects;
	IOReturn rc;

	GALog(3, "%s(%p, %#x, %#x, %#x, %p, %p)\n", __FUNCTION__,
		  myInstance, FMT_U(options), FMT_U(type), FMT_U(sourceType), operation, source);

	if ((type & kIOBlitTypeOperationMask) != kIOBlitCopyOperation)
		return kIOReturnUnsupported;

	if (!me)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;

	copy_rects = reinterpret_cast<IOBlitCopyRectangles*>(operation);
	if (!copy_rects || !copy_rects->count)
		return kIOReturnBadArgument;

#if LOGGING_LEVEL >= 1
	if (logLevel >= 3)
		for (IOItemCount i = 0; i < copy_rects->count; ++i) {
			IOBlitCopyRectangle* copy_rect = &copy_rects->rects[i];
			GALog(3, "%s:   Copy Rect %u == [%d, %d, %d, %d, %d, %d]\n",
				  __FUNCTION__,
				  FMT_U(i),
				  FMT_D(copy_rect->sourceX),
				  FMT_D(copy_rect->sourceY),
				  FMT_D(copy_rect->x),
				  FMT_D(copy_rect->y),
				  FMT_D(copy_rect->width),
				  FMT_D(copy_rect->height));
		}
#endif

	rc = RectCopy(me->_context, &copy_rects->rects[0], copy_rects->count * sizeof(IOBlitCopyRectangle));

	GALog(3, "%s:   Copy returns %#x\n", __FUNCTION__, rc);

	return rc;
}

static IOReturn vmFill(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
	GAType* me = static_cast<GAType*>(myInstance);
	IOBlitRectangles* rects;
	IOReturn rc;

	GALog(3, "%s(%p, %#x, %#x, %#x, %p, %p)\n", __FUNCTION__,
		  myInstance, FMT_U(options), FMT_U(type), FMT_U(sourceType), operation, source);

	if ((type & kIOBlitTypeOperationMask) != kIOBlitCopyOperation)
		return kIOReturnUnsupported;

	if (!me)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;

	/*
	 * source is the packed 32-bit operation argument, not a client pointer.
	 * Do not infer inversion from ffffffff: the measured native CGS path
	 * uses it as an RGB mask while clearing alpha. The provider owns the
	 * destination-specific operation semantics.
	 */
	rects = reinterpret_cast<IOBlitRectangles*>(operation);
	if (!rects || !rects->count)
		return kIOReturnBadArgument;

#if LOGGING_LEVEL >= 1
	if (logLevel >= 3)
		for (IOItemCount i = 0; i < rects->count; ++i) {
			IOBlitRectangle* rect = &rects->rects[i];
			GALog(3, "%s:   Fill Rect %u == [%d, %d, %d, %d]\n",
				  __FUNCTION__,
				  FMT_U(i),
				  FMT_D(rect->x),
				  FMT_D(rect->y),
				  FMT_D(rect->width),
				  FMT_D(rect->height));
		}
#endif

	rc = RectFill(me->_context, reinterpret_cast<uintptr_t>(source), &rects->rects[0], rects->count * sizeof(IOBlitRectangle));

	GALog(3, "%s:   Fill returns %#x\n", __FUNCTION__, rc);

	return rc;
}

static IOReturn vmCopyRegion(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
	GAType* me = static_cast<GAType*>(myInstance);
	IOBlitCopyRegion* copy_region;
	IOAccelDeviceRegion* rgn;
	IOReturn rc;

	GALog(3, "%s(%p, %#x, %#x, %#x, %p, %p)\n", __FUNCTION__,
		  myInstance, FMT_U(options), FMT_U(type), FMT_U(sourceType), operation, source);

	if ((type & kIOBlitTypeOperationMask) != kIOBlitTypeOperationType0)
		return kIOReturnUnsupported;
	switch (sourceType & 0x7FFFF000U) {
		case kIOBlitSourceDefault:
		case kIOBlitSourceFramebuffer:
			source = 0;
			break;
		case kIOBlitSourceCGSSurface:
			break;
		default:
			return kIOReturnUnsupported;
	}
	if (!me)
		return kIOReturnBadArgument;
	if (!me->_context)
		return kIOReturnNotReady;
	copy_region = reinterpret_cast<IOBlitCopyRegion*>(operation);
	if (!copy_region)
		return kIOReturnBadArgument;
	rgn = copy_region->region;

	GALog(3, "%s:   CopyRegion deltaX == %d, deltaY == %d, region->num_rects == %u\n",
		  __FUNCTION__,
		  FMT_D(copy_region->deltaX),
		  FMT_D(copy_region->deltaY),
		  FMT_U(rgn ? rgn->num_rects : 0xFFFFFFFFU));

	if (!rgn)
		return kIOReturnBadArgument;

#if LOGGING_LEVEL >= 1
	if (logLevel >= 3) {
		IOAccelBounds* rect = &rgn->bounds;
		GALog(3, "%s:   CopyRegion bounds == [%d, %d, %d, %d]\n", __FUNCTION__, rect->x, rect->y, rect->w, rect->h);
		for (UInt32 i = 0; i < rgn->num_rects; ++i) {
			rect = &rgn->rect[i];
			GALog(3, "%s:   CopyRegion rect %u == [%d, %d, %d, %d]\n", __FUNCTION__, FMT_U(i), rect->x, rect->y, rect->w, rect->h);
		}
	}
#endif

	if (!source && me->_surface) {
		VIMGLIGPUCopyProc gpu_copy=vmGLIHooks()->copy;
		IOBlitSurface* target=static_cast<IOBlitSurface*>(me->_surface);
		SurfaceInfo* info=reinterpret_cast<SurfaceInfo*>(target->interfaceRef);
		if(gpu_copy && info && info->cgsSurfaceID<=UINT32_MAX &&
		   gpu_copy(me->_context,static_cast<uint32_t>(info->cgsSurfaceID),rgn,
			copy_region->deltaX,copy_region->deltaY,&rc))return rc;
	}
	rc = CopyRegion(me->_context,
					reinterpret_cast<uintptr_t>(source),
					copy_region->deltaX,
					copy_region->deltaY,
					rgn,
					IOACCEL_SIZEOF_DEVICE_REGION(rgn));

	if (!rc) rc = vmNotifyCPUSurfaceCopy(me, rgn, copy_region->deltaX, copy_region->deltaY);

	GALog(3, "%s:   CopyRegion returns %#x\n", __FUNCTION__, rc);

	return rc;
}

static IOReturn vmMemCopy(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
	GALog(2, "%s(%p, %#x, %#x, %#x, %p, %p)\n", __FUNCTION__,
		  myInstance, FMT_U(options), FMT_U(type), FMT_U(sourceType), operation, source);

	/*
	 * TBD: memory to framebuffer copy
	 */
	return kIOReturnUnsupported;
}

static IOReturn vmMemCopyRegion(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source)
{
	GALog(2, "%s(%p, %#x, %#x, %#x, %p, %p)\n", __FUNCTION__,
		  myInstance, FMT_U(options), FMT_U(type), FMT_U(sourceType), operation, source);

	/*
	 * TBD: not sure
	 */
	return kIOReturnUnsupported;
}

#pragma mark -
#pragma mark misc
#pragma mark -

static GAType* _allocGAType(CFUUIDRef factoryID)
{
	GAType *newOne = static_cast<GAType*>(malloc(sizeof(GAType)));
	if (!newOne)
		return 0;

	bzero(newOne, sizeof(GAType));
	_buildGAFTbl();
    newOne->_interface = &ga;

	if (factoryID) {
		newOne->_factoryID = static_cast<CFUUIDRef>(CFRetain(factoryID));
		CFPlugInAddInstanceForFactory(factoryID);
	}
	newOne->_refCount = 1;
	return newOne;
}

static void _deallocGAType(GAType *myInstance)
{
	CFUUIDRef factoryID;

	if (!myInstance)
		return;
	factoryID = myInstance->_factoryID;
	free(myInstance);
	if (factoryID) {
		CFPlugInRemoveInstanceForFactory(factoryID);
		CFRelease(factoryID);
	}
}

static void _buildGAFTbl()
{
	if (ga_initialized)
		return;
	bzero(&ga, sizeof ga);
	ga.QueryInterface = vmQueryInterface;
	ga.AddRef = vmAddRef;
	ga.Release = vmRelease;
	ga.version = kCurrentGraphicsInterfaceVersion;
	ga.revision = kCurrentGraphicsInterfaceRevision;
	ga.Probe = vmProbe;
	ga.Start = vmStart;
	ga.Stop = vmStop;
	ga.Reset = vmReset;
	ga.CopyCapabilities = vmGetCapabilities;
	ga.Flush = vmFlush;
	ga.Synchronize = vmSynchronize;
	ga.GetBeamPosition = vmGetBeamPosition;
	ga.AllocateSurface = vmAllocateSurface;
	ga.FreeSurface = vmFreeSurface;
	ga.LockSurface = vmLockSurface;
	ga.UnlockSurface = vmUnlockSurface;
	ga.SwapSurface = vmSwapSurface;
	ga.SetDestination = vmSetDestination;
	ga.GetBlitter = vmGetBlitter;
	ga.WaitComplete = vmWaitComplete;
	ga.__gaInterfaceReserved[0] = reinterpret_cast<void*>(vmWaitSurface);
	ga.__gaInterfaceReserved[1] = reinterpret_cast<void*>(vmSetSurface);
	/*
	 * 22 slots reserved from here.
	 */
	ga_initialized = 1;
}

__attribute__((visibility("default")))
void* VMsvga2GAFactory(CFAllocatorRef allocator, CFUUIDRef typeID)
{
	if (CFEqual(typeID, kIOGraphicsAcceleratorTypeID))
		return _allocGAType(kVMGAFactoryID);
	return NULL;
}
