/*
 *  UCMethods.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on September 4th 2009.
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

#ifndef __UCMETHODS_H__
#define __UCMETHODS_H__

enum eIOVMGLMethods {
	kIOVMGLSetSurface,
	kIOVMGLSetSwapRect,
	kIOVMGLSetSwapInterval,
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	kIOVMGLGetConfig,
#endif
	kIOVMGLGetSurfaceSize,
	kIOVMGLGetSurfaceInfo,
	kIOVMGLReadBuffer,
	kIOVMGLFinish,
	kIOVMGLWaitForStamp,
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	kIOVMGLNewTexture,
	kIOVMGLDeleteTexture,
#endif
	kIOVMGLBecomeGlobalShared,
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	kIOVMGLPageOffTexture,
#endif
	kIOVMGLPurgeTexture,
	kIOVMGLSetSurfaceVolatileState,
	kIOVMGLSetSurfaceGetConfigStatus,
	kIOVMGLReclaimResources,
	kIOVMGLGetDataBuffer,
	kIOVMGLSetStereo,
	kIOVMGLPurgeAccelerator,
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	kIOVMGLGetChannelMemory,
#else
	kIOVMGLSubmitCommandBuffer,
	kIOVMGLFilterControl,	// This method only appeared in OS 10.6.8
#endif

	kIOVMGLGetQueryBuffer,
	kIOVMGLGetNotifiers,
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	kIOVMGLNewHeapObject,
#endif
	kIOVMGLKernelPrintf,
	kIOVMGLNvRmConfigGet,
	kIOVMGLNvRmConfigGetEx,
	kIOVMGLNvClientRequest,
	kIOVMGLPageoffSurfaceTexture,
	kIOVMGLGetDataBufferWithOffset,
	kIOVMGLNvRmControl,
	kIOVMGLGetPowerState,
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	kIOVMGLSetWatchdogTimer,
	kIOVMGLGetHandleIndex,
	kIOVMGLForceTextureLargePages,
#endif

	kIOVMGLNumMethods
};

enum eIOVM2DMethods {
	kIOVM2DSetSurface,
	kIOVM2DGetConfig,
	kIOVM2DGetSurfaceInfo1,
	kIOVM2DSwapSurface,
	kIOVM2DScaleSurface,
	kIOVM2DLockMemory,
	kIOVM2DUnlockMemory,
	kIOVM2DFinish,
	kIOVM2DDeclareImage,
	kIOVM2DCreateImage,
	kIOVM2DCreateTransfer,
	kIOVM2DDeleteImage,
	kIOVM2DWaitImage,
	kIOVM2DSetSurfacePagingOptions,
	kIOVM2DSetSurfaceVsyncOptions,
	kIOVM2DSetMacrovision,

	kIOVM2DReadConfigs,
	kIOVM2DReadConfigEx,
	kIOVM2DGetSurfaceInfo2,
	kIOVM2DKernelPrintf,

	kIOVM2DCopyRegion,
	kIOVM2DUseAccelUpdates,
	kIOVM2DRectCopy,
	kIOVM2DRectFill,
	kIOVM2DUpdateFramebuffer,
	kIOVM2DLockSurfaceBacking, // VIMSurfaceMapping; unlock with UnlockMemory
	kIOVM2DGetSurfaceClip, // VIMSurfaceClip; query while backing is locked
	kIOVM2DGetSurfaceGeometry, // VIMSurfaceGeometry; drawing view, not allocation
	kIOVM2DDirectGLTransfer, // private bounded research stream, opt-in QEMU
	kIOVM2DGetSurfaceState, // VIMSurfaceState; caller must own backing lock
	kIOVM2DUnlockSurfaceBacking, // scalar read-only flag, exact post-unlock state
	kIOVM2DDirectGLScatter, // private prepared VRAM row ranges; append only
	kIOVM2DFramebufferBlitControl, // research counters and same-boot A/B
	kIOVM2DSurfaceServiceProbe, // bounded 2x2 research materialization, append only
	kIOVM2DMaterializeSurface, // kernel-owned read into caller's locked backing
	kIOVM2DDeferSurface, // retain published GPU epoch; next CPU acquisition reads it
	kIOVM2DLockGPUSurface, // metadata-only acquisition; never maps CPU pixels
	kIOVM2DGetVRAMSurface, // mode 0=query, 1=dirty notification after GPU completion
	kIOVM2DGetRAMSurface, // mode, first page; bounded pinned physical page chunks
	kIOVM2DDirectGLExchange, // bounded in-band send + receive, single ordered device action
	kIOVM2DWaitSurfaceBacking, // interruptible atomic acquisition after context contention
	kIOVM2DBeginRAMBackingLoan, // locked transport endpoint; VIMSurfaceVRAM -> VIMSurfaceBackingLoan
	kIOVM2DGetRAMBackingLoan, // serial, first -> stable VIMSurfaceRAM even after source teardown
	kIOVM2DEndRAMBackingLoan, // serial, mode -> exact post-unlock VIMSurfaceState
	kIOVM2DBeginDeferredRAMLoan, // explicit whole-object CPU exclusion before GPU writes
	kIOVM2DCommitDeferredRAMLoan, // actual native ticket -> future GPU_PENDING state
	kIOVM2DCommitFutureRAMLoan, // as above, additionally exposes exact GPU-only read loans
	kIOVM2DLockFutureGPUSurface, // metadata-only READ of pending immutable image, never a drawable
	kIOVM2DGetFutureGPUSurface, // exact VIMSurfaceFutureRead while owning that read loan
	kIOVM2DBeginWriterRAMLoan, // initial CPU-ready whole-object gate, explicit writer-chain mode
	kIOVM2DContinueWriterRAMLoan, // layout -> retained VIMSurfaceWriterLoan, NO CPU acquisition
	kIOVM2DCommitWriterRAMLoan, // native sealed ticket -> latest GPU future, gate remains retained
	kIOVM2DEndWriterRAMLoan, // serial, read-only refusal OR actual drained Abort; never fake Written
	kIOVM2DGetSurfaceDrawingSize, // validated single client-backed rectangle
	kIOVM2DGetSurfacePixelFormat, // VIMSurfacePixelFormat; bound backing lock required

	kIOVM2DNumMethods
};

enum eIOVMDVDMethods {
	kIOVMDVDNumMethods
};

enum eIOVMDeviceMethods {
	kIOVMDeviceCreateShared,
	kIOVMDeviceGetConfig,
	kIOVMDeviceGetSurfaceInfo,
	kIOVMDeviceGetName,
	kIOVMDeviceWaitForStamp,
	kIOVMDeviceNewTexture,
	kIOVMDeviceDeleteTexture,
	kIOVMDevicePageOffTexture,
	kIOVMDeviceGetChannelMemory,

	kIOVMDeviceKernelPrintf,
	kIOVMDeviceNvRmConfigGet,
	kIOVMDeviceNvRmConfigGetEx,
	kIOVMDeviceNvRmControl,

	kIOVMDeviceNumMethods
};

enum eIOVMOCDMethods {
	kIOVMOCDFinish,
	kIOVMOCDWaitForStamp,

	kIOVMOCDCheckErrorNotifier,
	kIOVMOCDMarkTextureForOCDUse,
	kIOVMOCDFreeEvent,
	kIOVMOCDGetHandleIndex,

	kIOVMOCDNumMethods
};

#endif /* __UCMETHODS_H__ */
