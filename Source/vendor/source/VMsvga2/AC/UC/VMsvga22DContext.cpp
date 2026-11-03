/*
 *  VMsvga22DContext.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 10th 2009.
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

#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/task.h>
#include <libkern/libkern.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "PhysicalSegment.h"
#include "VMsvga2Surface.h"
#include "VMsvga22DContext.h"
#include "UCMethods.h"
#include "../../vminclude/vim_passthrough_opengl.h"
#include "../../vminclude/vim_gl_surface_service.h"
#include "../../vminclude/vim_handoff_profile.h"

#include "svga_apple_header.h"
#include "svga_overlay.h"
#include "svga_apple_footer.h"

#define CLASS VMsvga22DContext
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga22DContext, IOUserClient);

#if LOGGING_LEVEL >= 1
#define TDLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IO2D: ", ##__VA_ARGS__); } while (false)
#else
#define TDLog(log_level, ...)
#endif

#define HIDDEN __attribute__((visibility("hidden")))

static
IOExternalMethod const iofbFuncsCache[kIOVM2DNumMethods] =
{
// Note: methods from IONV2DContext
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_config), kIOUCScalarIScalarO, 0, 2},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info1), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::swap_surface), kIOUCScalarIScalarO, 1, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::scale_surface), kIOUCScalarIScalarO, 3, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::lock_memory), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::unlock_memory), kIOUCScalarIScalarO, 1, 1},
{0, reinterpret_cast<IOMethod>(&CLASS::finish), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::declare_image), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::create_image), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::create_transfer), kIOUCScalarIStructO, 2, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::delete_image), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_image), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_paging_options), kIOUCStructIStructO, 12, 12},
{0, reinterpret_cast<IOMethod>(&CLASS::set_surface_vsync_options), kIOUCStructIStructO, 12, 12},
{0, reinterpret_cast<IOMethod>(&CLASS::set_macrovision), kIOUCScalarIScalarO, 1, 0},
// Note: Methods from NV2DContext
{0, reinterpret_cast<IOMethod>(&CLASS::read_configs), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::read_config_Ex), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_info2), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::kernel_printf), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
// Note: VM Methods
{0, reinterpret_cast<IOMethod>(&CLASS::CopyRegion), kIOUCScalarIStructI, 3, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::useAccelUpdates), kIOUCScalarIScalarO, 1, 0},
{0, reinterpret_cast<IOMethod>(&CLASS::RectCopy), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::RectFill), kIOUCScalarIStructI, 1, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&VMsvga2Accel::UpdateFramebufferAutoRing), kIOUCScalarIStructI, 0, 4U * sizeof(UInt32)},
{0, reinterpret_cast<IOMethod>(&CLASS::lock_surface_backing), kIOUCStructIStructO, 0, sizeof(struct VIMSurfaceMapping)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_clip), kIOUCStructIStructO, 0, sizeof(struct VIMSurfaceClip)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_geometry), kIOUCStructIStructO, 0, sizeof(struct VIMSurfaceGeometry)},
{0, reinterpret_cast<IOMethod>(&CLASS::direct_gl_transfer), kIOUCStructIStructO, sizeof(struct VIMGLTransfer), sizeof(struct VIMGLResult)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_state), kIOUCStructIStructO, 0, sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::unlock_surface_backing), kIOUCScalarIStructO, 1, sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::direct_gl_scatter), kIOUCStructIStructO, kIOUCVariableStructureSize, sizeof(struct VIMGLScatterResult)},
{0, reinterpret_cast<IOMethod>(&CLASS::framebuffer_blit_control), kIOUCScalarIStructO, 1, sizeof(struct VIMGLFramebufferStats)},
{0, reinterpret_cast<IOMethod>(&CLASS::surface_service_probe), kIOUCStructIStructO, sizeof(struct VIMGLServiceProbe), 16},
{0, reinterpret_cast<IOMethod>(&CLASS::materialize_surface), kIOUCStructIStructO, sizeof(struct VIMSurfaceMaterialize), sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::defer_surface), kIOUCStructIStructO, sizeof(struct VIMSurfaceMaterialize), sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::lock_gpu_surface), kIOUCScalarIStructO, 0, sizeof(struct VIMSurfaceMapping)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_vram_surface), kIOUCScalarIStructO, 1, sizeof(struct VIMSurfaceVRAM)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_ram_surface), kIOUCScalarIStructO, 2, sizeof(struct VIMSurfaceRAM)},
{0, reinterpret_cast<IOMethod>(&CLASS::direct_gl_exchange), kIOUCStructIStructO, kIOUCVariableStructureSize, kIOUCVariableStructureSize},
{0, reinterpret_cast<IOMethod>(&CLASS::wait_surface_backing), kIOUCStructIStructO, 0, sizeof(struct VIMSurfaceMapping)},
{0, reinterpret_cast<IOMethod>(&CLASS::begin_ram_backing_loan), kIOUCStructIStructO, sizeof(struct VIMSurfaceVRAM), sizeof(struct VIMSurfaceBackingLoan)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_ram_backing_loan), kIOUCScalarIStructO, 2, sizeof(struct VIMSurfaceRAM)},
{0, reinterpret_cast<IOMethod>(&CLASS::end_ram_backing_loan), kIOUCScalarIStructO, 2, sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::begin_deferred_ram_loan), kIOUCStructIStructO, sizeof(struct VIMSurfaceVRAM), sizeof(struct VIMSurfaceBackingLoan)},
{0, reinterpret_cast<IOMethod>(&CLASS::commit_deferred_ram_loan), kIOUCStructIStructO, sizeof(struct VIMSurfaceBackingPending), sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::commit_future_ram_loan), kIOUCStructIStructO, sizeof(struct VIMSurfaceBackingPending), sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::lock_future_gpu_surface), kIOUCStructIStructO, 0, sizeof(struct VIMSurfaceMapping)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_future_gpu_surface), kIOUCStructIStructO, 0, sizeof(struct VIMSurfaceFutureRead)},
{0, reinterpret_cast<IOMethod>(&CLASS::begin_writer_ram_loan), kIOUCStructIStructO, sizeof(struct VIMSurfaceVRAM), sizeof(struct VIMSurfaceBackingLoan)},
{0, reinterpret_cast<IOMethod>(&CLASS::continue_writer_ram_loan), kIOUCStructIStructO, sizeof(struct VIMFramebufferLayout), sizeof(struct VIMSurfaceWriterLoan)},
{0, reinterpret_cast<IOMethod>(&CLASS::commit_writer_ram_loan), kIOUCStructIStructO, sizeof(struct VIMSurfaceBackingPending), sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::end_writer_ram_loan), kIOUCScalarIStructO, 2, sizeof(struct VIMSurfaceState)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_drawing_size), kIOUCStructIStructO, 0, sizeof(struct VIMSurfaceDrawingSize)},
{0, reinterpret_cast<IOMethod>(&CLASS::get_surface_pixel_format), kIOUCStructIStructO, 0, sizeof(struct VIMSurfacePixelFormat)}
};

struct VIM2DDirectGuard {
    IOLock* lock;
    explicit VIM2DDirectGuard(IOLock* value):lock(value){IOLockLock(lock);}
    ~VIM2DDirectGuard(){IOLockUnlock(lock);}
};

IOReturn CLASS::get_surface_drawing_size(void* output,size_t* size)
{
    if(!output || !size || *size!=sizeof(struct VIMSurfaceDrawingSize))return kIOReturnBadArgument;
    if(!m_direct_lock)return kIOReturnNotReady;
    VIM2DDirectGuard guard(m_direct_lock);
    if(!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked || m_surface_future)
        return kIOReturnNotOpen;
    return m_surface_client->context_get_drawing_size(static_cast<struct VIMSurfaceDrawingSize*>(output));
}

IOReturn CLASS::get_surface_pixel_format(void* output,size_t* size)
{
    if(!output || !size || *size!=sizeof(struct VIMSurfacePixelFormat))return kIOReturnBadArgument;
    if(!m_direct_lock)return kIOReturnNotReady;
    VIM2DDirectGuard guard(m_direct_lock);
    if(!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked || m_surface_future)
        return kIOReturnNotOpen;
    return m_surface_client->context_get_pixel_format(static_cast<struct VIMSurfacePixelFormat*>(output));
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOReturn CLASS::framebuffer_blit_control(uintptr_t mode,void* output,size_t* size)
{
    return m_provider?m_provider->directFramebufferControl(mode,output,size):kIOReturnNotReady;
}

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (!targetP || index >= kIOVM2DNumMethods)
		return 0;
	switch (index) {
		case kIOVM2DUpdateFramebuffer:
			if (m_provider)
				*targetP = m_provider;
			else
				return 0;
			break;
		default:
			*targetP = this;
			break;
	}
	return const_cast<IOExternalMethod*>(&iofbFuncsCache[index]);
}

IOReturn CLASS::clientClose()
{
	TDLog(2, "%s\n", __FUNCTION__);
    if (m_direct_lock) IOLockLock(m_direct_lock);
    bool drained=close_direct_stream();
    if(drained && restore_deferred_loan()!=kIOReturnSuccess)drained=false;
    if(drained && m_writer_loan){VIMSurfaceState ignored;if(finish_writer_loan(VIM_RAM_LOAN_ABORT,&ignored))drained=false;}
    if((m_backing_loan || m_writer_loan) && !drained){
        // Never unpin memory or return CPU ownership on a cancellation-only
        // ACK, missing device, or failed retirement. Keep this bounded loan
        // quarantined until VM reset rather than risk writes into reused RAM.
        if(!m_loan_quarantined){m_loan_quarantined=true;retain();IOLog("VMsvga2: backing loan retirement failed; guest restart required\n");}
        if(m_direct_lock)IOLockUnlock(m_direct_lock);
        return kIOReturnIOError;
    }
    clear_direct_scatter();
    if (m_direct_buffer && m_provider) {
        m_provider->VRAMFree(m_direct_buffer);m_direct_buffer=0;m_direct_token=0;
    }
	if (m_surface_client) {
		if (m_surface_locked) {
			if(m_surface_future){m_surface_client->context_release_backing_future();m_surface_future=false;}
			else {
			// A failed deferred read must not leave the context ownership bit
			// orphaned after this client dies. Read-only unlock drops its mapping
			// while retaining GPU-newer state for a later successful CPU acquire.
			if(m_surface_client->context_unlock_memory(0,false,0,m_backing_loan))
				m_surface_client->context_unlock_memory(0,true,0,m_backing_loan);
			}
			m_surface_locked=false;
		}
		m_surface_client->release();
		m_surface_client = 0;
	}
    release_backing_loan();
	m_framebufferIndex = 0;
	bTargetIsCGSSurface = false;
	if (m_provider && m_owns_accel_updates) {
		m_provider->useAccelUpdates(0, m_owning_task);
		m_owns_accel_updates = false;
	}
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	m_provider = 0;
    if (m_direct_lock) IOLockUnlock(m_direct_lock);
	return kIOReturnSuccess;
}
void CLASS::free()
{
    clear_direct_scatter();
    if (m_direct_lock) { IOLockFree(m_direct_lock);m_direct_lock=0; }
    super::free();
}
IOReturn CLASS::surface_service_probe(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if(!m_provider || !input || input_size!=sizeof(VIMGLServiceProbe) || !output || !output_size || *output_size!=16)return kIOReturnBadArgument;
    VIMGLServiceProbe request=*static_cast<VIMGLServiceProbe const*>(input);
    uint32_t hold=0;
    IOReturn error=m_provider->holdGPUSurface(request.namespace_id,request.object,request.incarnation,request.width,request.height,&hold);
    if(error)return error;
    IOBufferMemoryDescriptor* memory=IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,kIODirectionIn,16);
    if(!memory)error=kIOReturnNoMemory;
    else {
        error=memory->prepare();
        if(!error) {
            error=m_provider->readGPUSurface(hold,request.epoch,request.x,request.y,2,2,memory,0,8);
            if(!error)memcpy(output,memory->getBytesNoCopy(),16);
            memory->complete();
        }
        memory->release();
    }
    IOReturn released=m_provider->releaseGPUSurface(hold);
    return error?error:released;
}
IOReturn CLASS::direct_gl_transfer(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if (!input || input_size!=sizeof(VIMGLTransfer) || !output || !output_size ||
        *output_size!=sizeof(VIMGLResult) || !m_direct_lock) return kIOReturnBadArgument;
    VIMGLTransfer request=*static_cast<VIMGLTransfer const*>(input);
    if (request.action<1 || request.action>3 || request.bytes>VIM_GL_CHUNK ||
        (request.action==1 && (request.bytes || request.address)) ||
        (request.action!=1 && (!request.bytes || !request.address || request.address+request.bytes<request.address)))
        return kIOReturnBadArgument;
    IOLockLock(m_direct_lock);
    IOReturn error=kIOReturnSuccess;
    if (!m_provider || (request.action==1 && !m_provider->directGLAvailable())) { IOLockUnlock(m_direct_lock);return kIOReturnUnsupported; }
    if (!m_direct_buffer) m_direct_buffer=m_provider->VRAMMalloc(sizeof(VIMGLDevicePacket)+VIM_GL_CHUNK);
    if (!m_direct_buffer) { IOLockUnlock(m_direct_lock);return kIOReturnNoMemory; }
    /* A small owning-task command write is a snapshot, not an exported RAM
     * loan. The synchronous device call consumes the copied VRAM bytes before
     * returning. Avoid constructing/preparing/releasing a task memory
     * descriptor for every metadata batch; copyin faults/validates the same
     * bounded source before ANY stream bytes are submitted. Keep descriptor
     * loans for bulk/direct uploads and ALL reads (including their existing
     * pre-consumption destination validation). A port used from a different
     * task must still address m_owning_task, not that caller's address space.
     * No guest task address is ever handed to the host interpreter.
     */
    bool small_write=request.action==2 && request.bytes<=m_provider->directGLCommandCopyLimit() &&
        m_owning_task && current_task()==m_owning_task;
    IOMemoryDescriptor* memory=0;bool prepared=false;
    if (request.action!=1) {
        if (!m_direct_token) error=kIOReturnNotOpen;
        else if(!small_write) {
            memory=IOMemoryDescriptor::withAddressRange(request.address,request.bytes,
                request.action==2 ? kIODirectionOut : kIODirectionIn,m_owning_task);
            if (!memory) error=kIOReturnNoMemory;
            else { error=memory->prepare();prepared=!error; }
        }
    } else if (m_direct_token) error=kIOReturnExclusiveAccess;
    VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_direct_buffer);
    if (!error) {
        bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;
        packet->action=request.action;packet->bytes=request.bytes;packet->token=m_direct_token;packet->result=-1;
        if (request.action==1) {
            m_direct_read_supported=m_provider->directGLDirectReadAvailable();
            m_direct_write_supported=m_provider->directGLDirectWriteAvailable();
            m_direct_scatter_supported=m_provider->directGLScatterAvailable();
            m_direct_ram_scatter_supported=m_direct_scatter_supported && m_provider->directGLRAMScatterAvailable();
            m_direct_masked_scatter_supported=m_direct_ram_scatter_supported && m_provider->directGLMaskedScatterAvailable();
            m_direct_ram_upload_supported=m_provider->directGLRAMUploadAvailable();
            m_direct_bulk_upload_supported=m_direct_ram_upload_supported && m_provider->directGLBulkUploadAvailable();
            m_direct_exchange_supported=m_provider->directGLExchangeAvailable();
            m_direct_large_exchange_supported=m_direct_exchange_supported && m_provider->directGLLargeExchangeAvailable();
        }
        if (request.action==3 && m_direct_read_supported &&
            m_provider->directGLReadOffset(memory,request.bytes,&packet->reserved)) packet->action=5;
        if (request.action==2 && memory && m_direct_write_supported &&
            m_provider->directGLReadOffset(memory,request.bytes,&packet->reserved)) packet->action=6;
        if (request.action==2 && memory && packet->action!=6 && m_direct_ram_upload_supported &&
            m_provider->directGLRAMUploadEnabled())prepare_direct_upload(memory,packet,request.bytes);
        if (request.action==2 && small_write) {
            if(copyin(request.address,packet+1,request.bytes))error=kIOReturnVMError;
        } else if (request.action==2 && packet->action!=6 && packet->action!=10 &&
            memory->readBytes(0,packet+1,request.bytes)!=request.bytes) error=kIOReturnVMError;
        if (!error) error=m_provider->directGLTransfer(packet);
        if (!error && packet->result<0) error=kIOReturnIOError;
        if (!error && request.action!=1 && static_cast<uint32_t>(packet->result)>request.bytes) error=kIOReturnVMError;
        if (!error && request.action==1){
            m_direct_token=packet->token;
            m_direct_drained_close_supported=!!(packet->reserved&VIM_GL_RESULT_CAP_DRAINED_CLOSE);
        }
        if (!error && request.action==3 && (static_cast<uint32_t>(packet->result)>request.bytes ||
            (packet->action!=5 && memory->writeBytes(0,packet+1,packet->result)!=static_cast<unsigned>(packet->result))))
            error=kIOReturnVMError;
    }
    if (memory) { if (prepared) memory->complete();memory->release(); }
    if (!error) {
        VIMGLResult result={packet->result,packet->action==5 ? VIM_GL_RESULT_DIRECT_READ :
            (packet->action==6 || packet->action==10) ? VIM_GL_RESULT_DIRECT_WRITE : 0};
        if (request.action==1 && m_direct_scatter_supported)result.reserved|=VIM_GL_RESULT_CAP_SCATTER;
        if (request.action==1 && m_direct_ram_scatter_supported)result.reserved|=VIM_GL_RESULT_CAP_RAM_SCATTER;
        if (request.action==1 && m_direct_masked_scatter_supported)result.reserved|=VIM_GL_RESULT_CAP_MASKED_SCATTER;
        if (request.action==1 && m_direct_ram_scatter_supported && m_direct_ram_upload_supported)result.reserved|=VIM_GL_RESULT_CAP_ROW_UPLOAD;
        if (request.action==1 && m_direct_ram_scatter_supported && m_direct_ram_upload_supported)result.reserved|=VIM_GL_RESULT_CAP_ROW_UPLOAD_TRANSACTION;
        if (request.action==1 && m_direct_exchange_supported)result.reserved|=VIM_GL_RESULT_CAP_EXCHANGE;
        if (request.action==1 && m_direct_exchange_supported)result.reserved|=VIM_GL_RESULT_CAP_EXCHANGE_TRAP;
        if (request.action==1 && m_direct_large_exchange_supported)result.reserved|=VIM_GL_RESULT_CAP_LARGE_EXCHANGE;
        if (request.action==1 && m_direct_drained_close_supported)result.reserved|=VIM_GL_RESULT_CAP_DRAINED_CLOSE;
        memcpy(output,&result,sizeof(result));
    }
    IOLockUnlock(m_direct_lock);return error;
}

IOReturn CLASS::direct_gl_exchange(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if(!input || !output || !output_size || input_size<sizeof(VIMGLExchange) ||
        input_size>VIM_GL_EXCHANGE_LIMIT || !m_direct_lock)return kIOReturnBadArgument;
    VIMGLExchange request=*static_cast<VIMGLExchange const*>(input);
    if(request.version!=1 || request.reserved || !request.send_bytes || !request.receive_bytes ||
        request.send_bytes!=input_size-sizeof(request) || request.receive_bytes>VIM_GL_EXCHANGE_LIMIT ||
        request.receive_bytes!=*output_size)return kIOReturnBadArgument;
    IOLockLock(m_direct_lock);IOReturn error=kIOReturnSuccess;
    if(!m_provider || !m_direct_exchange_supported)error=kIOReturnUnsupported;
    else if(!m_direct_token || !m_direct_buffer)error=kIOReturnNotOpen;
    else {
        VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_direct_buffer);
        bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->action=13;
        packet->token=m_direct_token;packet->bytes=request.send_bytes;
        packet->reserved=request.receive_bytes;packet->result=-1;
        memcpy(packet+1,static_cast<VIMGLExchange const*>(input)+1,request.send_bytes);
        error=m_provider->directGLTransfer(packet);
        if(!error && packet->result!=static_cast<int32_t>(request.receive_bytes))error=kIOReturnIOError;
        if(!error)memcpy(output,packet+1,request.receive_bytes);
    }
    IOLockUnlock(m_direct_lock);return error;
}

/* Mavericks xnu-2422.115.4 IOUserClient.cpp's iokit_user_client_trap retains
 * this client and invokes its six-pointer member on the calling task. Unlike
 * a MIG structure method, it does NOT snapshot/validate user buffers for us.
 * Copy only bounded owning-task bytes, under the SAME ordered stream lock.
 * No user pointer reaches QEMU or the host executor; no per-query allocation,
 * mapping, pinning, polling, new protocol or speculative GL result is needed.
 */
IOExternalTrap* CLASS::getTargetAndTrapForIndex(IOService** targetP,UInt32 index)
{
    static const IOExternalTrap trap={0,reinterpret_cast<IOTrap>(&CLASS::direct_gl_exchange_trap)};
    if(!targetP || index!=VIM_GL_EXCHANGE_TRAP)return 0;
    *targetP=this;return const_cast<IOExternalTrap*>(&trap);
}

IOReturn CLASS::direct_gl_exchange_trap(void* input,void* input_size_arg,void* output,
    void* output_size_arg,void* reserved0,void* reserved1)
{
    size_t input_size=reinterpret_cast<uintptr_t>(input_size_arg);
    size_t output_size=reinterpret_cast<uintptr_t>(output_size_arg);
    user_addr_t input_address=reinterpret_cast<user_addr_t>(input);
    user_addr_t output_address=reinterpret_cast<user_addr_t>(output);
    if(!input || !output || reserved0 || reserved1 || input_size<sizeof(VIMGLExchange) ||
        input_size>(m_direct_large_exchange_supported?VIM_GL_LARGE_EXCHANGE_LIMIT:VIM_GL_EXCHANGE_LIMIT) ||
        !output_size || output_size>VIM_GL_EXCHANGE_LIMIT ||
        input_address+input_size<input_address || output_address+output_size<output_address ||
        !m_direct_lock)return kIOReturnBadArgument;
    IOLockLock(m_direct_lock);IOReturn error=kIOReturnSuccess;
    VIMGLExchange request;
    if(!m_owning_task || current_task()!=m_owning_task)error=kIOReturnNotPermitted;
    else if(!m_provider || !m_direct_exchange_supported)error=kIOReturnUnsupported;
    else if(!m_direct_token || !m_direct_buffer)error=kIOReturnNotOpen;
    else if(copyin(input_address,&request,sizeof(request)))error=kIOReturnVMError;
    else if(request.version!=1 || request.reserved || !request.send_bytes ||
        request.send_bytes!=input_size-sizeof(request) || request.receive_bytes!=output_size)
        error=kIOReturnBadArgument;
    else {
        VIMGLDevicePacket* packet=static_cast<VIMGLDevicePacket*>(m_direct_buffer);
        bzero(packet,sizeof(*packet));packet->magic=VIM_GL_MAGIC;packet->action=13;
        packet->token=m_direct_token;packet->bytes=request.send_bytes;
        packet->reserved=request.receive_bytes;packet->result=-1;
        if(copyin(input_address+sizeof(request),packet+1,request.send_bytes))
            error=kIOReturnVMError;
        else {
            error=m_provider->directGLTransfer(packet);
            if(!error && packet->result!=static_cast<int32_t>(request.receive_bytes))error=kIOReturnIOError;
            if(!error && copyout(packet+1,output_address,request.receive_bytes))
                error=kIOReturnVMError;
        }
    }
    IOLockUnlock(m_direct_lock);return error;
}

#if 0
IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	TDLog(2, "%s(%u, options_out, memory_out)\n", __FUNCTION__, static_cast<unsigned>(type));
	return super::clientMemoryForType(type, options, memory);
}
#endif

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider)
		return false;
	m_log_level = imax(m_provider->getLogLevelGA(), m_provider->getLogLevelAC());
	return super::start(provider);
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	m_log_level = LOGGING_LEVEL;
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	m_owns_accel_updates = false;
	m_surface_future=false;bzero(&m_surface_future_read,sizeof(m_surface_future_read));
	m_ram_access_owner=0;m_scatter_access=false;
	m_writer_loan=0;m_writer_continuation=false;
	m_direct_read_supported=false;
	m_direct_write_supported=false;
	m_direct_scatter_supported=false;
	m_direct_ram_scatter_supported=false;
	m_direct_masked_scatter_supported=false;
	m_direct_ram_upload_supported=false;
	m_direct_bulk_upload_supported=false;
	m_direct_exchange_supported=false;m_direct_large_exchange_supported=false;
	m_direct_drained_close_supported=false;m_loan_quarantined=false;m_backing_loan=0;m_deferred_loan=0;
	m_loan_serial=0;m_loan_pages=0;m_loan_count=0;bzero(&m_loan_backing,sizeof(m_loan_backing));
	m_scatter_memory=0;m_scatter_segments=0;
	m_scatter_count=m_scatter_bytes=m_scatter_token=m_scatter_serial=m_scatter_capacity=0;
	m_scatter_masked=false;
	m_scatter_upload=false;
	m_direct_lock = IOLockAlloc();
	if (!m_direct_lock) return false;
	return true;
}

#include "VMsvga22DScatter.inc"
#include "VMsvga22DDeferredBacking.inc"
#include "VMsvga22DBackingLoan.inc"
#include "VMsvga22DWriterBacking.inc"

HIDDEN
CLASS* CLASS::withTask(task_t owningTask, void* securityToken, uint32_t type)
{
	CLASS* inst;

	inst = new CLASS;

	if (inst && !inst->initWithTask(owningTask, securityToken, type))
	{
		inst->release();
		inst = 0;
	}

	return (inst);
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

HIDDEN
IOReturn CLASS::locateSurface(uint32_t surface_id)
{
	if (!m_provider)
		return kIOReturnNotReady;
	m_surface_client = m_provider->findSurfaceForID(surface_id);
	if (!m_surface_client)
		return kIOReturnNotFound;
	m_surface_client->retain();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark GA Support Methods
#pragma mark -

HIDDEN
IOReturn CLASS::useAccelUpdates(uintptr_t state)
{
	if (!m_direct_lock)
		return kIOReturnNotReady;
	IOLockLock(m_direct_lock);
	IOReturn result = kIOReturnNotReady;
	if (m_provider) {
		if ((state != 0) == m_owns_accel_updates) result = kIOReturnSuccess;
		else {
			result = m_provider->useAccelUpdates(state != 0, m_owning_task);
			if (!result) m_owns_accel_updates = state != 0;
		}
	}
	IOLockUnlock(m_direct_lock);
	return result;
}

HIDDEN
IOReturn CLASS::RectCopy(struct IOBlitCopyRectangleStruct const* copyRects,
						 size_t copyRectsSize)
{
	if (bTargetIsCGSSurface) {
		TDLog(1, "%s: called with surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_provider)
		return kIOReturnNotReady;
	return m_provider->RectCopy(m_framebufferIndex, copyRects, copyRectsSize);
}

HIDDEN
IOReturn CLASS::RectFill(uintptr_t color,
						 struct IOBlitRectangleStruct const* rects,
						 size_t rectsSize)
{
	if (bTargetIsCGSSurface) {
		if (!m_surface_client) return kIOReturnNotReady;
		return m_surface_client->fill_rectangles(static_cast<uint32_t>(color),rects,rectsSize);
	}
	if (!m_provider)
		return kIOReturnNotReady;
	return m_provider->RectFill(m_framebufferIndex, static_cast<uint32_t>(color), rects, rectsSize);
}

HIDDEN
IOReturn CLASS::CopyRegion(uintptr_t source_surface_id,
						   intptr_t destX,
						   intptr_t destY,
						   IOAccelDeviceRegion const* region,
						   size_t regionSize)
{
	IOReturn rc;
	uint32_t _source_surface_id;
	int _destX, _destY;
	VMsvga2Surface* source_surface;

	if (!region || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(region))
		return kIOReturnBadArgument;

	if (!m_provider)
		return kIOReturnNotReady;

	if (bTargetIsCGSSurface && !m_surface_client)
		return kIOReturnNotReady;

	/*
	 * Correct for truncation done by IOUserClient dispatcher
	 */
	_source_surface_id = static_cast<uint32_t>(source_surface_id);
	_destX = static_cast<int>(destX);
	_destY = static_cast<int>(destY);

	if (_source_surface_id) {
		source_surface = m_provider->findSurfaceForID(_source_surface_id);
		if (!source_surface) {
			TDLog(1, "%s: Source Surface(%#x) not found\n", __FUNCTION__, _source_surface_id);
			return kIOReturnNotFound;
		}
		source_surface->retain();
		if (bTargetIsCGSSurface)
			rc = m_surface_client->copy_surface_region_to_self(source_surface,
															   _destX,
															   _destY,
															   region,
															   regionSize);
		else
			rc = source_surface->copy_self_region_to_framebuffer(m_framebufferIndex,
																 _destX,
																 _destY,
																 region,
																 regionSize);
		source_surface->release();
		return rc;
	}
	if (!bTargetIsCGSSurface) {
		/*
		 * destination is framebuffer, use classic mode
		 */
		if (!m_provider)
			return kIOReturnNotReady;
		return m_provider->CopyRegion(m_framebufferIndex, _destX, _destY, region, regionSize);
	}
	/*
	 * destination is a surface
	 */
	return m_surface_client->copy_framebuffer_region_to_self(m_framebufferIndex,
															 _destX,
															 _destY,
															 region,
															 regionSize);
}

#pragma mark -
#pragma mark IONV2DContext Methods
#pragma mark -

HIDDEN
IOReturn CLASS::set_surface(uintptr_t surface_id,
							eIOContextModeBits options,
							void* struct_out,
							size_t* struct_out_size)
{
	if(!m_direct_lock)return kIOReturnNotReady;
	VIM2DDirectGuard guard(m_direct_lock);
	uint32_t vmware_pixel_format, apple_pixel_format;
	IOReturn rc;

	if (!struct_out || !struct_out_size)
		return kIOReturnBadArgument;
	if (m_surface_locked || m_writer_loan)
		return kIOReturnCannotLock;
	bzero(struct_out, *struct_out_size);
	if (m_surface_client) {
		m_surface_client->release();
		m_surface_client = 0;
	}
	/*
	 * options == 0x800 -- has surface id
	 *            0x400 -- UYVY format ('2vuy' for Apple)
	 *			  0x200 -- YUY2 format ('yuvs' for Apple)
	 *			  0x100 -- BGRA format
	 *            0 -- framebuffer, surface_id is framebufferIndex
	 */
	if (!(options & 0x800U)) {
		/*
		 * set target to framebuffer
		 */
		bTargetIsCGSSurface = false;
		m_framebufferIndex = static_cast<uint32_t>(surface_id);
		return kIOReturnSuccess;
	}
	bTargetIsCGSSurface = true;
	/*
	 * Note: VMWARE_FOURCC_YV12 is not supported (planar 4:2:0 format)
	 */
	if (options & 0x400U) {
		vmware_pixel_format = VMWARE_FOURCC_UYVY;
		apple_pixel_format = kIO2vuyPixelFormat;
	} else if (options & 0x200U) {
		vmware_pixel_format = VMWARE_FOURCC_YUY2;
		apple_pixel_format = kIOYUVSPixelFormat;
	} else if (options & 0x100U) {
		vmware_pixel_format = 0;
		apple_pixel_format = kIO32BGRAPixelFormat;
	} else {
		vmware_pixel_format = 0;
		apple_pixel_format = 0;
	}
	rc = locateSurface(static_cast<uint32_t>(surface_id));
	if (rc != kIOReturnSuccess)
		return rc;
	return m_surface_client->context_set_surface(vmware_pixel_format, apple_pixel_format);
}

HIDDEN
IOReturn CLASS::get_config(uint32_t* config_1, uint32_t* config_2)
{
	if (!config_1 || !config_2)
		return kIOReturnBadArgument;
	*config_1 = 0U;
	/*
	 * TBD: transfer m_provider->getOptionsGA() as well
	 */
	if (m_provider)
		*config_2 = static_cast<uint32_t>(m_provider->getLogLevelGA());
	else
		*config_2 = 0U;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_surface_info1(uintptr_t c1, eIOContextModeBits c2, void* struct_out, size_t* struct_out_size)
{
	TDLog(2, "%s(%lu, %lu, struct_out, %lu)\n", __FUNCTION__, c1, c2, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::swap_surface(uintptr_t options, uint32_t* swapFlags)
{
	if (!bTargetIsCGSSurface) {
		TDLog(2, "%s: called with non-surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_surface_client)
		return kIOReturnNotReady;
	m_surface_client->surface_flush_video(swapFlags);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::scale_surface(uintptr_t options, uintptr_t width, uintptr_t height)
{
	if (!bTargetIsCGSSurface) {
		TDLog(2, "%s: called with non-surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_surface_client)
		return kIOReturnNotReady;
	return m_surface_client->context_scale_surface(static_cast<IOOptionBits>(options),
												   static_cast<uint32_t>(width),
												   static_cast<uint32_t>(height));
}

HIDDEN
IOReturn CLASS::lock_memory(uintptr_t options, uint64_t* struct_out, size_t* struct_out_size)
{
	if (!struct_out || !struct_out_size || *struct_out_size < 2U * sizeof *struct_out)
		return kIOReturnBadArgument;
	if (!bTargetIsCGSSurface) {
		TDLog(2, "%s: called with non-surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_surface_client)
		return kIOReturnNotReady;
	if (m_surface_locked) return kIOReturnCannotLock;
	IOReturn result=m_surface_client->context_lock_memory(m_owning_task, &struct_out[0], &struct_out[1]);
	if (!result) m_surface_locked=true;
	return result;
}

HIDDEN
IOReturn CLASS::lock_surface_backing(void* output, size_t* output_size)
{
	if (!output || !output_size || *output_size != sizeof(struct VIMSurfaceMapping))
		return kIOReturnBadArgument;
	if (!bTargetIsCGSSurface || !m_surface_client)
		return kIOReturnNotReady;
	if (m_surface_locked) return kIOReturnCannotLock;
	IOReturn result=m_surface_client->context_lock_backing(m_owning_task,
		static_cast<struct VIMSurfaceMapping*>(output));
	if (!result) m_surface_locked=true;
	return result;
}
IOReturn CLASS::lock_gpu_surface(void* output,size_t* output_size)
{
    if(!output || !output_size || *output_size!=sizeof(VIMSurfaceMapping))return kIOReturnBadArgument;
    if(!bTargetIsCGSSurface || !m_surface_client)return kIOReturnNotReady;
    if(m_surface_locked)return kIOReturnCannotLock;
    IOReturn error=m_surface_client->context_lock_gpu_backing(static_cast<VIMSurfaceMapping*>(output));
    if(!error)m_surface_locked=true;
    return error;
}
IOReturn CLASS::lock_future_gpu_surface(void* output,size_t* output_size)
{
    if(!output || !output_size || *output_size!=sizeof(VIMSurfaceMapping) || !m_direct_lock)return kIOReturnBadArgument;
    VIM2DDirectGuard guard(m_direct_lock);
    if(!bTargetIsCGSSurface || !m_surface_client)return kIOReturnNotReady;
    if(m_surface_locked || m_backing_loan || m_writer_loan)return kIOReturnCannotLock;
    IOReturn error=m_surface_client->context_acquire_backing_future(static_cast<VIMSurfaceMapping*>(output),&m_surface_future_read);
    if(!error)m_surface_locked=m_surface_future=true;
    return error;
}
IOReturn CLASS::get_future_gpu_surface(void* output,size_t* output_size)
{
    if(!output || !output_size || *output_size!=sizeof(VIMSurfaceFutureRead) || !m_direct_lock)return kIOReturnBadArgument;
    VIM2DDirectGuard guard(m_direct_lock);
    if(!m_surface_locked || !m_surface_future)return kIOReturnNotOpen;
    memcpy(output,&m_surface_future_read,sizeof(m_surface_future_read));return kIOReturnSuccess;
}

/* The legacy try-lock retains CannotLock semantics. Only this appended
 * operation may sleep, with no driver/transport/2D-client lock held. Retain
 * the selected surface across that sleep and reject a concurrent retarget
 * or close rather than assigning ownership to a different surface. */
IOReturn CLASS::wait_surface_backing(void* output,size_t* output_size)
{
    if(!output || !output_size || *output_size!=sizeof(VIMSurfaceMapping))return kIOReturnBadArgument;
    if(!bTargetIsCGSSurface || !m_surface_client)return kIOReturnNotReady;
    if(m_surface_locked)return kIOReturnCannotLock;
    VMsvga2Surface* surface=m_surface_client;surface->retain();
    IOReturn error=surface->context_lock_backing(m_owning_task,static_cast<VIMSurfaceMapping*>(output),true);
    if(!error){
        if(m_surface_client!=surface || !m_provider || !bTargetIsCGSSurface){
            surface->context_unlock_memory(0,true);error=kIOReturnOffline;
        } else m_surface_locked=true;
    }
    surface->release();return error;
}

HIDDEN
IOReturn CLASS::get_surface_clip(void* output, size_t* output_size)
{
	if (!output || !output_size || *output_size != sizeof(struct VIMSurfaceClip))
		return kIOReturnBadArgument;
	if (!bTargetIsCGSSurface || !m_surface_client)
		return kIOReturnNotReady;
	return m_surface_client->context_get_clip(static_cast<struct VIMSurfaceClip*>(output));
}

HIDDEN
IOReturn CLASS::get_surface_geometry(void* output, size_t* output_size)
{
	if (!output || !output_size || *output_size != sizeof(struct VIMSurfaceGeometry))
		return kIOReturnBadArgument;
	if (!bTargetIsCGSSurface || !m_surface_client)
		return kIOReturnNotReady;
	return m_surface_client->context_get_geometry(static_cast<struct VIMSurfaceGeometry*>(output));
}

IOReturn CLASS::get_vram_surface(uintptr_t dirty,void* output,size_t* output_size)
{
    if(dirty>1 || !output || !output_size || *output_size!=sizeof(VIMSurfaceVRAM))return kIOReturnBadArgument;
    if(!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked || m_surface_future)return kIOReturnNotOpen;
    return m_surface_client->context_get_vram(static_cast<VIMSurfaceVRAM*>(output),dirty!=0);
}
IOReturn CLASS::get_ram_surface(uintptr_t dirty,uintptr_t first,void* output,size_t* output_size)
{
    if(dirty>1 || first>UINT32_MAX || !output || !output_size || *output_size!=sizeof(VIMSurfaceRAM))return kIOReturnBadArgument;
    if(!m_direct_lock)return kIOReturnNotReady;
    VIM2DDirectGuard guard(m_direct_lock);
    if(!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked || m_surface_future)return kIOReturnNotOpen;
    // Page-vector exports may feed native GPU snapshots without a scoped
    // render loan. They need the SAME admission lifetime until CPU unlock.
    IOReturn error=reserve_ram_access();if(error)return error;
    return m_surface_client->context_get_ram(static_cast<VIMSurfaceRAM*>(output),first,dirty!=0);
}

HIDDEN
IOReturn CLASS::unlock_memory(uintptr_t options, uint32_t* swapFlags)
{
	if(!m_direct_lock)return kIOReturnNotReady;
	VIM2DDirectGuard guard(m_direct_lock);
	if(m_backing_loan)return kIOReturnBusy;
	if (!bTargetIsCGSSurface) {
		TDLog(2, "%s: called with non-surface destination - unsupported\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!m_surface_client)
		return kIOReturnNotReady;
	if (!m_surface_locked) return kIOReturnNotOpen;
	if(m_surface_future)return kIOReturnNotPermitted;
	IOReturn result=m_surface_client->context_unlock_memory(swapFlags);
	if (!result) {m_surface_locked=false;release_ram_access();}
	return result;
}

HIDDEN
IOReturn CLASS::get_surface_state(void* output,size_t* output_size)
{
	if (!output || !output_size || *output_size!=sizeof(VIMSurfaceState)) return kIOReturnBadArgument;
	if(!m_direct_lock)return kIOReturnNotReady;
	VIM2DDirectGuard guard(m_direct_lock);
	if (!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked) return kIOReturnNotOpen;
	if(m_surface_future){memcpy(output,&m_surface_future_read.state,sizeof(VIMSurfaceState));return kIOReturnSuccess;}
	return m_surface_client->context_get_state(static_cast<VIMSurfaceState*>(output));
}

IOReturn CLASS::materialize_surface(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if(!m_direct_lock)return kIOReturnNotReady;
    VIM2DDirectGuard guard(m_direct_lock);
    if(m_backing_loan || m_surface_future)return kIOReturnBusy;
    if(!input || input_size!=sizeof(VIMSurfaceMaterialize) || !output || !output_size ||
       *output_size!=sizeof(VIMSurfaceState))return kIOReturnBadArgument;
    if(!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked)return kIOReturnNotOpen;
    return m_surface_client->context_materialize_gpu(static_cast<VIMSurfaceMaterialize const*>(input),
        static_cast<VIMSurfaceState*>(output));
}

IOReturn CLASS::defer_surface(void const* input,void* output,size_t input_size,size_t* output_size)
{
    if(!m_direct_lock)return kIOReturnNotReady;
    VIM2DDirectGuard guard(m_direct_lock);
    if(m_backing_loan || m_surface_future)return kIOReturnBusy;
    if(!input || input_size!=sizeof(VIMSurfaceMaterialize) || !output || !output_size ||
       *output_size!=sizeof(VIMSurfaceState))return kIOReturnBadArgument;
    if(!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked)return kIOReturnNotOpen;
    return m_surface_client->context_defer_gpu(static_cast<VIMSurfaceMaterialize const*>(input),
        static_cast<VIMSurfaceState*>(output));
}

HIDDEN
IOReturn CLASS::unlock_surface_backing(uintptr_t read_only,void* output,size_t* output_size)
{
	if(!m_direct_lock)return kIOReturnNotReady;
	VIM2DDirectGuard guard(m_direct_lock);
	if(m_backing_loan)return kIOReturnBusy;
	if (read_only>VIM_SURFACE_UNLOCK_READ_ONLY || !output || !output_size ||
		*output_size!=sizeof(VIMSurfaceState)) return kIOReturnBadArgument;
	if (!bTargetIsCGSSurface || !m_surface_client || !m_surface_locked) return kIOReturnNotOpen;
	if(m_surface_future){
		if(!read_only)return kIOReturnNotPermitted;
		memcpy(output,&m_surface_future_read.state,sizeof(VIMSurfaceState));
		static_cast<VIMSurfaceState*>(output)->flags&=~VIM_SURFACE_STATE_CONTEXT_LOCKED;
		m_surface_client->context_release_backing_future();m_surface_locked=m_surface_future=false;
		bzero(&m_surface_future_read,sizeof(m_surface_future_read));return kIOReturnSuccess;
	}
	IOReturn result=m_surface_client->context_unlock_memory(0,read_only!=0,static_cast<VIMSurfaceState*>(output));
	if (!result) {m_surface_locked=false;release_ram_access();}
	return result;
}

HIDDEN
IOReturn CLASS::finish(uintptr_t options)
{
#if 0
	if (!(options & (kIOBlitWaitAll2D | kIOBlitWaitAll)))
		return kIOReturnUnsupported;
#endif
	if (m_provider)
		return m_provider->SyncFIFO();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::declare_image(UInt64 const* struct_in,
							  UInt64* struct_out,
							  size_t struct_in_size,
							  size_t* struct_out_size)
{
	TDLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::create_image(uintptr_t c1, uintptr_t c2, UInt64* struct_out, size_t* struct_out_size)
{
	TDLog(2, "%s(%lu, %lu, struct_out, %lu)\n", __FUNCTION__, c1, c2, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::create_transfer(uintptr_t c1, uintptr_t c2, UInt64* struct_out, size_t* struct_out_size)
{
	TDLog(2, "%s(%lu, %lu, struct_out, %lu)\n", __FUNCTION__, c1, c2, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::delete_image(uintptr_t image_id)
{
	TDLog(2, "%s(%lu)\n", __FUNCTION__, image_id);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::wait_image(uintptr_t image_id)
{
	TDLog(2, "%s(%lu)\n", __FUNCTION__, image_id);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::set_surface_paging_options(IOSurfacePagingControlInfoStruct const* struct_in,
										   IOSurfacePagingControlInfoStruct* struct_out,
										   size_t struct_in_size,
										   size_t* struct_out_size)
{
	TDLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::set_surface_vsync_options(IOSurfaceVsyncControlInfoStruct const* struct_in,
										  IOSurfaceVsyncControlInfoStruct* struct_out,
										  size_t struct_in_size,
										  size_t* struct_out_size)
{
	TDLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::set_macrovision(uintptr_t new_state)
{
	TDLog(3, "%s(%lu)\n", __FUNCTION__, new_state);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark NV2DContext Methods
#pragma mark -

HIDDEN
IOReturn CLASS::read_configs(uint32_t const* struct_in,
							 uint32_t* struct_out,
							 size_t struct_in_size,
							 size_t* struct_out_size)
{
	TDLog(3, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (!struct_in || !struct_out || !struct_out_size)
		return kIOReturnBadArgument;
	if (struct_in_size < sizeof(uint32_t) || *struct_out_size < sizeof(uint32_t))
		return kIOReturnBadArgument;
	if (*struct_in == 2U)
		*struct_out = 2U;
	else
		*struct_out = 0U;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::read_config_Ex(uint32_t const* struct_in,
							   uint32_t* struct_out,
							   size_t struct_in_size,
							   size_t* struct_out_size)
{
	TDLog(3, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	if (!struct_in || !struct_out || !struct_out_size)
		return kIOReturnBadArgument;
	if (struct_in_size < 2U * sizeof(uint32_t))
		return kIOReturnBadArgument;
	bzero(struct_out, *struct_out_size);
	switch (struct_in[0]) {
		case 144:
			/*
			 * GetBeamPosition
			 */
			if (*struct_out_size < 2U * sizeof(uint32_t))
				return kIOReturnBadArgument;
			struct_out[1] = 0;
			break;
		case 288:
			/*
			 * SetSurface
			 */
			if (*struct_out_size < 3U * sizeof(uint32_t))
				return kIOReturnBadArgument;
			struct_out[0] = 64;
			struct_out[2] = 16;
			break;
		default:
			return kIOReturnUnsupported;
	}
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_surface_info2(uint32_t const* struct_in,
								  uint32_t* struct_out,
								  size_t struct_in_size,
								  size_t* struct_out_size)
{
	TDLog(2, "%s(struct_in, struct_out, %lu, %lu)\n", __FUNCTION__, struct_in_size, *struct_out_size);
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::kernel_printf(char const* str, size_t str_size)
{
	TDLog(2, "%s: %.80s\n", __FUNCTION__, str);
	return kIOReturnSuccess;
}
