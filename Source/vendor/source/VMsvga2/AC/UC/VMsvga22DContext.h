/*
 *  VMsvga22DContext.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 10th 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
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

#ifndef __VMSVGA22DCONTEXT_H__
#define __VMSVGA22DCONTEXT_H__

#include <IOKit/IOUserClient.h>
#include <libkern/c++/OSData.h>
#include "../../vminclude/vim_framebuffer.h"

typedef uintptr_t eIOContextModeBits;
struct IOSurfacePagingControlInfoStruct;
struct IOSurfaceVsyncControlInfoStruct;

class VMsvga22DContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga22DContext);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	int m_log_level;

	unsigned bTargetIsCGSSurface:1;
	class VMsvga2Surface* m_surface_client;
	uint32_t m_framebufferIndex;
	IOLock* m_direct_lock;
	void* m_direct_buffer;
	uint32_t m_direct_token;
	bool m_direct_read_supported;
	bool m_direct_write_supported;
	bool m_direct_scatter_supported;
	bool m_direct_ram_scatter_supported;
	bool m_direct_masked_scatter_supported;
	bool m_direct_ram_upload_supported;
	bool m_direct_bulk_upload_supported;
	bool m_direct_exchange_supported;
	bool m_direct_large_exchange_supported;
	bool m_direct_drained_close_supported;
	bool m_loan_quarantined;
	class VMsvga2PreparedBacking* m_backing_loan;
	class VMsvga2PreparedBacking* m_ram_access_owner; // normal/by-value page export reservation
	IOReturn reserve_ram_access(); // direct lock held; no native use before success
	void release_ram_access(); // after completed host use and/or drained close
	class VIMDeferredBacking* m_deferred_loan;
	class VIMWriterBacking* m_writer_loan;
	bool m_writer_continuation;
	IOReturn finish_writer_loan(unsigned,struct VIMSurfaceState*); // direct lock held
	struct VIMSurfaceVRAM m_loan_backing;
	uint64_t m_loan_serial;
	OSData* m_loan_pages;
	uint32_t m_loan_count;
	bool close_direct_stream(); // m_direct_lock held; true means completed retirement
	void release_backing_loan(); // only after completed GPU use/retirement
	IOReturn restore_deferred_loan(); // only after actual drained host use
	IOReturn finish_backing_loan(unsigned,struct VIMSurfaceState*); // m_direct_lock held
	IOMemoryDescriptor* m_scatter_memory;
	void* m_scatter_segments;
	uint32_t m_scatter_count,m_scatter_bytes,m_scatter_token,m_scatter_serial,m_scatter_capacity;
	bool m_scatter_masked;
	bool m_scatter_upload;
	bool m_scatter_access; // unknown-alias shared reservation until scatter retirement
	IOReturn prepare_direct_mask(void const*,size_t);
	bool m_surface_locked; // ownership, not merely the target's global lock bit
	bool m_surface_future; // separate immutable GPU READ loan, never CPU/context ownership
	struct VIMSurfaceFutureRead m_surface_future_read;
	bool m_owns_accel_updates; // only this client's successful acquisition is released

	IOReturn locateSurface(uint32_t surface_id);
	void clear_direct_scatter();
	bool prepare_direct_upload(IOMemoryDescriptor*,void*,uint32_t);

public:
	IOReturn direct_gl_scatter(void const*,void*,size_t,size_t*);
	IOReturn direct_gl_scatter_locked(void const*,void*,size_t,size_t*);
	IOReturn framebuffer_blit_control(uintptr_t,void*,size_t*);
	/*
	 * Methods overridden from superclass
	 */
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOExternalTrap* getTargetAndTrapForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	void free();
#if 0
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
#endif
	bool start(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga22DContext* withTask(task_t owningTask, void* securityToken, uint32_t type);

	/*
	 * GA Support Methods
	 */
	IOReturn useAccelUpdates(uintptr_t state);
	IOReturn RectCopy(struct IOBlitCopyRectangleStruct const* copyRects,
					  size_t copyRectsSize);
	IOReturn RectFill(uintptr_t color,
					  struct IOBlitRectangleStruct const* rects,
					  size_t rectsSize);
	IOReturn CopyRegion(uintptr_t source_surface_id,
						intptr_t destX,
						intptr_t destY,
						IOAccelDeviceRegion const* region,
						size_t regionSize);

	/*
	 * Methods corresponding to Apple's GeForce.kext 2D Context User Client
	 */
	/*
	 * IONV2DContext
	 */
	IOReturn set_surface(uintptr_t surface_id, eIOContextModeBits options, void* struct_out, size_t* struct_out_size);
	IOReturn get_config(uint32_t* config_1, uint32_t* config_2);
	IOReturn get_surface_info1(uintptr_t, eIOContextModeBits, void*, size_t*);
	IOReturn swap_surface(uintptr_t options, uint32_t* swapFlags);
	IOReturn scale_surface(uintptr_t options, uintptr_t width, uintptr_t height);
	IOReturn lock_memory(uintptr_t options, uint64_t* struct_out, size_t* struct_out_size);
	IOReturn unlock_memory(uintptr_t options, uint32_t* swapFlags);
	IOReturn lock_surface_backing(void*, size_t*);
	IOReturn wait_surface_backing(void*, size_t*);
	IOReturn get_surface_clip(void*, size_t*);
	IOReturn get_surface_geometry(void*, size_t*);
	IOReturn get_surface_drawing_size(void*, size_t*);
	IOReturn get_surface_pixel_format(void*, size_t*);
	IOReturn get_vram_surface(uintptr_t,void*,size_t*);
	IOReturn get_ram_surface(uintptr_t,uintptr_t,void*,size_t*);
	IOReturn begin_ram_backing_loan(void const*,void*,size_t,size_t*);
	IOReturn begin_deferred_ram_loan(void const*,void*,size_t,size_t*);
	IOReturn commit_deferred_ram_loan(void const*,void*,size_t,size_t*);
	IOReturn commit_future_ram_loan(void const*,void*,size_t,size_t*);
	IOReturn commit_pending_ram_loan(void const*,void*,size_t,size_t*,bool);
	IOReturn lock_future_gpu_surface(void*,size_t*);
	IOReturn get_future_gpu_surface(void*,size_t*);
	IOReturn begin_writer_ram_loan(void const*,void*,size_t,size_t*);
	IOReturn continue_writer_ram_loan(void const*,void*,size_t,size_t*);
	IOReturn commit_writer_ram_loan(void const*,void*,size_t,size_t*);
	IOReturn end_writer_ram_loan(uintptr_t,uintptr_t,void*,size_t*);
	IOReturn get_ram_backing_loan(uintptr_t,uintptr_t,void*,size_t*);
	IOReturn end_ram_backing_loan(uintptr_t,uintptr_t,void*,size_t*);
	IOReturn cancel_surface_backing_loan(class VMsvga2Surface*); // called without surface content lock
	IOReturn get_surface_state(void*, size_t*);
	IOReturn unlock_surface_backing(uintptr_t, void*, size_t*);
	IOReturn direct_gl_transfer(void const*,void*,size_t,size_t*);
	IOReturn direct_gl_exchange(void const*,void*,size_t,size_t*);
	IOReturn direct_gl_exchange_trap(void*,void*,void*,void*,void*,void*);
	IOReturn surface_service_probe(void const*,void*,size_t,size_t*);
	IOReturn materialize_surface(void const*,void*,size_t,size_t*);
	IOReturn defer_surface(void const*,void*,size_t,size_t*);
	IOReturn lock_gpu_surface(void*,size_t*);
	IOReturn finish(uintptr_t options);
	IOReturn declare_image(uint64_t const*, uint64_t*, size_t, size_t*);
	IOReturn create_image(uintptr_t, uintptr_t, uint64_t*, size_t*);
	IOReturn create_transfer(uintptr_t, uintptr_t, uint64_t*, size_t*);
	IOReturn delete_image(uintptr_t image_id);
	IOReturn wait_image(uintptr_t image_id);
	IOReturn set_surface_paging_options(IOSurfacePagingControlInfoStruct const*,
										IOSurfacePagingControlInfoStruct*,
										size_t,
										size_t*);
	IOReturn set_surface_vsync_options(IOSurfaceVsyncControlInfoStruct const*,
									   IOSurfaceVsyncControlInfoStruct*,
									   size_t,
									   size_t*);
	IOReturn set_macrovision(uintptr_t new_state);

	/*
	 * NV2DContext
	 */
	IOReturn read_configs(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn read_config_Ex(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn get_surface_info2(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn kernel_printf(char const*, size_t);
};

#endif /* __VMSVGA22DCONTEXT_H__ */
