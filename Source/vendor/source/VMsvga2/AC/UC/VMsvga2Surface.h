/*
 *  VMsvga2Surface.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on July 29th 2009.
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

#ifndef __VMSVGA2SURFACE_H__
#define __VMSVGA2SURFACE_H__

#include <IOKit/IOUserClient.h>
#include <kern/thread.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include "VendorTransferBuffer.h"
#include "../VMsvga2PreparedBacking.h"
#include "../../vminclude/vim_framebuffer.h"

class VMsvga2Surface: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2Surface);

private:
	friend class VMsvga2Accel;
	bool m_pbuffer_only; // application-owned surface; never a display family
	bool m_cgl_pbuffer; // CGL allocates independent source geometry via its shape
	/*
	 * Base
	 */
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	int m_log_level;
	VMsvga2PreparedBacking* m_prepared_backing; // basic FIFO: owns the actual prepared lifetime
	VMsvga2PreparedBacking* m_active_backing_loan; // nonowning: the transport endpoint retains it
	class VMsvga22DContext* m_backing_loan_endpoint; // retained during the scoped loan
	struct VIMSurfaceFutureRead m_future_backing;
	uint32_t m_future_readers; // immutable GPU reads only; NOT CPU mappings/context ownership
	bool m_future_retiring;
	OSObject* m_writer_backing; // nonowning; coordinator retains source until final CPU restoration
	IOLock* m_content_lock;
	thread_t m_context_owner; // identity only, never dereferenced
	uint32_t m_context_waiters; // protected by m_content_lock; no empty wakeups
	bool m_content_closing;
	uint64_t m_backing_incarnation,m_content_epoch;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	uint64_t m_content_object; // boot-local identity; Darwin9 has no registry ID KPI
#endif
	uint32_t m_gpu_hold,m_content_readers;
	uint64_t* m_shared_ram_pages;
	uint32_t m_shared_ram_page_count;
	uint64_t m_gpu_epoch;
	IOReturn materializeGPU(); // m_content_lock held; retain dirty state on failure
	void discardGPU(); // m_content_lock held
	IOReturn beginContentRead();
	void endContentRead();
	uint32_t m_content_writers,m_identity_width,m_identity_height,m_identity_pitch,m_identity_bpp,m_identity_format;
	bool m_native_write_active;
	bool m_gpu_presentation,m_gpu_invalidated,m_gpu_service_ready;
	uint32_t m_gpu_companion_id;
	VMsvga2Surface* m_gpu_primary;
	VMsvga2Surface* gpuPresentationRoot() const;
	IOReturn gpuPresentationFlush(uintptr_t,IOOptionBits);
	void noteBackingChange();
	IOReturn beginContentWrite();
	void endContentWrite();
	void nativeContentWrite(bool);
	void copyContentState(struct VIMSurfaceState*); // m_content_lock held
	struct ContentWrite {
		VMsvga2Surface* surface;
		IOReturn error;
		explicit ContentWrite(VMsvga2Surface* s):surface(s->m_gpu_primary?s->m_gpu_primary:s),error(surface->beginContentWrite()) {}
		~ContentWrite() { if(!error)surface->endContentWrite(); }
	};
	struct ContentRead {
		VMsvga2Surface* surface;bool active;IOReturn error;
		explicit ContentRead(VMsvga2Surface* s,bool needed=true):surface(s->m_gpu_primary?s->m_gpu_primary:s),active(needed),error(needed?surface->beginContentRead():kIOReturnSuccess) {}
		~ContentRead() { if(active && !error)surface->endContentRead(); }
	};
	struct FamilyMetadata {
		VMsvga2Surface* surface;IOReturn error;
		explicit FamilyMetadata(VMsvga2Surface* s):surface(s),error(kIOReturnSuccess) {
			if(!surface)return;
			IOLockLock(surface->m_content_lock);
			if(surface->m_content_writers || surface->m_content_readers || surface->m_native_write_active ||
			   (surface->bIsLocked&((128U>>vmSurfaceLockContext)|(128U>>vmSurfaceLockRead)|(128U>>vmSurfaceLockWrite))))
				error=kIOReturnCannotLock;
		}
		~FamilyMetadata(){if(surface)IOLockUnlock(surface->m_content_lock);}
	};

	/*
	 * Flags
	 */
	unsigned bHaveID:1;				// covers everything under "ID stuff"
	unsigned bHaveMasterSurface:1;
	unsigned bVideoMode:1;
	unsigned bDirectBlit:1;
	unsigned bHaveScreenObject:1;
	unsigned bSkipWriteLockOnce:1;

	/*
	 * Locking stuff
	 */
	enum {
		vmSurfaceLockRead = 0U,
		vmSurfaceLockWrite = 1U,
		vmSurfaceLockContext = 2U
	};
	uint8_t volatile bIsLocked;

	/*
	 * ID stuff
	 */
	uint32_t m_wID;
	SVGA3dSurfaceFormat m_surfaceFormat;
	uint32_t m_bytes_per_pixel;
	uint32_t m_pixel_format;

	/*
	 * Backing stuff
	 */
	struct {
		uint8_t* self;
		vm_offset_t offset;
		vm_size_t size;
		IOMemoryMap* map[2];
		VendorTransferBuffer vtb;
	} m_backing;

	/*
	 * Client backing stuff
	 */
	struct {
		mach_vm_address_t addr;
		vm_size_t rowbytes;
		vm_size_t size;
	} m_client_backing;

	/*
	 * Shape stuff
	 */
	OSData* m_last_shape;
	IOAccelDeviceRegion const* m_last_region;
	uint32_t m_framebufferIndex;

	/*
	 * Scale stuff
	 */
	IOAccelSurfaceScaling m_scale;

	/*
	 * Fullscreen stuff
	 */
	IOAccelSurfaceReadData m_screenInfo;

	/*
	 * Video stuff
	 */
	struct {
		uint32_t stream_id;
		uint32_t vmware_pixel_format;
		SVGAOverlayUnit	unit;
	} m_video;

	/*
	 * Private support methods
	 */
	IOReturn set_shape_backing_length_ext(eIOAccelSurfaceShapeBits options,
										  uintptr_t framebufferIndex,
										  mach_vm_address_t backing,
										  size_t rowbytes,
										  size_t backingLength,
										  IOAccelDeviceRegion const* rgn,
										  size_t rgnSize);
	bool haveFrontBuffer() const;
	bool isBackingValid() const;
	int classifyBacking() const;
	bool hasSourceGrown() const;
	bool isSourceValid() const;
	bool isClientBackingValid() const;
	bool isIdentityScale() const;
	void Init();
	void Cleanup();
	void clearLastRegion();
	void calculateSurfaceInformation(IOAccelSurfaceInformation* info);
	void calculateScaleParameters(bool bFromGFB = false);
	void clipRegionToBuffer(IOAccelDeviceRegion* region,
							int deltaX,
							int deltaY);

	/*
	 * Private support methods - backing
	 */
	bool wrapClientBacking();
	bool allocGMRBacking();
	bool allocBacking();
	bool mapBacking(task_t for_task, uint32_t index);
	void releaseBacking();
	void releaseBackingMap(uint32_t index);
	IOReturn obtainKernelPtrs(IOVirtualAddress* base, vm_size_t* limit_from_base, IOMemoryMap** holder);

	/*
	 * Private support methods - 3D
	 */
	void Start3D();
	void Cleanup3D();
	IOReturn detectBlitBug();
	IOReturn DMAOutDirect(bool withFence);
	IOReturn DMAOutWithCopy(bool withFence);
	IOReturn DMAOutStretchWithCopy(bool withFence);
	IOReturn doPresent();

	/*
	 * Private support methods - Screen Object
	 */
	IOReturn ScreenObjectOutDirect(bool withFence);
	IOReturn ScreenObjectOutVia3D(bool withFence);

	/*
	 * Private support methods - GFB
	 */
	IOReturn GFBOutDirect();

	/*
	 * Private support methods - Video
	 */
	void clear_yuv_to_black(void* buffer, vm_size_t size);
	IOReturn setup_trick_buffer();
	bool setVideoDest();
	bool setVideoRegs();
	void videoReshape();

public:
	/*
	 * Methods overridden from superclass
	 */
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	void free();
#if 0
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
	IOReturn externalMethod(uint32_t selector,
							IOExternalMethodArguments* arguments,
							IOExternalMethodDispatch* dispatch = 0,
							OSObject* target = 0,
							void* reference = 0);
#endif
	IOReturn message(UInt32 type, IOService* provider, void* argument = 0);
	bool start(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga2Surface* withTask(task_t owningTask, void* securityToken, uint32_t type,
		bool pbufferOnly=false);

	/*
	 * Interface for VMsvga22DContext
	 */
	IOReturn context_set_surface(uint32_t vmware_pixel_format, uint32_t apple_pixel_format);
	IOReturn context_scale_surface(IOOptionBits options, uint32_t width, uint32_t height);
	IOReturn context_lock_memory(task_t context_owning_task,
								 mach_vm_address_t* address,
								 mach_vm_size_t* rowBytes, bool wait = false);
	IOReturn context_unlock_memory(uint32_t* swapFlags, bool read_only = false, struct VIMSurfaceState* = 0,
		VMsvga2PreparedBacking* completed_loan = 0,uint64_t writer_epoch = 0,OSObject* writer_gate = 0);
	IOReturn context_register_writer_backing(VMsvga2PreparedBacking*,OSObject*);
	OSObject* context_copy_writer_backing(); // retained; caller drops content lock before gate operations
	IOReturn context_publish_writer_future(VMsvga2PreparedBacking*,OSObject*,uint64_t,uint64_t);
	IOReturn context_set_writer_thread(VMsvga2PreparedBacking*,OSObject*,bool);
	IOReturn context_get_state(struct VIMSurfaceState*);
	IOReturn context_materialize_gpu(struct VIMSurfaceMaterialize const*,struct VIMSurfaceState*);
	IOReturn context_defer_gpu(struct VIMSurfaceMaterialize const*,struct VIMSurfaceState*);
	IOReturn context_lock_backing(task_t, struct VIMSurfaceMapping*, bool wait = false);
	IOReturn context_lock_gpu_backing(struct VIMSurfaceMapping*);
	IOReturn context_get_clip(struct VIMSurfaceClip*);
	IOReturn context_get_geometry(struct VIMSurfaceGeometry*);
	IOReturn context_get_drawing_size(struct VIMSurfaceDrawingSize*);
	IOReturn context_get_pixel_format(struct VIMSurfacePixelFormat*);
	IOReturn context_get_vram(struct VIMSurfaceVRAM*,bool);
	IOReturn context_get_ram(struct VIMSurfaceRAM*,uint32_t,bool);
	/* Internal endpoint loan prerequisite, no user/wire capability yet. Caller
	 * holds real CPU exclusion, retains the returned owner through completed
	 * GPU use, and releases it afterwards. Never exports a kernel pointer. */
	IOReturn context_retain_ram_backing(struct VIMSurfaceVRAM const*,VMsvga2PreparedBacking**,class VMsvga22DContext*);
	IOReturn context_copy_ram_owner(VMsvga2PreparedBacking**); // locked metadata export; retained pin lifetime
	IOReturn context_mark_backing_pending(VMsvga2PreparedBacking*);
	IOReturn context_mark_backing_future(VMsvga2PreparedBacking*,uint64_t);
	IOReturn context_acquire_backing_future(struct VIMSurfaceMapping*,struct VIMSurfaceFutureRead*);
	void context_release_backing_future();
	void context_cancel_ram_backing_loan(VMsvga2PreparedBacking*); // Begin failed before GPU use
	IOReturn fill_rectangles(uint32_t color,struct IOBlitRectangleStruct const* rects,size_t rectsSize);
	IOReturn copy_framebuffer_region_to_self(uint32_t framebufferIndex,
											 int destX,
											 int destY,
											 IOAccelDeviceRegion const* region,
											 size_t regionSize);
	IOReturn copy_self_region_to_framebuffer(uint32_t framebufferIndex,
											 int destX,
											 int destY,
											 IOAccelDeviceRegion const* region,
											 size_t regionSize);
	IOReturn copy_surface_region_to_self(class VMsvga2Surface* source_surface,
										 int destX,
										 int destY,
										 IOAccelDeviceRegion const* region,
										 size_t regionSize);
	IOReturn surface_video_off();
	IOReturn surface_flush_video(uint32_t* swapFlags);

	/*
	 * IOAccelSurfaceConnect
	 */
	IOReturn surface_read_lock_options(eIOAccelSurfaceLockBits options, IOAccelSurfaceInformation* info, size_t* infoSize);
	IOReturn surface_read_unlock_options(eIOAccelSurfaceLockBits options);
	IOReturn get_state(eIOAccelSurfaceStateBits* state);	// not called
	IOReturn surface_write_lock_options(eIOAccelSurfaceLockBits options, IOAccelSurfaceInformation* info, size_t* infoSize);
	IOReturn surface_write_unlock_options(eIOAccelSurfaceLockBits options);
	IOReturn surface_read(IOAccelSurfaceReadData const* parameters, size_t parametersSize);
	IOReturn set_shape_backing(eIOAccelSurfaceShapeBits options,
							   uintptr_t framebufferIndex,
							   IOVirtualAddress backing,
							   size_t rowbytes,
							   IOAccelDeviceRegion const* rgn,
							   size_t rgnSize);	// not called
	IOReturn set_id_mode(uintptr_t wID, eIOAccelSurfaceModeBits modebits);
	IOReturn set_scale(eIOAccelSurfaceScaleBits options,
					   IOAccelSurfaceScaling const* scaling,
					   size_t scalingSize);
	IOReturn set_shape(eIOAccelSurfaceShapeBits options,
					   uintptr_t framebufferIndex,
					   IOAccelDeviceRegion const* rgn,
					   size_t rgnSize);
	IOReturn surface_flush(uintptr_t framebufferMask, IOOptionBits options);
	IOReturn surface_query_lock();
	IOReturn surface_read_lock(IOAccelSurfaceInformation* info, size_t* infoSize);
	IOReturn surface_read_unlock();
	IOReturn surface_write_lock(IOAccelSurfaceInformation* info, size_t* infoSize);
	IOReturn surface_write_unlock();
	IOReturn surface_control(uintptr_t selector, uintptr_t arg, uint32_t* result);
	IOReturn set_shape_backing_length(eIOAccelSurfaceShapeBits options,
									  uintptr_t framebufferIndex,
									  IOVirtualAddress backing,
									  size_t rowbytes,
									  size_t backingLength,
									  IOAccelDeviceRegion const* rgn /* , size_t rgnSize */);
};

#endif /* __VMSVGA2SURFACE_H__ */
