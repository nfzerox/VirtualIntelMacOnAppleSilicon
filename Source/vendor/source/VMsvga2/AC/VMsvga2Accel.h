/*
 *  VMsvga2Accel.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on July 29th 2009.
 *  Copyright 2009-2012 Zenith432. All rights reserved.
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

#ifndef __VMSVGA2ACCEL_H__
#define __VMSVGA2ACCEL_H__

#include <IOKit/graphics/IOAccelerator.h>
#include "SVGA3D.h"
#include "SVGAScreen.h"
#include "FenceTracker.h"
#include "../vminclude/vim_backing_access.h"

#define kIOMessageFindSurface iokit_vendor_specific_msg(0x10)

#define AUTO_SYNC_PRESENT_FENCE_COUNT	2

class VMsvga2Accel : public IOAccelerator
{
	OSDeclareDefaultStructors(VMsvga2Accel);

private:
	/*
	 * Base
	 */
	SVGA3D svga3d;
	SVGAScreen screen;
	class VMsvga2* m_framebuffer;
	SVGADevice* m_svga;
	IODeviceMemory* m_vram;
	IOMemoryMap* m_vram_kernel_map;
	class VMsvga2Allocator* m_allocator;
	IOLock* m_iolock;
	IOLock* m_framebuffer_blit_lock;
	IOLock* m_surface_service_lock;
	void* m_surface_service_buffer;
	uint32_t m_surface_service_token;
	uint64_t m_surface_service_capabilities;
	uint64_t m_surface_service_namespace[2];
	bool m_surface_service_failed;
	void closeSurfaceService(); // service lock held, or provider teardown
	IOReturn transferSurfaceService(void*,size_t,bool);
	IOReturn openSurfaceService(uint64_t const*);
	void* m_framebuffer_blit_buffer;
	int m_framebuffer_blit_supported;
	bool m_framebuffer_blit_enabled;
	uint32_t m_framebuffer_blit_rejection;
	volatile uint32_t m_direct_ram_upload_enabled; // independent diagnostic switch, no associated data
	volatile uint32_t m_direct_ram_bulk_upload_enabled;
	volatile uint32_t m_direct_command_copy_limit; // Same-boot bounded-write comparison; no associated data.
	volatile uint32_t m_direct_gl_doorbell_supported,m_direct_gl_doorbell_enabled;
	volatile uint32_t m_direct_gl_parallel_doorbell_enabled; // research A/B; packet owners still serialize their own stream
	IOLock* m_backing_notify_lock;
	IOLock* m_backing_access_lock; // metadata only; never held across device/native I/O
	void* m_backing_notify_buffer;
	uint32_t m_backing_notify_token;
	uint64_t m_backing_notify_serial;
	bool m_backing_notify_registered,m_backing_notify_failed;
	struct VIMBackingKernelJob* m_backing_notify_jobs[256];
	struct VIMBackingAccessRegistry m_backing_access;
	IOReturn openBackingNotifications(); // notification lock held
	IOReturn transferBackingNotifications(); // own packet, independent doorbell
	void closeBackingNotifications(); // unregister before taking lock
	static void backingNotifications(OSObject*);
	uint64_t m_framebuffer_blits,m_framebuffer_blit_bytes,m_framebuffer_blit_fallbacks;
#ifdef FB_NOTIFIER
	IONotifier* m_fbNotifier;
#endif
	task_t m_updating_ga;
	IOLock* m_accel_update_lock;
	uint32_t m_accel_update_clients;

	/*
	 * Logging and Options
	 */
	int m_log_level_ac;
	int m_log_level_ga;
	int m_log_level_gld;
	uint32_t m_options_ga;

	/*
	 * 3D area
	 */
	unsigned bHaveSVGA3D:1;
	unsigned bHaveScreenObject:1;
	uint64_t m_surface_id_mask[4];
	uint64_t m_context_id_mask;
	uint64_t m_gmr_id_mask[2];
	uint32_t m_surface_ids_unmanaged;
	uint32_t m_context_ids_unmanaged;
	uint32_t m_surface_id_idx;
	uint32_t m_gmr_id_idx;
	int volatile m_master_surface_retain_count;
	uint32_t m_master_surface_id;
	IOReturn m_blitbug_result;
	struct {
		uint32_t w, h;
	} m_primary_screen;

	/*
	 * AutoSync area
	 */
	FenceTracker<AUTO_SYNC_PRESENT_FENCE_COUNT> m_present_tracker;

	/*
	 * Video area
	 */
	uint32_t m_stream_id_mask;

	/*
	 * OS 10.6 specific
	 */
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	class IOSurfaceRoot* m_surface_root;
	unsigned m_surface_root_uuid;
#endif

	/*
	 * Private support methods
	 */
	void Cleanup();
#ifdef TIMING
	void timeSyncs();
#endif
	bool createMasterSurface(uint32_t width, uint32_t height);
	void destroyMasterSurface();
	void processOptions();
	IOReturn findFramebuffer();
	IOReturn setupAllocator();
#ifdef FB_NOTIFIER
	IOReturn fbNotificationHandler(void* ref,
								   class IOFramebuffer* framebuffer,
								   SInt32 event,
								   void* info);
#endif
	void initPrimaryScreen();
	void cleanupPrimaryScreen();

public:
	/*
	 * Methods overridden from superclass
	 */
	bool init(OSDictionary* dictionary = 0);
	bool start(IOService* provider);
	void stop(IOService* provider);
	IOReturn callPlatformFunction(const OSSymbol*,bool,void*,void*,void*,void*);
	IOReturn borrowBackingAccess(void*,void*);
	void returnBackingAccess(void*);
	bool promoteBackingAccess(void*,void*);
	void releaseBackingAccess(void*);
	IOReturn retireBackingTicket(uint64_t const*,uint64_t); // completed notification only
	IOReturn newUserClient(task_t owningTask,
						   void* securityID,
						   UInt32 type,
						   IOUserClient ** handler);

	/*
	 * Standalone Sync Methods
	 */
	IOReturn SyncFIFO();
	bool directGLAvailable();
	bool directGLDirectReadAvailable();
	bool directGLDirectWriteAvailable();
	bool directGLScatterAvailable();
	bool directGLRAMScatterAvailable();
	bool directGLMaskedScatterAvailable();
	bool directGLRAMUploadAvailable();
	bool directGLBulkUploadAvailable();
	bool directGLExchangeAvailable();
	bool directGLLargeExchangeAvailable();
	bool directGLRAMUploadEnabled() const { return m_direct_ram_upload_enabled!=0; }
	bool directGLBulkUploadEnabled() const { return m_direct_ram_bulk_upload_enabled!=0; }
	uint32_t directGLCommandCopyLimit() const { return m_direct_command_copy_limit; }
	bool directGLVRAMRange(uint64_t*,uint64_t*) const;
	bool directGLReadOffset(IOMemoryDescriptor*,IOByteCount,uint32_t*) const;
	IOReturn directGLTransfer(void* buffer);
	IOReturn directVRAMDirty(uint64_t,uint64_t);
	IOReturn directRAMDirty(uint64_t const*,uint32_t);
	IOReturn holdGPUSurface(uint64_t const*,uint64_t,uint64_t,uint32_t,uint32_t,uint32_t*,uint64_t epoch=0);
	IOReturn releaseGPUSurface(uint32_t);
	IOReturn presentGPUSurface(uint32_t,uint64_t,uint64_t*);
	IOReturn gpuPresentationReady(bool*);
	IOReturn readGPUSurface(uint32_t,uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,IOMemoryDescriptor*,uint64_t,uint64_t);
	IOReturn directFramebufferBlit(IOMemoryDescriptor*,uint64_t,uint64_t,uint32_t,void const*);
	IOReturn directFramebufferControl(uintptr_t,void*,size_t*);
	IOReturn RingDoorBell();
	IOReturn SyncToFence(uint32_t fence);

	/*
	 * Methods for supporting VMsvga22DContext
	 */
	IOReturn useAccelUpdates(bool state, task_t owningTask);
	IOReturn RectCopy(uint32_t framebufferIndex,
					  struct IOBlitCopyRectangleStruct const* copyRects,
					  size_t copyRectsSize);
#if 0
	IOReturn RectFillScreen(uint32_t framebufferIndex,
							uint32_t color,
							struct IOBlitRectangleStruct const* rects,
							size_t numRects);
	IOReturn RectFill3D(uint32_t color,
						struct IOBlitRectangleStruct const* rects,
						size_t numRects);
#endif
	IOReturn RectFill(uint32_t framebufferIndex,
					  uint32_t color,
					  struct IOBlitRectangleStruct const* rects,
					  size_t rectsSize);
	IOReturn UpdateFramebufferAutoRing(uint32_t const* rect);	// rect is an array of 4 uint32_t - x, y, width, height
	IOReturn CopyRegion(uint32_t framebufferIndex,
						int destX,
						int destY,
						void /* IOAccelDeviceRegion */ const* region,
						size_t regionSize);
	struct FindSurface {
		uint32_t cgsSurfaceID;
		OSObject* client;
		bool privatePBuffer; // copied during callback; no post-iteration dereference
	};

	/*
	 * Methods for supporting VMsvga2Surface
	 */
	struct ExtraInfo {
		vm_offset_t mem_offset_in_gmr;
		vm_size_t mem_pitch;
		int srcDeltaX;
		int srcDeltaY;
		int dstDeltaX;
		int dstDeltaY;
		uint32_t mem_gmr_id;
	};
	struct ExtraInfoEx {
		vm_offset_t mem_offset_in_gmr;
		vm_size_t mem_pitch;
		vm_size_t mem_limit;
		uint32_t mem_gmr_id;
		uint32_t suffix_flags;
	};
	/*
	 * SVGA3D Methods
	 */
	IOReturn createSurface(uint32_t sid,
						   SVGA3dSurfaceFlags surfaceFlags,
						   SVGA3dSurfaceFormat surfaceFormat,
						   uint32_t width,
						   uint32_t height);
	IOReturn destroySurface(uint32_t sid);
	IOReturn surfaceDMA2D(uint32_t sid,
						  SVGA3dTransferType transfer,
						  void /* IOAccelDeviceRegion */ const* region,
						  ExtraInfo const* extra,
						  uint32_t* fence = 0);
	IOReturn surfaceCopy(uint32_t src_sid,
						 uint32_t dst_sid,
						 void /* IOAccelDeviceRegion */ const* region,
						 ExtraInfo const* extra);
	IOReturn surfaceStretch(uint32_t src_sid,
							uint32_t dst_sid,
							SVGA3dStretchBltMode mode,
							void /* IOAccelBounds */ const* src_rect,
							void /* IOAccelBounds */ const* dest_rect);
	IOReturn surfacePresentAutoSync(uint32_t sid,
									void /* IOAccelDeviceRegion */ const* region,
									ExtraInfo const* extra);
	IOReturn surfacePresentReadback(void /* IOAccelDeviceRegion */ const* region);
	IOReturn setRenderTarget(uint32_t cid,
							 SVGA3dRenderTargetType rtype,
							 uint32_t sid);
	IOReturn clear(uint32_t cid,
				   SVGA3dClearFlag flags,
				   void /* IOAccelDeviceRegion */ const* region,
				   uint32_t color,
				   float depth,
				   uint32_t stencil);
	IOReturn createContext(uint32_t cid);
	IOReturn destroyContext(uint32_t cid);
	IOReturn drawPrimitives(uint32_t cid,
							uint32_t numVertexDecls,
							uint32_t numRanges,
							SVGA3dVertexDecl const* decls,
							SVGA3dPrimitiveRange const* ranges);
	IOReturn setTextureState(uint32_t cid,
							 uint32_t numStates,
							 SVGA3dTextureState const* states);
	IOReturn setRenderState(uint32_t cid,
							uint32_t numStates,
							SVGA3dRenderState const* states);
	IOReturn surfaceDMA3DEx(SVGA3dSurfaceImageId const* hostImage,
							SVGA3dTransferType transfer,
							SVGA3dCopyBox const* copyBox,
							ExtraInfoEx const* extra,
							uint32_t* fence = 0);

	/*
	 * Screen Methods
	 */
	IOReturn createPrimaryScreen(uint32_t width,
								 uint32_t height);
	IOReturn blitFromScreen(uint32_t srcScreenId,
							void /* IOAccelDeviceRegion */ const* region,
							ExtraInfo const* extra,
							uint32_t* fence = 0);
	IOReturn blitToScreen(uint32_t destScreenId,
						  void /* IOAccelDeviceRegion */ const* region,
						  ExtraInfo const* extra,
						  uint32_t* fence = 0);
	IOReturn blitSurfaceToScreen(uint32_t src_sid,
								 uint32_t destScreenId,
								 void /* IOAccelBounds */ const* src_rect,
								 void /* IOAccelDeviceRegion */ const* dest_region);
	/*
	 * GFB Methods
	 */
	IOReturn blitGFB(uint32_t framebufferIndex,
					 void /* IOAccelDeviceRegion */ const* region,
					 ExtraInfo const* extra,
					 IOVirtualAddress gmrPtr,
					 vm_size_t limitFromGmrPtr,
					 int direction);
#if 0
	IOReturn clearGFB(uint32_t color,
					  struct IOBlitRectangleStruct const* rects,
					  size_t numRects);
#endif

	/*
	 * Misc support methods
	 */
	IOReturn getScreenInfo(IOAccelSurfaceReadData* info);
	bool retainMasterSurface();
	void releaseMasterSurface();
	uint32_t getMasterSurfaceID() const { return m_master_surface_id; }
	int getLogLevelAC() const { return m_log_level_ac; }
	int getLogLevelGA() const { return m_log_level_ga; }
	int getLogLevelGLD() const { return m_log_level_gld; }
	uint32_t getOptionsGA() const { return m_options_ga; }
	IOReturn getBlitBugResult() const { return m_blitbug_result; }
	void cacheBlitBugResult(IOReturn r) { m_blitbug_result = r; }
	void lockAccel();
	void unlockAccel();
	bool Have3D() const { return bHaveSVGA3D != 0; }
	bool HaveScreen() const { return bHaveScreenObject != 0; }
	bool HaveFrontBuffer() const { return bHaveScreenObject != 0 || bHaveSVGA3D != 0; }
	bool HaveGLBaseline() const;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	unsigned getSurfaceRootUUID() const { return m_surface_root_uuid; }
#endif
	IOMemoryDescriptor* getChannelMemory() const;
	uint32_t getVRAMSize() const;
	vm_offset_t offsetInVRAM(void* vram_ptr) const;
	class VMsvga2Surface* findSurfaceForID(uint32_t surface_id);
	IOReturn claimSurfaceID(class VMsvga2Surface*,uint32_t);
	SVGA3D* lock3D();
	void unlock3D();
	static
	IOReturn genericBlitCopy(IOVirtualAddress dst_base,
							 SVGAGuestImage const* dst_image,
							 SVGASignedPoint const* dst_delta,
							 IOVirtualAddress src_base,
							 SVGAGuestImage const* src_image,
							 SVGASignedPoint const* src_delta,
							 void /* IOAccelDeviceRegion */ const* region,
							 uint8_t bytes_per_pixel);
	bool isPrimaryScreenActive() const;

	/*
	 * Video Support
	 */
	IOReturn VideoSetRegsInRange(uint32_t streamId,
								 struct SVGAOverlayUnit const* regs,
								 uint32_t regMin,
								 uint32_t regMax,
								 uint32_t* fence = 0);
	IOReturn VideoSetReg(uint32_t streamId,
						 uint32_t registerId,
						 uint32_t value,
						 uint32_t* fence = 0);

	/*
	 * ID Allocation
	 */
	uint32_t AllocSurfaceID();
	void FreeSurfaceID(uint32_t sid);
	uint32_t AllocContextID();
	void FreeContextID(uint32_t cid);
	uint32_t AllocStreamID();
	void FreeStreamID(uint32_t streamId);
	uint32_t AllocGMRID();
	void FreeGMRID(uint32_t gmrId);

	/*
	 * Memory Support
	 */
	void* VRAMMalloc(size_t bytes);
	void* VRAMRealloc(void* ptr, size_t bytes);
	void VRAMFree(void* ptr);
	IOMemoryMap* mapVRAMRangeForTask(task_t task, vm_offset_t offset_in_vram, vm_size_t size);

	/*
	 * GMR Allocation
	 */
	IOReturn createGMR(uint32_t gmrId, IOMemoryDescriptor* md);
	IOReturn destroyGMR(uint32_t gmrId);
	IOReturn createGMR2(uint32_t gmrId, IOMemoryDescriptor* md);
	IOReturn destroyGMR2(uint32_t gmrId);
};

#endif /* __VMSVGA2ACCEL_H__ */
