/*
 *  VMsvga2GA.h
 *  VMsvga2GA
 *
 *  Created by Zenith432 on August 6th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
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

#ifndef __VMSVGA2GA_H__
#define __VMSVGA2GA_H__

#include <IOKit/IOCFPlugIn.h>

#ifdef __cplusplus

class VMsvga2GA
{
public:
	HRESULT QueryInterface(REFIID iid, LPVOID *ppv);
	ULONG AddRef();
	ULONG Release();
	IOReturn Probe(CFDictionaryRef propertyTable, io_service_t service, SInt32* order);
	IOReturn Start(CFDictionaryRef propertyTable, io_service_t service);
	IOReturn Stop();
	IOReturn Reset(IOOptionBits options);
	IOReturn CopyCapabilities(FourCharCode select, CFTypeRef* capabilities);
	IOReturn Flush(IOOptionBits options);
	IOReturn Synchronize(UInt32 options, UInt32 x, UInt32 y, UInt32 w, UInt32 h);
	IOReturn GetBeamPosition(IOOptionBits options, SInt32 * position);
	IOReturn AllocateSurface(IOOptionBits options, IOBlitSurface* surface, void* cgsSurfaceID);
	IOReturn FreeSurface(IOOptionBits options, IOBlitSurface* surface);
	IOReturn LockSurface(IOOptionBits options, IOBlitSurface* surface, vm_address_t* address);
	IOReturn UnlockSurface(IOOptionBits options, IOBlitSurface* surface, IOOptionBits* swapFlags);
	IOReturn SwapSurface(IOOptionBits options, IOBlitSurface* surface, IOOptionBits* swapFlags);
	IOReturn SetDestination(IOOptionBits options, IOBlitSurface* surface);
	IOReturn GetBlitter(IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitterPtr* blitter);
	IOReturn WaitComplete(IOOptionBits options);

	IOReturn Blitter(IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source);
};

#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * IOGraphicsAccelerator
 */
static IOReturn vmReset(void* myInstance, IOOptionBits options);
static IOReturn vmGetCapabilities(void* myInstance, FourCharCode select, CFTypeRef* capabilities);
static IOReturn vmFlush(void* myInstance, IOOptionBits options);
static IOReturn vmSynchronize(void* myInstance, UInt32 options, UInt32 x, UInt32 y, UInt32 w, UInt32 h);
static IOReturn vmGetBeamPosition(void* myInstance, IOOptionBits options, SInt32* position);
static IOReturn vmAllocateSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, void* cgsSurfaceID);
static IOReturn vmFreeSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface);
static IOReturn vmLockSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, vm_address_t* address);
static IOReturn vmUnlockSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, IOOptionBits* swapFlags);
static IOReturn vmSwapSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface, IOOptionBits* swapFlags);
static IOReturn vmSetDestination(void* myInstance, IOOptionBits options, IOBlitSurface* surface);
static IOReturn vmGetBlitter(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitterPtr* blitter);
static IOReturn vmWaitComplete(void* myInstance, IOOptionBits options);
static IOReturn vmWaitSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface);
static IOReturn vmSetSurface(void* myInstance, IOOptionBits options, IOBlitSurface* surface);
/*
 * Blitters
 */
static IOReturn vmCopy(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source);
static IOReturn vmFill(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source);
static IOReturn vmCopyRegion(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source);
static IOReturn vmMemCopy(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source);
static IOReturn vmMemCopyRegion(void* myInstance, IOOptionBits options, IOBlitType type, IOBlitSourceType sourceType, IOBlitOperation* operation, void* source);

#ifdef __cplusplus
}
#endif

#endif /* __VMSVGA2GA_H__ */
