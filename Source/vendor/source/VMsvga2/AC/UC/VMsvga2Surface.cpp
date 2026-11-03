/*
 *  VMsvga2Surface.cpp
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

#include <IOKit/IOLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#include <libkern/OSAtomic.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
static volatile UInt32 VIMSurfaceObjectSequence;
static uint64_t VIMNextSurfaceObject()
{
    // Darwin8 exports32-bit CAS, not OSIncrementAtomic64. Surface creation
    // is cold; reserve a boot-scoped opaque key and refuse exhaustion rather
    // than wrap/reuse a key or expose a kernel address to the host.
    for(;;){
        UInt32 old=VIMSurfaceObjectSequence;
        if(old==UINT32_MAX)return 0;
        if(OSCompareAndSwap(old,old+1,const_cast<UInt32*>(&VIMSurfaceObjectSequence)))return uint64_t(old)+1;
    }
}
#else
static volatile SInt64 VIMSurfaceObjectSequence;
#endif
#endif

#include "vmw_options_ac.h"
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "PhysicalSegment.h"
#include "VMsvga2Surface.h"
#include "VMsvga22DContext.h"
#include "../../vminclude/vim_surface_clip.h"
#include "../../vminclude/vim_gl_surface_service.h"
#include "../../vminclude/vim_passthrough_opengl.h"
#include "../../../../../probes/opengl-map-object-profile.h"

#include "svga_apple_header.h"
#include "svga_overlay.h"
#include "svga_apple_footer.h"

#define CLASS VMsvga2Surface
#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2Surface, IOUserClient);

OSDefineMetaClassAndStructors(VMsvga2PreparedBacking, OSObject);
VMsvga2PreparedBacking* VMsvga2PreparedBacking::withMemory(IOMemoryDescriptor* memory,IOReturn* result)
{
    if(!result)return 0;
    *result=kIOReturnBadArgument;if(!memory)return 0;
    VMsvga2PreparedBacking* owner=new VMsvga2PreparedBacking;
    if(!owner){*result=kIOReturnNoMemory;return 0;}
    if(!owner->init()){owner->release();*result=kIOReturnNoMemory;return 0;}
    owner->m_access_map=0;owner->m_access_object=0;owner->m_access_checked=false;
    owner->m_pages_lock=IOLockAlloc();
    if(!owner->m_pages_lock){owner->release();*result=kIOReturnNoMemory;return 0;}
    // Exactly the preparation formerly owned by the surface, not another
    // per-borrow prepare. OSObject reference counting now owns completion.
    *result=memory->prepare();
    if(*result){owner->release();return 0;}
    memory->retain();owner->m_memory=memory;return owner;
}
void VMsvga2PreparedBacking::free()
{
    if(m_access_map){m_access_map->release();m_access_map=0;}
    if(m_pages){m_pages->release();m_pages=0;}
    if(m_pages_lock){IOLockFree(m_pages_lock);m_pages_lock=0;}
    if(m_memory){m_memory->complete();m_memory->release();m_memory=0;}
    OSObject::free();
}
IOMemoryMap* VMsvga2PreparedBacking::copyAccessMap(void** object,IOReturn* error)
{
    if(!object || !error)return 0;
    *object=0;*error=kIOReturnUnsupported;
    IOLockLock(m_pages_lock);
    if(!m_access_checked){
        m_access_checked=true;
        IOMemoryMap* map=m_memory->createMappingInTask(kernel_task,0,kIOMapAnywhere|kIOMapUnique);
        void* key=map?VIMLegacyWholeObject(map,m_memory):0;
        if(key){m_access_map=map;m_access_object=key;}else if(map)map->release();
    }
    IOMemoryMap* map=m_access_map;
    if(map){map->retain();*object=m_access_object;*error=kIOReturnSuccess;}
    IOLockUnlock(m_pages_lock);return map;
}
OSData* VMsvga2PreparedBacking::copyPages(uint32_t count,IOReturn* result)
{
    if(!result)return 0;
    *result=kIOReturnBadArgument;
    if(!count || count>16384 || !m_memory || !m_pages_lock)return 0;
    IOLockLock(m_pages_lock);OSData* pages=m_pages;
    uint32_t prior=pages?pages->getLength()/sizeof(uint64_t):0;
    if(prior<count){
        pages=OSData::withCapacity(count*sizeof(uint64_t));
        *result=kIOReturnNoMemory;
        if(pages && prior && !pages->appendBytes(m_pages->getBytesNoCopy(),prior*sizeof(uint64_t))){pages->release();pages=0;}
        for(uint32_t i=prior;pages && i<count;++i){
            IOByteCount span=0;addr64_t page=VIMPhysicalSegment(m_memory,uint64_t(i)*4096,&span);
            if(!page || (page&4095) || !span){*result=kIOReturnUnsupported;pages->release();pages=0;break;}
            if(!pages->appendBytes(&page,sizeof(page))){pages->release();pages=0;break;}
        }
        if(pages){if(m_pages)m_pages->release();m_pages=pages;}
    }
    // Published OSData is never modified. A larger prefix replaces the cache;
    // an existing endpoint's retained snapshot remains valid through teardown.
    if(pages){pages->retain();*result=kIOReturnSuccess;}
    IOLockUnlock(m_pages_lock);return pages;
}

#if LOGGING_LEVEL >= 1
#define SFLog(log_level, ...) do { if (log_level <= m_log_level) VLog("IOSF: ", ##__VA_ARGS__); } while (false)
#else
#define SFLog(log_level, ...)
#endif

#define FMT_D(x) static_cast<int>(x)
#define FMT_U(x) static_cast<unsigned>(x)
#define FMT_LU(x) static_cast<unsigned long>(x)

#define HIDDEN __attribute__((visibility("hidden")))

#if IOACCELTYPES_10_5 || (IOACCEL_TYPES_REV < 12 && !defined(kIODescriptionKey))
#define ACCEL_TYPES_10_5
#endif

#ifdef ACCEL_TYPES_10_5
#define CLIENT_ADDR_TO_UINTPTR_T(x) reinterpret_cast<uintptr_t>(x)
#define CLIENT_ADDR_TO_PVOID(x) static_cast<void*>(x)
#else
#define CLIENT_ADDR_TO_UINTPTR_T(x) static_cast<uintptr_t>(x)
#define CLIENT_ADDR_TO_PVOID(x) reinterpret_cast<void*>(x)
#endif

typedef unsigned __v4si __attribute__((vector_size(16), may_alias));
typedef long long __v2di __attribute__((vector_size(16), may_alias));

static
IOExternalMethod const iofbFuncsCache[kIOAccelNumSurfaceMethods] =
{
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_lock_options), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_unlock_options), kIOUCScalarIScalarO, 1, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::get_state), kIOUCScalarIScalarO, 0, 1},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_lock_options), kIOUCScalarIStructO, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_unlock_options), kIOUCScalarIScalarO, 1, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read), kIOUCScalarIStructI, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape_backing), kIOUCScalarIStructI, 4, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_id_mode), kIOUCScalarIScalarO, 2, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_scale), kIOUCScalarIStructI, 1, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape), kIOUCScalarIStructI, 2, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_flush), kIOUCScalarIScalarO, 2, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_query_lock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_lock), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_read_unlock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_lock), kIOUCScalarIStructO, 0, kIOUCVariableStructureSize},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_write_unlock), kIOUCScalarIScalarO, 0, 0},
	{0, reinterpret_cast<IOMethod>(&CLASS::surface_control), kIOUCScalarIScalarO, 2, 1},
	{0, reinterpret_cast<IOMethod>(&CLASS::set_shape_backing_length), kIOUCScalarIStructI, 5, kIOUCVariableStructureSize}
};

#pragma mark -
#pragma mark Static Functions
#pragma mark -

template<unsigned N>
union DefineRegion
{
	uint8_t b[sizeof(IOAccelDeviceRegion) + N * sizeof(IOAccelBounds)];
	IOAccelDeviceRegion r;
};

static inline
uint32_t round_up_to_power2(uint32_t s, uint32_t power2)
{
	return (s + power2 - 1U) & -power2;
}

static inline
size_t page_residue(uint64_t addr)
{
	return static_cast<size_t>(addr & PAGE_MASK);
}

static inline
void memset32(void* dest, uint32_t value, size_t size)
{
	__asm__ volatile ("cld; rep stosl" : "+c" (size), "+D" (dest) : "a" (value) : "memory");
}

#ifdef VECTORIZE
static inline
void memset128(void* dest, __v2di value, size_t size)
{
	__v2di* d = static_cast<__v2di*>(dest);
	for (; size; --size, ++d)
		// Same SSE2 streaming store, without the removed compiler builtin.
		__asm__ volatile ("movntdq %1, %0" : "=m"(*d) : "x"(value));
}
#endif /* VECTORIZE */

static
bool isRegionEmpty(IOAccelDeviceRegion const* rgn)
{
	if (!rgn)
		return false;
	if (rgn->num_rects &&
		(rgn->rect[0].w <= 0 || rgn->rect[0].h <= 0))
		return true;
	return false;
}

void set_region(IOAccelDeviceRegion* rgn,
				uint32_t x,
				uint32_t y,
				uint32_t w,
				uint32_t h);

static inline
uint32_t GMR_VRAM(void)
{
	return static_cast<uint32_t>(-2) /* SVGA_GMR_FRAMEBUFFER */;
}

static inline
bool isIdValid(uint32_t id)
{
	return static_cast<int>(id) >= 0;
}

#pragma mark -
#pragma mark IOUserClient Methods
#pragma mark -

IOExternalMethod* CLASS::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	if (!targetP || index >= kIOAccelNumSurfaceMethods)
		return 0;
	*targetP = this;
	return const_cast<IOExternalMethod*>(&iofbFuncsCache[index]);
}

IOReturn CLASS::clientClose()
{
	SFLog(2, "%s[%#x]\n", __FUNCTION__, m_wID);
	VMsvga22DContext* endpoint=0;
	if(m_content_lock){
		IOLockLock(m_content_lock);m_content_closing=true;
		endpoint=m_backing_loan_endpoint;if(endpoint)endpoint->retain();
		if(m_context_waiters)IOLockWakeup(m_content_lock,const_cast<uint8_t*>(&bIsLocked),false);
		IOLockUnlock(m_content_lock);
	}
	if(endpoint){
		// A pin alone cannot stop the source's malloc buffer being reused.
		// Retire the endpoint BEFORE close returns to that CPU allocator.
		// Never hold m_content_lock while entering a transport/GL wait.
		endpoint->cancel_surface_backing_loan(this);endpoint->release();
		IOLockLock(m_content_lock);
		while(m_active_backing_loan)IOLockSleep(m_content_lock,&m_active_backing_loan,THREAD_UNINT);
		IOLockUnlock(m_content_lock);
	}
	Cleanup();m_provider=0;
	if (!terminate(0))
		IOLog("%s: terminate failed\n", __FUNCTION__);
	m_owning_task = 0;
	return kIOReturnSuccess;
}
void CLASS::free()
{
	if (m_content_lock) { IOLockFree(m_content_lock);m_content_lock=0; }
	super::free();
}

#if 0
/*
 * Note: for NVSurface
 *   In OS 10.5 this method actually does something
 *   In OS 10.6 it returns an error kIOReturnError
 */
IOReturn CLASS::clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory)
{
	SFLog(3, "%s(%u, %p, %p)\n", __FUNCTION__, type, options, memory);
	return kIOReturnError;
}
#endif

/*
 * Note:
 *   IONVSurface in OS 10.6 has an override on this method, to
 *   redirect external methods as follows
 *     set_shape_backing        --> set_shape_backing_length_ext
 *     set_shape_backing_length --> set_shape_backing_length_ext
 */
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
IOReturn CLASS::externalMethod(uint32_t selector,
							   IOExternalMethodArguments* arguments,
							   IOExternalMethodDispatch* dispatch,
							   OSObject* target,
							   void* reference)
{
	switch (selector) {
		case kIOAccelSurfaceSetShapeBackingAndLength:
			return set_shape_backing_length_ext(static_cast<eIOAccelSurfaceShapeBits>(arguments->scalarInput[0]),
												static_cast<uintptr_t>(arguments->scalarInput[1]),
												arguments->scalarInput[2],
												static_cast<size_t>(arguments->scalarInput[3]),
												static_cast<size_t>(arguments->scalarInput[4]),
												static_cast<IOAccelDeviceRegion const*>(arguments->structureInput),
												arguments->structureInputSize);
		case kIOAccelSurfaceSetShapeBacking:
			return set_shape_backing_length_ext(static_cast<eIOAccelSurfaceShapeBits>(arguments->scalarInput[0]),
												static_cast<uintptr_t>(arguments->scalarInput[1]),
												arguments->scalarInput[2],
												static_cast<size_t>(arguments->scalarInput[3]),
												0U,
												static_cast<IOAccelDeviceRegion const*>(arguments->structureInput),
												arguments->structureInputSize);
	}
	return super::externalMethod(selector, arguments, dispatch, target, reference);
}
#endif

IOReturn CLASS::message(UInt32 type, IOService* provider, void* argument)
{
	VMsvga2Accel::FindSurface* fs;

	if (provider != m_provider || type != kIOMessageFindSurface)
		return super::message(type, provider, argument);
	if (!argument)
		return kIOReturnBadArgument;
	if (!bHaveID)
		return kIOReturnNotReady;
	fs = static_cast<VMsvga2Accel::FindSurface*>(argument);
	if (fs->cgsSurfaceID == m_wID) {
		fs->client = this;
		fs->privatePBuffer |= m_pbuffer_only;
	}
	return kIOReturnSuccess;
}

bool CLASS::start(IOService* provider)
{
	m_provider = OSDynamicCast(VMsvga2Accel, provider);
	if (!m_provider)
		return false;
	if (!super::start(provider))
		return false;
	m_log_level = m_provider->getLogLevelAC();
	if (m_provider->getScreenInfo(&m_screenInfo) != kIOReturnSuccess) {
		super::stop(provider);
		return false;
	}
	Start3D();
	return true;
}

bool CLASS::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	if (!super::initWithTask(owningTask, securityToken, type))
		return false;
	m_owning_task = owningTask;
	Init();
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
	return m_content_lock!=0 && m_content_object!=0;
#else
	return m_content_lock!=0;
#endif
}

CLASS* CLASS::withTask(task_t owningTask, void* securityToken, uint32_t type,bool pbufferOnly)
{
	CLASS* inst = new CLASS;

	if (inst && !inst->initWithTask(owningTask, securityToken, type)) {
		inst->release();
		inst = 0;
	}

	if(inst)inst->m_pbuffer_only=pbufferOnly;
	return inst;
}

#pragma mark -
#pragma mark Private Support Methods
#pragma mark -

HIDDEN
bool CLASS::haveFrontBuffer() const
{
	return bHaveScreenObject != 0U || bHaveMasterSurface != 0U;
}

HIDDEN
bool CLASS::isBackingValid() const
{
	if(m_gpu_primary)return m_gpu_primary->m_gpu_presentation && m_gpu_primary->isBackingValid();
	return m_backing.self != 0 || m_backing.vtb.md != 0;
}

/*
 * 0 - no backing
 * 1 - backing in kernel
 * 2 - backing in client
 */
HIDDEN
int CLASS::classifyBacking() const
{
	if (m_backing.self)
		return 1;
	if (m_backing.vtb.md)
		return m_backing.vtb.gart_ptr ? 2 : 1;
	return 0;
}

HIDDEN
bool CLASS::hasSourceGrown() const
{
	return static_cast<vm_size_t>(m_scale.reserved[0]) > m_backing.size;
}

HIDDEN
bool CLASS::isSourceValid() const
{
	return (m_scale.source.w > 0) && (m_scale.source.h > 0);
}

HIDDEN
bool CLASS::isClientBackingValid() const
{
	return m_client_backing.addr != 0U;
}

HIDDEN
bool CLASS::isIdentityScale() const
{
	return
		m_scale.buffer.w == m_last_region->bounds.w &&
		m_scale.buffer.h == m_last_region->bounds.h;
}

HIDDEN
void CLASS::Init()
{
	m_log_level = LOGGING_LEVEL;
	m_content_lock=IOLockAlloc();m_backing_incarnation=1;m_content_epoch=1;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
	m_content_object=VIMNextSurfaceObject();
#elif __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	// Only an opaque resource key is sent to the host, never a kernel pointer.
	// OSIncrementAtomic64 returns the previous value. The existing transport
	// namespace scopes this monotonically allocated identity to this boot.
	m_content_object=static_cast<uint64_t>(OSIncrementAtomic64(&VIMSurfaceObjectSequence))+1;
#endif
	m_context_owner=0;m_context_waiters=0;m_content_closing=false;
	m_shared_ram_pages=0;m_shared_ram_page_count=0;
	m_prepared_backing=0;
	m_active_backing_loan=0;m_backing_loan_endpoint=0;
	bzero(&m_future_backing,sizeof(m_future_backing));m_future_readers=0;m_future_retiring=false;m_writer_backing=0;
	m_gpu_presentation=false;m_gpu_invalidated=true;m_gpu_service_ready=false;m_gpu_companion_id=0;m_gpu_primary=0;
	m_backing.vtb.init();
	m_video.stream_id = SVGA_ID_INVALID;
}

HIDDEN
void CLASS::noteBackingChange()
{
	if (!m_content_lock) return;
	IOLockLock(m_content_lock);++m_backing_incarnation;++m_content_epoch;
	if(m_shared_ram_pages)IOFree(m_shared_ram_pages,m_shared_ram_page_count*sizeof(uint64_t));
	m_shared_ram_pages=0;m_shared_ram_page_count=0;IOLockUnlock(m_content_lock);
}
HIDDEN
IOReturn CLASS::beginContentWrite()
{
	if(m_gpu_primary)return m_gpu_primary->beginContentWrite();
	IOLockLock(m_content_lock);
	IOReturn error=m_content_closing?kIOReturnOffline:m_active_backing_loan?kIOReturnCannotLock:materializeGPU();
	if(!error) { ++m_content_writers;++m_content_epoch; }
	IOLockUnlock(m_content_lock);return error;
}
HIDDEN
IOReturn CLASS::beginContentRead()
{
	if(m_gpu_primary)return m_gpu_primary->beginContentRead();
	IOLockLock(m_content_lock);IOReturn error=m_content_closing?kIOReturnOffline:m_active_backing_loan?kIOReturnCannotLock:materializeGPU();
	if(!error)++m_content_readers;
	IOLockUnlock(m_content_lock);return error;
}
HIDDEN
void CLASS::endContentRead()
{
	if(m_gpu_primary){m_gpu_primary->endContentRead();return;}
	IOLockLock(m_content_lock);if(m_content_readers)--m_content_readers;IOLockUnlock(m_content_lock);
}
HIDDEN
void CLASS::discardGPU()
{
	if(m_gpu_hold && m_provider)m_provider->releaseGPUSurface(m_gpu_hold);
	m_gpu_hold=0;m_gpu_epoch=0;
}
HIDDEN
IOReturn CLASS::materializeGPU()
{
	if(!m_gpu_hold)return kIOReturnSuccess;
	if(!m_provider)return kIOReturnOffline;
	IOMemoryDescriptor* memory=m_backing.vtb.md;
	bool temporary=false,prepared=false;IOReturn error=kIOReturnSuccess;
	if(!memory && m_backing.self) {
		memory=IOMemoryDescriptor::withAddress(m_backing.self,m_backing.size,kIODirectionInOut);
		temporary=true;
		if(memory) { error=memory->prepare();prepared=!error; }
	}
	if(!memory)error=kIOReturnNoMemory;
	if(!error)error=m_provider->readGPUSurface(m_gpu_hold,m_gpu_epoch,0,0,
		m_identity_width,m_identity_height,memory,m_backing.vtb.md?m_backing.offset:0,m_identity_pitch);
	if(temporary && memory) { if(prepared)memory->complete();memory->release(); }
	// A failed read may have written a prefix. Keep the GPU copy authoritative
	// and refuse CPU access until a complete read succeeds. Reading does not
	// advance the logical content version.
	if(!error)discardGPU();
	return error;
}
HIDDEN
void CLASS::endContentWrite()
{
	if(m_gpu_primary){m_gpu_primary->endContentWrite();return;}
	IOLockLock(m_content_lock);if (m_content_writers) --m_content_writers;
	++m_content_epoch;IOLockUnlock(m_content_lock);
}
HIDDEN
void CLASS::nativeContentWrite(bool active)
{
	IOLockLock(m_content_lock);m_native_write_active=active;++m_content_epoch;IOLockUnlock(m_content_lock);
}
HIDDEN
void CLASS::copyContentState(VIMSurfaceState* output)
{
	// View changes must not turn the full primary allocation into a different
	// resource. Source layout changes do require a new backing incarnation.
	uint32_t width=m_scale.source.w,height=m_scale.source.h,pitch=m_scale.reserved[1];
	if (m_identity_width!=width || m_identity_height!=height || m_identity_pitch!=pitch ||
		m_identity_bpp!=m_bytes_per_pixel || m_identity_format!=static_cast<uint32_t>(m_surfaceFormat)) {
		m_identity_width=width;m_identity_height=height;m_identity_pitch=pitch;m_identity_bpp=m_bytes_per_pixel;
		m_identity_format=static_cast<uint32_t>(m_surfaceFormat);
		++m_backing_incarnation;++m_content_epoch;
	}
	bzero(output,sizeof(*output));output->version=VIM_SURFACE_STATE_VERSION;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	output->object=m_content_object;
#else
	output->object=getRegistryEntryID();
#endif
	output->incarnation=m_backing_incarnation;output->content_epoch=m_content_epoch;
	// OSTestAndSet indexes from the high end of this byte, as also used in
	// the provider's pre-existing surface_query_lock implementation.
	if (m_native_write_active || m_content_writers || (bIsLocked & (128U>>vmSurfaceLockWrite)))
		output->flags|=VIM_SURFACE_STATE_CPU_WRITER;
	if (bIsLocked & (128U>>vmSurfaceLockContext)) output->flags|=VIM_SURFACE_STATE_CONTEXT_LOCKED;
	if (m_gpu_hold) output->flags|=VIM_SURFACE_STATE_GPU_NEWER;
}

HIDDEN
void CLASS::Cleanup()
{
	m_gpu_presentation=false;
	if(m_gpu_primary){
		IOLockLock(m_gpu_primary->m_content_lock);
		if(m_gpu_primary->m_gpu_companion_id==m_wID)m_gpu_primary->m_gpu_companion_id=0;
		IOLockUnlock(m_gpu_primary->m_content_lock);
		m_gpu_primary->release();m_gpu_primary=0;
	}
	if(m_content_lock) { IOLockLock(m_content_lock);discardGPU();IOLockUnlock(m_content_lock); }
	if (m_provider) {
		Cleanup3D();
		surface_video_off();
	}
	releaseBacking();
	clearLastRegion();
	if (m_last_shape) {
		m_last_shape->release();
		m_last_shape = 0;
	}
}

HIDDEN
void CLASS::clearLastRegion()
{
	if (m_last_region) {
		m_last_region = 0;
		m_framebufferIndex = 0U;
	}
}

HIDDEN
void CLASS::calculateSurfaceInformation(IOAccelSurfaceInformation* info)
{
#ifdef ACCEL_TYPES_10_5
	if (isClientBackingValid())
		info->address[0] = static_cast<vm_address_t>(m_client_backing.addr);
	else
		info->address[0] = m_backing.map[0]->getVirtualAddress();
#else
	if (isClientBackingValid())
		info->address[0] = m_client_backing.addr;
	else
		info->address[0] = m_backing.map[0]->getAddress();
#endif
	info->address[0] += m_scale.reserved[2];
	info->width = static_cast<uint32_t>(m_scale.buffer.w);
	info->height = static_cast<uint32_t>(m_scale.buffer.h);
	info->rowBytes = m_scale.reserved[1];
	info->pixelFormat = m_pixel_format;
	info->colorTemperature[0] = 0x1CCCCU;	// from GeForce.kext
}

/*
 * m_scale.reserved[0] - Byte size of source rounded up nearest PAGE_SIZE
 * m_scale.reserved[1] - Source pitch
 * m_scale.reserved[2] - buffer byte offset within source
 */
HIDDEN
void CLASS::calculateScaleParameters(bool bFromGFB)
{
	if (bFromGFB) {
		m_scale.reserved[0] = static_cast<uint32_t>(m_screenInfo.y);
		m_scale.reserved[1] = m_screenInfo.client_row_bytes;
		// The paired compositor needs a display-sized SHADOW, not a second
		// copy of the entire PCI aperture. Snow's primary surface requests
		// 128 MB here even at 1280x800; VRAM cannot hold that extra aperture,
		// and its native fallback allocation fails before the display binds.
		// Preserve the native pitch and complete source extent, including all
		// drawing-view offsets. Hardware SVGA3D/screen-object paths are unchanged.
		const uint64_t active=static_cast<uint64_t>(m_scale.source.h)*m_scale.reserved[1];
		if (!m_provider->Have3D() && !m_provider->HaveScreen() &&
			m_provider->getProperty("VIMDirectGLCompositor") &&
			m_scale.source.w>0 && m_scale.source.h>0 &&
			m_scale.reserved[1]>=static_cast<uint32_t>(m_scale.source.w)*m_bytes_per_pixel &&
			active && active<=m_scale.reserved[0])
			m_scale.reserved[0]=static_cast<uint32_t>(active);
	} else if (isClientBackingValid()) {
		m_scale.reserved[0] = static_cast<uint32_t>(m_client_backing.size + page_residue(m_client_backing.addr));
		m_scale.reserved[1] = static_cast<uint32_t>(m_client_backing.rowbytes);
	} else {
		m_scale.reserved[1] = static_cast<uint32_t>(m_scale.source.w) * m_bytes_per_pixel;
		m_scale.reserved[0] = static_cast<uint32_t>(m_scale.source.h) * m_scale.reserved[1];
	}
	m_scale.reserved[0] = round_up_to_power2(m_scale.reserved[0], PAGE_SIZE);
	m_scale.reserved[2] = m_scale.buffer.y * m_scale.reserved[1] + m_scale.buffer.x * m_bytes_per_pixel;
}

HIDDEN
void CLASS::clipRegionToBuffer(IOAccelDeviceRegion* region,
							   int deltaX,
							   int deltaY)
{
	uint32_t i;

	if (!region || !region->num_rects)
		return;
	for (i = 0U; i != region->num_rects; ++i) {
		IOAccelBounds* rect = &region->rect[i];
		int x = rect->x + deltaX;
		int y = rect->y + deltaY;
		if (x < 0) {
			rect->x = -deltaX;
			rect->w += x;
			x = 0;
		}
		if (y < 0) {
			rect->y = -deltaY;
			rect->h += y;
			y = 0;
		}
		if (x + rect->w > m_scale.buffer.w)
			rect->w = m_scale.buffer.w - x;
		if (y + rect->h > m_scale.buffer.h)
			rect->h = m_scale.buffer.h - y;
		if (rect->w < 0)
			rect->w = 0;
		if (rect->h < 0)
			rect->h = 0;
	}
}

#pragma mark -
#pragma mark Private Support Methods - backing
#pragma mark -

HIDDEN
bool CLASS::wrapClientBacking()
{
	IOReturn rc;
	IOMemoryDescriptor* md;

	switch (classifyBacking()) {
		case 1:
			/*
			 * Previous backing in kernel, purge it
			 */
			releaseBacking();
			break;
		case 2:
			/*
			 * Reuse existing descriptor if possible
			 */
			if (m_backing.vtb.gart_ptr == 1U)	// client backing hasn't reconfigured
				return true;
			releaseBacking();
			break;
	}
	m_backing.offset = page_residue(m_client_backing.addr);
	noteBackingChange();
	m_backing.size = m_scale.reserved[0];
	md = IOMemoryDescriptor::withAddressRange(m_client_backing.addr & -PAGE_SIZE,
											  m_backing.size,
											  kIODirectionInOut,	// TBD: if memory is read-only in client, use Out
											  m_owning_task);
	if (!md) {
		SFLog(1, "%s[%#x]: Failed to create IOMemoryDescriptor\n", __FUNCTION__, m_wID);
		return false;
	}
	m_backing.vtb.md = md;
	if (!m_provider->Have3D() && !m_provider->HaveScreen()) {
		m_prepared_backing=VMsvga2PreparedBacking::withMemory(md,&rc);
	} else
		rc = m_backing.vtb.prepare(m_provider);
	if (rc != kIOReturnSuccess) {
		m_backing.vtb.md = 0;
		md->release();
		SFLog(1, "%s[%#x]: VendorTransferBuffer::prepare failed with status code %#x\n", __FUNCTION__, m_wID, rc);
		return false;
	}
	m_backing.vtb.gart_ptr = 1U;	// mark as client descriptor
	m_backing.vtb.fence = 0U;
	return true;
}

HIDDEN
bool CLASS::allocGMRBacking()
{
	noteBackingChange();
	IOReturn rc;
	IOBufferMemoryDescriptor* bmd =
	IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task /* 0 */,
													 kIODirectionInOut /* | kIOMemoryPageable */,
													 m_backing.size,
													 0xFFFFFFFF000ULL);	// ensures 32-bit PPNs
	if (!bmd) {
		SFLog(1, "%s[%#x]: Failed to allocate IOBufferMemoryDescriptor\n", __FUNCTION__, m_wID);
		return false;
	}
	m_backing.offset = 0U;
	m_backing.vtb.md = bmd;
	if (!m_provider->Have3D() && !m_provider->HaveScreen()) {
		m_prepared_backing=VMsvga2PreparedBacking::withMemory(bmd,&rc);
	} else
		rc = m_backing.vtb.prepare(m_provider);
	if (rc != kIOReturnSuccess) {
		m_backing.vtb.md = 0;
		bmd->release();
		SFLog(1, "%s[%#x]: VendorTransferBuffer::prepare failed with status code %#x\n", __FUNCTION__, m_wID, rc);
		return false;
	}
	m_backing.vtb.gart_ptr = 0U;	// mark as kernel descriptor
	m_backing.vtb.fence = 0U;
	return true;
}

HIDDEN
bool CLASS::allocBacking()
{
	if(m_gpu_primary)return m_gpu_primary->m_gpu_presentation && m_gpu_primary->allocBacking();
	if (m_client_backing.addr)
		return wrapClientBacking();
	switch (classifyBacking()) {
		case 1:	// previous in kernel
			/*
			 * We only resize the backing if it needs to grow.  The reasons for this:
			 *   1) The Allocator hasn't been stress tested.
			 *   2) Every reallocation needs to create/modify the memory maps in
			 *      the owning task[s].
			 */
			if (!hasSourceGrown())
				return true;
			for (uint32_t i = 0U; i != 2U; ++i)
				releaseBackingMap(i);
			if(m_prepared_backing){m_prepared_backing->release();m_prepared_backing=0;}
			m_backing.vtb.complete(m_provider);
			m_backing.vtb.discard();
			break;
		case 2:
			/*
			 * Previous backing was client, purge it
			 */
			releaseBacking();
			break;
	}
	m_backing.size = m_scale.reserved[0];
	noteBackingChange();
	m_backing.self = static_cast<uint8_t*>(m_provider->VRAMRealloc(m_backing.self, m_backing.size));
	if (!m_backing.self)
		return allocGMRBacking();
	m_backing.offset = reinterpret_cast<vm_offset_t>(m_backing.self) - CLIENT_ADDR_TO_UINTPTR_T(m_screenInfo.client_addr);
	SFLog(2, "%s[%#x]: m_backing.offset is %#lx\n", __FUNCTION__, m_wID, FMT_LU(m_backing.offset));
	m_backing.vtb.gmr_id = GMR_VRAM();
	m_backing.vtb.fence = 0U;
	return true;
}

HIDDEN
bool CLASS::mapBacking(task_t for_task, uint32_t index)
{
	IOMemoryMap** m;

	if (index >= 2U)
		return false;
	m = &m_backing.map[index];
	if (*m)
		return true;
	if (m_backing.self)
		*m = m_provider->mapVRAMRangeForTask(for_task, m_backing.offset, m_backing.size);
	else if (m_backing.vtb.md)
		*m = m_backing.vtb.md->createMappingInTask(for_task, 0U, kIOMapAnywhere);
#if LOGGING_LEVEL >= 1
	if (!(*m))
		SFLog(1, "%s[%#x]: Failed to map backing in client for index %u\n", __FUNCTION__, m_wID, index);
#endif
	return (*m) != 0;
}

HIDDEN
void CLASS::releaseBacking()
{
	noteBackingChange();
	for (uint32_t i = 0U; i != 2U; ++i)
		if (m_backing.map[i])
			m_backing.map[i]->release();
	if(m_prepared_backing){m_prepared_backing->release();m_prepared_backing=0;}
	m_backing.vtb.complete(m_provider);
	m_backing.vtb.discard();
	if (m_provider != 0 && m_backing.self != 0)
		m_provider->VRAMFree(m_backing.self);
	bzero(&m_backing, sizeof m_backing);
	m_backing.vtb.init();
}

HIDDEN
void CLASS::releaseBackingMap(uint32_t index)
{
	if (index >= 2U)
		return;
	if (m_backing.map[index]) {
		m_backing.map[index]->release();
		m_backing.map[index] = 0;
	}
}

HIDDEN
IOReturn CLASS::obtainKernelPtrs(IOVirtualAddress* base, vm_size_t* limit_from_base, IOMemoryMap** holder)
{
	VMsvga2Surface* owner=m_gpu_primary?m_gpu_primary:this;
	vm_size_t t;
	if (owner->m_backing.vtb.md) {
		IOMemoryMap* m = owner->m_backing.vtb.md->createMappingInTask(kernel_task, 0U, kIOMapAnywhere);
		if (!m) {
			SFLog(1, "%s[%#x]: Failed to map backing in kernel\n", __FUNCTION__, m_wID);
			return kIOReturnError;
		}
		*holder = m;
		t = owner->m_backing.offset + m_scale.reserved[2];
		if(t>m->getLength()){m->release();*holder=0;return kIOReturnOverrun;}
		*base = m->getVirtualAddress() + t;
		*limit_from_base = m->getLength() - t;
		return kIOReturnSuccess;
	}
	if (owner->m_backing.self) {
		*holder = 0;
		t = m_scale.reserved[2];
		if(t>owner->m_backing.size)return kIOReturnOverrun;
		*base = reinterpret_cast<IOVirtualAddress>(owner->m_backing.self) + t;
		*limit_from_base = owner->m_backing.size - t;
		return kIOReturnSuccess;
	}
	return kIOReturnNotReady;
}

#pragma mark -
#pragma mark Private Support Methods - 3D
#pragma mark -

HIDDEN
void CLASS::Start3D()
{
	bHaveScreenObject = m_provider->HaveScreen();
	if (!bHaveScreenObject) {
		bHaveMasterSurface = m_provider->retainMasterSurface();
		if (!bHaveMasterSurface)
			return;
	}
	if (bHaveScreenObject)
		return;
	if (checkOptionAC(VMW_OPTION_AC_DIRECT_BLIT))
		bDirectBlit = true;
	else {
		IOReturn rc = m_provider->getBlitBugResult();
		if (rc == kIOReturnNotFound) {
			rc = detectBlitBug();
			m_provider->cacheBlitBugResult(rc);
			if (rc != kIOReturnSuccess)
				SFLog(1, "Blit Bug: Yes\n");
		}
		bDirectBlit = (rc == kIOReturnSuccess);
	}
}

HIDDEN
void CLASS::Cleanup3D()
{
	if (!m_provider || !haveFrontBuffer())
		return;
	if (bHaveMasterSurface)
		m_provider->releaseMasterSurface();
	bHaveMasterSurface = false;
	bHaveScreenObject = false;
}

HIDDEN
IOReturn CLASS::detectBlitBug()
{
	IOReturn rc;
	uint32_t* ptr = 0;
	uint32_t const sid = 666;
	uint32_t const pixval_check = 0xFF000000U;
	uint32_t const pixval_green = 0xFF00U;
	uint32_t const w = 10;
	uint32_t const h = 10;
	uint32_t const bytes_per_pixel = sizeof(uint32_t);
	uint32_t i, pixels;
	DefineRegion<1U> tmpRegion;
	VMsvga2Accel::ExtraInfo extra;

	if (!m_provider)
		return kIOReturnNotReady;
	pixels = w * h / 4;
	rc = m_provider->createSurface(sid,
								   SVGA3dSurfaceFlags(0),
								   SVGA3D_X8R8G8B8,
								   w,
								   h);
	if (rc != kIOReturnSuccess)
		return kIOReturnError;
	ptr = static_cast<uint32_t*>(m_provider->VRAMMalloc(PAGE_SIZE));
	if (!ptr) {
		rc = kIOReturnNoMemory;
		goto exit;
	}
	set_region(&tmpRegion.r, 0, 0, w/2, h/2);
	for (i = 0; i < pixels; ++i)
		ptr[i] = pixval_green;
	bzero(&extra, sizeof extra);
	extra.mem_gmr_id = GMR_VRAM();
	extra.mem_offset_in_gmr = reinterpret_cast<vm_offset_t>(ptr) - CLIENT_ADDR_TO_UINTPTR_T(m_screenInfo.client_addr);
	extra.mem_pitch = w * bytes_per_pixel / 2;
	extra.dstDeltaX = w/2;
	extra.dstDeltaY = h/2;
	rc = m_provider->surfaceDMA2D(sid, SVGA3D_WRITE_HOST_VRAM, &tmpRegion.r, &extra);
	if (rc != kIOReturnSuccess) {
		rc = kIOReturnError;
		goto exit;
	}
	extra.dstDeltaX = 0;
	extra.dstDeltaY = 0;
	rc = m_provider->surfaceDMA2D(sid, SVGA3D_READ_HOST_VRAM, &tmpRegion.r, &extra);
	if (rc != kIOReturnSuccess) {
		rc = kIOReturnError;
		goto exit;
	}
	m_provider->SyncFIFO();
	rc = kIOReturnSuccess;
	for (i = 0; i < pixels; ++i)
		if (ptr[i] != pixval_check) {
			rc = kIOReturnUnsupported;
			break;
		}
exit:
	if (ptr)
		m_provider->VRAMFree(ptr);
	m_provider->destroySurface(sid);
	SFLog(2, "%s: returns %#x\n", __FUNCTION__, rc);
	return rc;
}

HIDDEN
IOReturn CLASS::DMAOutDirect(bool withFence)
{
	VMsvga2Accel::ExtraInfo extra;

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	/*
	 * Note: this code asssumes 1:1 scale (m_last_region->bounds.w == m_scale.buffer.w && m_last_region->bounds.h == m_scale.buffer.h)
	 */
	bzero(&extra, sizeof extra);
	extra.mem_gmr_id = m_backing.vtb.gmr_id;
	extra.mem_offset_in_gmr = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	extra.srcDeltaX = -static_cast<int>(m_last_region->bounds.x);
	extra.srcDeltaY = -static_cast<int>(m_last_region->bounds.y);
	if (m_provider->surfaceDMA2D(m_provider->getMasterSurfaceID(),
								 SVGA3D_WRITE_HOST_VRAM,
								 m_last_region,
								 &extra,
								 withFence ? &m_backing.vtb.fence : 0) != kIOReturnSuccess)
		return kIOReturnDMAError;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::DMAOutWithCopy(bool withFence)
{
	IOReturn rc;
	int deltaX, deltaY;
	uint32_t aux_sid;
	VMsvga2Accel::ExtraInfo extra;

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	/*
	 * Note: this code asssumes 1:1 scale (m_last_region->bounds.w == m_scale.buffer.w && m_last_region->bounds.h == m_scale.buffer.h)
	 */
	deltaX = -static_cast<int>(m_last_region->bounds.x);
	deltaY = -static_cast<int>(m_last_region->bounds.y);
	aux_sid = m_provider->AllocSurfaceID();
	rc = m_provider->createSurface(aux_sid,
								   SVGA3dSurfaceFlags(0),
								   m_surfaceFormat,
								   m_scale.buffer.w,
								   m_scale.buffer.h);
	if (rc != kIOReturnSuccess) {
		m_provider->FreeSurfaceID(aux_sid);
		return kIOReturnNoResources;
	}
	extra.mem_gmr_id = m_backing.vtb.gmr_id;
	extra.mem_offset_in_gmr = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	extra.srcDeltaX = deltaX;
	extra.srcDeltaY = deltaY;
	extra.dstDeltaX = deltaX;
	extra.dstDeltaY = deltaY;
	rc = m_provider->surfaceDMA2D(aux_sid,
								  SVGA3D_WRITE_HOST_VRAM,
								  m_last_region,
								  &extra,
								  withFence ? &m_backing.vtb.fence : 0);
	if (rc != kIOReturnSuccess) {
		m_provider->destroySurface(aux_sid);
		m_provider->FreeSurfaceID(aux_sid);
		return kIOReturnDMAError;
	}
	bzero(&extra, sizeof extra);
	extra.srcDeltaX = deltaX;
	extra.srcDeltaY = deltaY;
	rc = m_provider->surfaceCopy(aux_sid,
								 m_provider->getMasterSurfaceID(),
								 m_last_region,
								 &extra);
	m_provider->destroySurface(aux_sid);
	m_provider->FreeSurfaceID(aux_sid);
	if (rc != kIOReturnSuccess)
		return kIOReturnNotWritable;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::DMAOutStretchWithCopy(bool withFence)
{
	IOReturn rc;
	uint32_t width, height, aux_sid[2];
	VMsvga2Accel::ExtraInfo extra;
	IOAccelBounds bounds;
	DefineRegion<1U> tmpRegion;

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	width  = m_scale.buffer.w;
	height = m_scale.buffer.h;
	aux_sid[1] = m_provider->AllocSurfaceID();
	rc = m_provider->createSurface(aux_sid[1],
								   SVGA3dSurfaceFlags(0),
								   m_surfaceFormat,
								   width,
								   height);
	if (rc != kIOReturnSuccess) {
		m_provider->FreeSurfaceID(aux_sid[1]);
		return kIOReturnNoResources;
	}
	set_region(&tmpRegion.r, 0, 0, width, height);
	bzero(&extra, sizeof extra);
	extra.mem_gmr_id = m_backing.vtb.gmr_id;
	extra.mem_offset_in_gmr = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	rc = m_provider->surfaceDMA2D(aux_sid[1],
								  SVGA3D_WRITE_HOST_VRAM,
								  &tmpRegion.r,
								  &extra,
								  withFence ? &m_backing.vtb.fence : 0);
	if (rc != kIOReturnSuccess) {
		m_provider->destroySurface(aux_sid[1]);
		m_provider->FreeSurfaceID(aux_sid[1]);
		return kIOReturnDMAError;
	}
	memcpy(&bounds, &m_last_region->bounds, sizeof bounds);
	bounds.x = 0;
	bounds.y = 0;
	aux_sid[0] = m_provider->AllocSurfaceID();
	rc = m_provider->createSurface(aux_sid[0],
								   SVGA3dSurfaceFlags(0),
								   m_surfaceFormat,
								   bounds.w,
								   bounds.h);
	if (rc != kIOReturnSuccess) {
		m_provider->FreeSurfaceID(aux_sid[0]);
		m_provider->destroySurface(aux_sid[1]);
		m_provider->FreeSurfaceID(aux_sid[1]);
		return kIOReturnNoResources;
	}
	rc = m_provider->surfaceStretch(aux_sid[1],
									aux_sid[0],
									SVGA3D_STRETCH_BLT_LINEAR,
									&tmpRegion.r.bounds,
									&bounds);
	m_provider->destroySurface(aux_sid[1]);
	m_provider->FreeSurfaceID(aux_sid[1]);
	if (rc != kIOReturnSuccess) {
		m_provider->destroySurface(aux_sid[0]);
		m_provider->FreeSurfaceID(aux_sid[0]);
		return kIOReturnNotWritable;
	}
	bzero(&extra, sizeof extra);
	extra.srcDeltaX = -static_cast<int>(m_last_region->bounds.x);
	extra.srcDeltaY = -static_cast<int>(m_last_region->bounds.y);
	rc = m_provider->surfaceCopy(aux_sid[0],
								 m_provider->getMasterSurfaceID(),
								 m_last_region,
								 &extra);
	m_provider->destroySurface(aux_sid[0]);
	m_provider->FreeSurfaceID(aux_sid[0]);
	if (rc != kIOReturnSuccess)
		return kIOReturnNotWritable;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::doPresent()
{
	VMsvga2Accel::ExtraInfo extra;

	if (!m_last_region)
		return kIOReturnNotReady;
	bzero(&extra, sizeof extra);
	if (m_provider->surfacePresentAutoSync(m_provider->getMasterSurfaceID(),
										   m_last_region,
										   &extra) != kIOReturnSuccess)
		return kIOReturnIOError;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Private Support Methods - Screen Object
#pragma mark -

HIDDEN
IOReturn CLASS::ScreenObjectOutDirect(bool withFence)
{
	VMsvga2Accel::ExtraInfo extra;

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	/*
	 * Note: this code asssumes 1:1 scale (m_last_region->bounds.w == m_scale.buffer.w && m_last_region->bounds.h == m_scale.buffer.h)
	 */
	bzero(&extra, sizeof extra);
	extra.mem_gmr_id = m_backing.vtb.gmr_id;
	extra.mem_offset_in_gmr = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	extra.srcDeltaX = -static_cast<int>(m_last_region->bounds.x);
	extra.srcDeltaY = -static_cast<int>(m_last_region->bounds.y);
	if (m_provider->blitToScreen(m_framebufferIndex,
								 m_last_region,
								 &extra,
								 withFence ? &m_backing.vtb.fence : 0) != kIOReturnSuccess)
		return kIOReturnDMAError;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::ScreenObjectOutVia3D(bool withFence)
{
	IOReturn rc;
	uint32_t width, height, aux_sid;
	VMsvga2Accel::ExtraInfo extra;
	DefineRegion<1U> tmpRegion;

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	width  = m_scale.buffer.w;
	height = m_scale.buffer.h;
	aux_sid = m_provider->AllocSurfaceID();
	rc = m_provider->createSurface(aux_sid,
								   SVGA3dSurfaceFlags(0),
								   m_surfaceFormat,
								   width,
								   height);
	if (rc != kIOReturnSuccess) {
		m_provider->FreeSurfaceID(aux_sid);
		return kIOReturnNoResources;
	}
	set_region(&tmpRegion.r, 0, 0, width, height);
	bzero(&extra, sizeof extra);
	extra.mem_gmr_id = m_backing.vtb.gmr_id;
	extra.mem_offset_in_gmr = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	rc = m_provider->surfaceDMA2D(aux_sid,
								  SVGA3D_WRITE_HOST_VRAM,
								  &tmpRegion.r,
								  &extra,
								  withFence ? &m_backing.vtb.fence : 0);
	if (rc != kIOReturnSuccess) {
		m_provider->destroySurface(aux_sid);
		m_provider->FreeSurfaceID(aux_sid);
		return kIOReturnDMAError;
	}
	rc = m_provider->blitSurfaceToScreen(aux_sid,
										 m_framebufferIndex,
										 &tmpRegion.r.bounds,
										 m_last_region);
	m_provider->destroySurface(aux_sid);
	m_provider->FreeSurfaceID(aux_sid);
	if (rc != kIOReturnSuccess)
		return kIOReturnNotWritable;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Private Support Methods - GFB
#pragma mark -

HIDDEN
IOReturn CLASS::GFBOutDirect()
{
	ContentRead reading(this);
	if(reading.error)return reading.error;
	VMsvga2Accel::ExtraInfo extra;
	IOVirtualAddress base;
	vm_size_t limit_from_base;
	IOMemoryMap* holder;
	IOReturn rc;
	uint32_t rect[4];

	if (!m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	/*
	 * Note: this code asssumes 1:1 scale (m_last_region->bounds.w == m_scale.buffer.w && m_last_region->bounds.h == m_scale.buffer.h)
	 */
	// Reuse the backing's prepared lifetime; no per-frame user/kernel map or
	// emulated memcpy on the supported path. Unsupported requests perform no
	// writes and fall through to the established implementation below.
	rc = kIOReturnUnsupported;
	if (m_backing.vtb.md && m_prepared_backing) {
		uint64_t offset=static_cast<uint64_t>(m_backing.offset)+m_scale.reserved[2];
		if (offset<=m_backing.vtb.md->getLength())
			rc=m_provider->directFramebufferBlit(m_backing.vtb.md,offset,m_backing.vtb.md->getLength()-offset,
												m_scale.reserved[1],m_last_region);
	} else if (!m_backing.vtb.md && m_backing.self && m_scale.reserved[2]<=m_backing.size) {
		rc=m_provider->directFramebufferBlit(0,m_provider->offsetInVRAM(m_backing.self)+m_scale.reserved[2],
											m_backing.size-m_scale.reserved[2],m_scale.reserved[1],m_last_region);
	}
	if (rc==kIOReturnSuccess)goto update;
	rc = obtainKernelPtrs(&base, &limit_from_base, &holder);
	if (rc != kIOReturnSuccess)
		return rc;
	bzero(&extra, sizeof extra);
	extra.mem_gmr_id = m_backing.vtb.gmr_id;
	extra.mem_offset_in_gmr = 0U;
	extra.mem_pitch = m_scale.reserved[1];
	/*
	 * Note: dst applies to backing, src to GFB
	 */
	extra.dstDeltaX = -static_cast<int>(m_last_region->bounds.x);
	extra.dstDeltaY = -static_cast<int>(m_last_region->bounds.y);
	rc = m_provider->blitGFB(m_framebufferIndex,
							 m_last_region,
							 &extra,
							 base,
							 limit_from_base,
							 1);
	if (holder)
		holder->release();
	if (rc != kIOReturnSuccess)
		return kIOReturnDMAError;
update:
	/*
	 * Note: we do a lazy UpdateFramebuffer on the bounds
	 *   instead of on each individual rect.
	 */
	rect[0] = m_last_region->bounds.x;
	rect[1] = m_last_region->bounds.y;
	rect[2] = m_last_region->bounds.w;
	rect[3] = m_last_region->bounds.h;
	m_provider->UpdateFramebufferAutoRing(&rect[0]);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Private Support Methods - Video
#pragma mark -

HIDDEN
void CLASS::clear_yuv_to_black(void* buffer, vm_size_t size)
{
	vm_size_t s = size / sizeof(uint32_t);	// size should already be aligned to PAGE_SIZE
	uint32_t pixval = 0U;

	switch (m_video.vmware_pixel_format) {
		case VMWARE_FOURCC_UYVY:
			pixval = 0x10801080U;
			break;
		case VMWARE_FOURCC_YUY2:
			pixval = 0x80108010U;
			break;
		default:
			return;				// Unsupported
	}
	/*
	 * XMM (SSE2) version
	 */
#ifdef VECTORIZE
	if ((reinterpret_cast<vm_address_t>(buffer) & 0xFU) == 0U) {
		__v4si p = { pixval, pixval, pixval, pixval };
		memset128(buffer, p, size / sizeof p);
		return;
	}
#endif /* VECTORIZE */
	memset32(buffer, pixval, s);
}

/*
 * This sets up a trick in order to get IOYUVImageCodecMergeFloatingImageOntoWindow
 *   to believe that a YUV CGSSurface is all-black thereby fooling it into flooding the
 *   background window behind the video frame with black in order satify
 *   VMware's black colorkey requirement.
 *
 * The trick exploits a design flaw in Apple's CGSSurface technology.  All requests by
 *   an application to create a surface (CGSAddSurface) are routed to the WindowServer.
 *   As a result, the WindowServer is the owning task for all CGSSurfaces, and any
 *   attempt to lock it via IOAccelSurfaceConnect expect a virtual address that is valid
 *   inside the WindowServer, not inside the application requesting the surface.
 * OTOH, the application later attaches to the CGSSurface by using the GA's AllocateSurface
 *   method.  It then uses LockSurface and expects a virtual address that is valid in its
 *   own process.  This is a design flaw since the application expects to be able to "lock"
 *   the surface from two different tasks - its own and the WindowServer at the same time,
 *   and provide each a view.
 * The driver can use this flaw by handing the two tasks views to different memory, making
 *   the WindowServer believe that the surface is all black, even though the application
 *   has already placed the first video frame in the surface.
 */
HIDDEN
IOReturn CLASS::setup_trick_buffer()
{
	IOBufferMemoryDescriptor* mem;
	IOMemoryMap* my_map_of_mem;

	if (!isBackingValid())
		return kIOReturnNotReady;
	mem = IOBufferMemoryDescriptor::inTaskWithOptions(0,
													  kIOMemoryKernelUserShared |
													  kIOMemoryPageable |
													  kIODirectionInOut,
													  m_backing.size,
													  page_size);
	if (!mem)
		return kIOReturnNoMemory;
	if (mem->prepare() != kIOReturnSuccess) {
		mem->release();
		return kIOReturnNoMemory;
	}
	my_map_of_mem = mem->createMappingInTask(kernel_task, 0U, kIOMapAnywhere);
	if (!my_map_of_mem) {
		mem->complete();
		mem->release();
		return kIOReturnNoMemory;
	}
	clear_yuv_to_black(reinterpret_cast<void*>(my_map_of_mem->getVirtualAddress()), m_backing.size);
	my_map_of_mem->release();
	mem->complete();
	releaseBackingMap(0U);
	m_backing.map[0] = mem->createMappingInTask(m_owning_task, 0U, kIOMapAnywhere);
	mem->release();
	return kIOReturnSuccess;
}

/*
 * Note: The WindowServer expects the driver to be able to clip the
 *   overlayed video frame based on the destination region, but
 *   VMware SVGA II doesn't support this.  So we take an all-or-nothing
 *   approach.  Either overlay the entire frame, or show nothing.
 */
HIDDEN
bool CLASS::setVideoDest()
{
	if (!m_last_region)
		return false;
	if (isRegionEmpty(m_last_region)) {
		memset32(&m_video.unit.dstX, 0U, 4U);
		return true;
	}
	m_video.unit.dstX = static_cast<uint32_t>(m_last_region->bounds.x);
	m_video.unit.dstY = static_cast<uint32_t>(m_last_region->bounds.y);
	m_video.unit.dstWidth = static_cast<uint32_t>(m_last_region->bounds.w);
	m_video.unit.dstHeight = static_cast<uint32_t>(m_last_region->bounds.h);
	return true;
}

HIDDEN
bool CLASS::setVideoRegs()
{
	if (!isBackingValid())
		return false;
	bzero(&m_video.unit, sizeof m_video.unit);
	m_video.unit.dataOffset = static_cast<uint32_t>(m_backing.offset);
	m_video.unit.format = m_video.vmware_pixel_format;
	m_video.unit.width = static_cast<uint32_t>(m_scale.source.w);
	m_video.unit.height = static_cast<uint32_t>(m_scale.source.h);
	m_video.unit.srcX = static_cast<uint32_t>(m_scale.buffer.x);
	m_video.unit.srcY = static_cast<uint32_t>(m_scale.buffer.y);
	m_video.unit.srcWidth = static_cast<uint32_t>(m_scale.buffer.w);
	m_video.unit.srcHeight = static_cast<uint32_t>(m_scale.buffer.h);
	m_video.unit.pitches[0] = m_scale.reserved[1];
	m_video.unit.size = m_video.unit.height * m_video.unit.pitches[0];
	m_video.unit.dataGMRId = m_backing.vtb.gmr_id;
	m_video.unit.dstScreenId = m_framebufferIndex;
	return true;
}

HIDDEN
void CLASS::videoReshape()
{
	if (!bVideoMode || !m_video.unit.enabled)
		return;
	if (!setVideoDest())
		return;
	m_provider->VideoSetRegsInRange(m_video.stream_id,
									&m_video.unit,
									SVGA_VIDEO_DST_X,
									SVGA_VIDEO_DST_HEIGHT,
									&m_backing.vtb.fence);
}

#pragma mark -
#pragma mark IONVSurface Methods
#pragma mark -

HIDDEN
IOReturn CLASS::surface_read_lock_options(eIOAccelSurfaceLockBits options, IOAccelSurfaceInformation* info, size_t* infoSize)
{
	if(m_gpu_primary){
		IOReturn rc=m_gpu_primary->surface_read_lock_options(options,info,infoSize);
		if(!rc){info->address[0]+=static_cast<int64_t>(m_scale.reserved[2])-m_gpu_primary->m_scale.reserved[2];
			info->width=m_scale.buffer.w;info->height=m_scale.buffer.h;}
		return rc;
	}
	SFLog(3, "%s[%#x](%#x, %p, %lu)\n", __FUNCTION__, m_wID, options, info, infoSize ? *infoSize : 0UL);

	if (!info || !infoSize || *infoSize < sizeof *info)
		return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	bzero(info, *infoSize);
	IOLockLock(m_content_lock);
	if(m_active_backing_loan || m_content_closing){IOLockUnlock(m_content_lock);return kIOReturnCannotLock;}
	bool already_read=OSTestAndSet(vmSurfaceLockRead, &bIsLocked);
	IOReturn materialized=already_read?kIOReturnCannotLock:materializeGPU();
	if(!already_read && materialized)OSTestAndClear(vmSurfaceLockRead,&bIsLocked);
	IOLockUnlock(m_content_lock);
	if (materialized)return materialized;
	if (isClientBackingValid())
		goto finishup;
	if (!allocBacking() || !mapBacking(m_owning_task, 0U)) {
		OSTestAndClear(vmSurfaceLockRead, &bIsLocked);
		return kIOReturnNoMemory;
	}
finishup:
	calculateSurfaceInformation(info);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surface_read_unlock_options(eIOAccelSurfaceLockBits options)
{
	if(m_gpu_primary)return m_gpu_primary->surface_read_unlock_options(options);
	if(m_gpu_presentation)releaseBackingMap(0U);
	SFLog(3, "%s[%#x](%#x)\n", __FUNCTION__, m_wID, options);

	OSTestAndClear(vmSurfaceLockRead, &bIsLocked);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::get_state(eIOAccelSurfaceStateBits* state)
{
	SFLog(2, "%s[%#x](%p)\n", __FUNCTION__, m_wID, state);

	if (state)
		*state = kIOAccelSurfaceStateNone;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surface_write_lock_options(eIOAccelSurfaceLockBits options, IOAccelSurfaceInformation* info, size_t* infoSize)
{
	if(m_gpu_primary){
		IOReturn rc=m_gpu_primary->surface_write_lock_options(options,info,infoSize);
		if(!rc){info->address[0]+=static_cast<int64_t>(m_scale.reserved[2])-m_gpu_primary->m_scale.reserved[2];
			info->width=m_scale.buffer.w;info->height=m_scale.buffer.h;}
		return rc;
	}
	bool already_write;
	IOReturn materialized;
	SFLog(3, "%s[%#x](%#x, %p, %lu)\n", __FUNCTION__, m_wID, options, info, infoSize ? *infoSize : 0UL);

	if (!info || !infoSize || *infoSize < sizeof *info)
		return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	bzero(info, *infoSize);
	if (bSkipWriteLockOnce) {
		bSkipWriteLockOnce = false;
		goto skip;
	}
	IOLockLock(m_content_lock);
	if(m_active_backing_loan || m_content_closing){IOLockUnlock(m_content_lock);return kIOReturnCannotLock;}
	already_write=OSTestAndSet(vmSurfaceLockWrite, &bIsLocked);
	materialized=already_write?kIOReturnCannotLock:materializeGPU();
	if(!already_write && materialized)OSTestAndClear(vmSurfaceLockWrite,&bIsLocked);
	IOLockUnlock(m_content_lock);
	if(materialized)return materialized;
skip:
	if (isClientBackingValid())
		goto finishup;
	if (!allocBacking() || !mapBacking(m_owning_task, 0U)) {
		OSTestAndClear(vmSurfaceLockWrite, &bIsLocked);
		return kIOReturnNoMemory;
	}
finishup:
	calculateSurfaceInformation(info);
	/*
	 * If we're not using packed backing, we let the Window Server run free over
	 *   its shadow framebuffer without syncing to any pending DMA transfers.
	 */
	if (!m_scale.reserved[2])
		m_backing.vtb.sync(m_provider);
	nativeContentWrite(true);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surface_write_unlock_options(eIOAccelSurfaceLockBits options)
{
	if(m_gpu_primary)return m_gpu_primary->surface_write_unlock_options(options);
	if(m_gpu_presentation)releaseBackingMap(0U);
	SFLog(3, "%s[%#x](%#x)\n", __FUNCTION__, m_wID, options);

	nativeContentWrite(false);
	OSTestAndClear(vmSurfaceLockWrite, &bIsLocked);
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surface_read(IOAccelSurfaceReadData const* parameters, size_t parametersSize)
{
	SFLog(2, "%s[%#x](%p, %lu)\n", __FUNCTION__, m_wID, parameters, parametersSize);

	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::set_shape_backing(eIOAccelSurfaceShapeBits options,
								  uintptr_t framebufferIndex,
								  IOVirtualAddress backing,
								  size_t rowbytes,
								  IOAccelDeviceRegion const* rgn,
								  size_t rgnSize)
{
	return set_shape_backing_length_ext(options, framebufferIndex, backing, rowbytes, 0, rgn, rgnSize);
}

HIDDEN
IOReturn CLASS::set_id_mode(uintptr_t wID, eIOAccelSurfaceModeBits modebits)
{
	// Finish the old backing's acquisition before changing its family owner.
	{ContentRead access(this);if(access.error)return access.error;}
	SFLog(2, "%s(%#lx, %#x)\n", __FUNCTION__, wID, modebits);

	/*
	 * wID == 1U is used by Mac OS X's WindowServer
	 * Let the WindowServer know if we support SVGAScreen or SVGA3D
	 */
	if (wID == 1U) {
		// Native VMware GL requires a hardware front buffer. The explicitly
		// paired PassthroughOpenGL compositor renders externally and presents through our
		// validated CPU/GFB blit path instead; rejecting its primary CGS
		// surface prevents CoreGraphics from ever acquiring display GL.
		// Keep the retired personality key readable for existing research EFIs.
		if (!haveFrontBuffer() && !m_provider->getProperty("VIMDirectGLCompositor") &&
			!m_provider->getProperty("VIMMesaCompositor"))
			return kIOReturnUnsupported;
		if (bHaveScreenObject) {
			IOReturn rc = m_provider->createPrimaryScreen(m_screenInfo.w, m_screenInfo.h);
			if (rc != kIOReturnSuccess)
				return rc;
		}
	}

	const uint32_t extra=modebits & ~(kIOAccelSurfaceModeColorDepthBits | kIOAccelSurfaceModeWindowedBit);
	/* Mavericks CGLSetPBuffer creates independent ARGB8888 2D/rectangle
	 * texture surfaces with modes 0x100024/0x200024. Unlike display companions this
	 * surface has no primary-screen parent. Rejecting the rectangle-target
	 * bit makes native CGL return kCGLBadAlloc before GLI attachment, then
	 * CA's image queue publishes surface ID zero. Keep all other unknown
	 * modes unsupported; the existing shape/scale/backing checks still apply.
	 */
	const bool rgba16_pbuffer=m_provider->getProperty("VIMDirectGLCompositor") &&
		wID>2U && wID<=UINT32_MAX && (extra==0 || extra==0x100000U || extra==0x200000U) &&
		(modebits & kIOAccelSurfaceModeColorDepthBits)==0xbU;
	bool period_private_pbuffer=false;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	/* Actual9J3050 public CGLSetPBuffer creates its independent rectangle
	 * surface with mode0x24, without the later target bits. Native9400M
	 * admits it; our pbuffer-only connection rejected it as a window claim.
	 * Admit only that connection's fresh private allocation. The reserved-ID,
	 * duplicate-owner and no-client-backing/shape guards remain unchanged;
	 * trusted CGS window surfaces must not become offscreen pbuffers. */
	period_private_pbuffer=m_pbuffer_only && extra==0;
#endif
	const bool cgl_pbuffer=rgba16_pbuffer || (m_provider->getProperty("VIMDirectGLCompositor") &&
		wID>1U && wID<=UINT32_MAX && (extra==0x100000U || extra==0x200000U || period_private_pbuffer) &&
		(modebits & kIOAccelSurfaceModeColorDepthBits)==kIOAccelSurfaceModeColorDepth8888);
	if(m_pbuffer_only && (!cgl_pbuffer || wID<=2U))return kIOReturnNotPrivileged;
	bool gpu_primary=false;
	VMsvga2Surface* gpu_parent=0;
	if(extra && m_provider->getProperty("VIMDirectGLGPUPresentation")){
		if(wID==1U && extra==0x8400U)gpu_primary=true;
		else if(extra==0x800U && wID!=1U){
			gpu_parent=m_provider->findSurfaceForID(1U);
			if(gpu_parent)gpu_parent->retain();
			if(gpu_parent && (gpu_parent->m_owning_task!=m_owning_task || !gpu_parent->m_gpu_presentation ||
			   (gpu_parent->m_gpu_companion_id && gpu_parent->m_gpu_companion_id!=wID))){
				gpu_parent->release();gpu_parent=0;
			}
		}
	}
	if (extra && !gpu_primary && !gpu_parent && !cgl_pbuffer)
		return kIOReturnUnsupported;
	if (!(modebits & kIOAccelSurfaceModeWindowedBit)){
		if(gpu_parent)gpu_parent->release();
		return kIOReturnUnsupported;
	}
	switch (modebits & kIOAccelSurfaceModeColorDepthBits) {
		case 0xbU: // Actual Snow cglsCreatePBuffer GL_RGBA16; not half float.
			if(!rgba16_pbuffer)return kIOReturnUnsupported;
			m_surfaceFormat=SVGA3D_A16B16G16R16;
			m_bytes_per_pixel=8;m_pixel_format=0xbU;
			break;
		case kIOAccelSurfaceModeColorDepth1555:
			m_surfaceFormat = SVGA3D_X1R5G5B5;
			m_bytes_per_pixel = sizeof(uint16_t);
			m_pixel_format = kIOAccelSurfaceModeColorDepth1555;
			break;
		case kIOAccelSurfaceModeColorDepth8888:
			m_surfaceFormat = SVGA3D_X8R8G8B8;
			m_bytes_per_pixel = sizeof(uint32_t);
			m_pixel_format = kIOAccelSurfaceModeColorDepth8888;
			break;
		case kIOAccelSurfaceModeColorDepthBGRA32:
			m_surfaceFormat = SVGA3D_A8R8G8B8;
			m_bytes_per_pixel = sizeof(uint32_t);
			m_pixel_format = kIOAccelSurfaceModeColorDepth8888;
			break;
		case kIOAccelSurfaceModeColorDepthYUV:
			m_surfaceFormat = SVGA3D_YUY2;
			m_bytes_per_pixel = sizeof(uint16_t);
			m_pixel_format = kIOYUVSPixelFormat;
			break;
		case kIOAccelSurfaceModeColorDepthYUV2:
			m_surfaceFormat = SVGA3D_UYVY;
			m_bytes_per_pixel = sizeof(uint16_t);
			m_pixel_format = kIO2vuyPixelFormat;
			break;
		default:
			if(gpu_parent)gpu_parent->release();
			return kIOReturnUnsupported;
	}
	if (!bHaveID || m_wID!=static_cast<uint32_t>(wID)) noteBackingChange();
	IOReturn claimed=m_provider->claimSurfaceID(this,static_cast<uint32_t>(wID));
	if(claimed){if(gpu_parent)gpu_parent->release();return claimed;}
	m_cgl_pbuffer=cgl_pbuffer;
	setProperty("CGSSurfaceID", static_cast<uint64_t>(m_wID), 32U);
	if(gpu_primary)m_gpu_presentation=true;
	if(gpu_parent){
		if(m_gpu_primary)m_gpu_primary->release();
		m_gpu_primary=gpu_parent;gpu_parent->m_gpu_companion_id=m_wID;
	}
	return kIOReturnSuccess;
}

VMsvga2Surface* CLASS::gpuPresentationRoot() const
{
	if(m_gpu_primary)return m_gpu_primary->m_gpu_presentation?m_gpu_primary:0;
	return m_gpu_presentation?const_cast<VMsvga2Surface*>(this):0;
}
IOReturn CLASS::gpuPresentationFlush(uintptr_t framebufferMask,IOOptionBits options)
{
	(void)options;
	VMsvga2Surface* root=gpuPresentationRoot();
	if(!root)return kIOReturnOffline;
	if(root!=this)return root->gpuPresentationFlush(framebufferMask,options);
	if(!(framebufferMask&(1U<<m_framebufferIndex)))return kIOReturnBadArgument;
	IOLockLock(m_content_lock);
	IOReturn error=kIOReturnNotReady;uint64_t ticket=0;
	if(m_gpu_hold && !m_content_writers && !m_native_write_active &&
	   !(bIsLocked&((128U>>vmSurfaceLockWrite)|(128U>>vmSurfaceLockContext))))
		error=m_provider->presentGPUSurface(m_gpu_hold,m_gpu_epoch,&ticket);
	IOLockUnlock(m_content_lock);return error;
}

HIDDEN
IOReturn CLASS::set_scale(eIOAccelSurfaceScaleBits options, IOAccelSurfaceScaling const* scaling, size_t scalingSize)
{
	ContentRead access(this);if(access.error)return access.error;
	SFLog(2, "%s[%#x](%#x, %p, %lu)\n", __FUNCTION__, m_wID, options, scaling, scalingSize);

	if (!scaling || scalingSize < sizeof *scaling)
		return kIOReturnBadArgument;
	memcpy(&m_scale, scaling, sizeof *scaling);
	calculateScaleParameters();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::set_shape(eIOAccelSurfaceShapeBits options, uintptr_t framebufferIndex, IOAccelDeviceRegion const* rgn, size_t rgnSize)
{
	return set_shape_backing_length_ext(options, framebufferIndex, 0, static_cast<size_t>(-1), 0, rgn, rgnSize);
}

HIDDEN
IOReturn CLASS::surface_flush(uintptr_t framebufferMask, IOOptionBits options)
{
	if(m_gpu_primary || m_gpu_presentation)return gpuPresentationFlush(framebufferMask,options);
	ContentRead access(this);if(access.error)return access.error;
	IOReturn rc;
	int DMA_type;
	bool withFence;

	SFLog(3, "%s[%#x](%lu, %#x)\n", __FUNCTION__, m_wID, framebufferMask, FMT_U(options));

	if (!bHaveID || !m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	if (bVideoMode)
		return kIOReturnSuccess;
#if 0
	if (!(framebufferMask & (1UL << m_framebufferIndex)))
		return kIOReturnSuccess;	// Note: nothing to do
#endif
	if (bHaveScreenObject) {
		if (m_surfaceFormat == SVGA3D_X8R8G8B8 && isIdentityScale()) {
			DMA_type = 3;	// direct
		} else {
			if (!m_provider->Have3D()) {
				SFLog(1, "%s[%#x]: called for SVGAScreen w/o SVGA3D, surface format == %d - unsupported\n",
					  __FUNCTION__, m_wID, m_surfaceFormat);
				return kIOReturnUnsupported;
			}
			DMA_type = 4;	// via 3D
		}
	} else if (bHaveMasterSurface) {
		/*
		 * Note: conditions for direct blit:
		 *   surfaceFormat same
		 *   1:1 scale
		 *   bDirectBlit flag
		 */
		if (!isIdentityScale()) {
			DMA_type = 2;	// stretch
		} else if (m_surfaceFormat == SVGA3D_X8R8G8B8 && bDirectBlit) {
			DMA_type = 0;	// direct
		} else {
			DMA_type = 1;	// copy
		}
	} else {
		// A8R8G8B8 and X8R8G8B8 have identical display RGB byte layout.
		if ((m_surfaceFormat != SVGA3D_X8R8G8B8 && m_surfaceFormat != SVGA3D_A8R8G8B8) || !isIdentityScale()) {
			SFLog(1, "%s[%#x]: called for GFB, surface format == %d - unsupported\n",
				  __FUNCTION__, m_wID, m_surfaceFormat);
			return kIOReturnUnsupported;
		}
		DMA_type = 5;
	}
	withFence = true;
	switch (DMA_type) {
		case 0:
			rc = DMAOutDirect(withFence);
			break;
		case 2:
			rc = DMAOutStretchWithCopy(withFence);
			break;
		case 3:
			rc = ScreenObjectOutDirect(withFence);
			break;
		case 4:
			rc = ScreenObjectOutVia3D(withFence);
			break;
		case 5:
			rc = GFBOutDirect();
			break;
		case 1:
		default:
			rc = DMAOutWithCopy(withFence);
			break;
	}
	if (rc != kIOReturnSuccess) {
		SFLog(1, "%s[%#x]: Output Blit failed, error == %#x\n", __FUNCTION__, m_wID, rc);
		return rc;
	}
	if (DMA_type <= 2)
		return doPresent();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surface_query_lock()
{
	SFLog(3, "%s[%#x]()\n", __FUNCTION__, m_wID);

	return (bIsLocked & ((128U >> vmSurfaceLockRead) | (128U >> vmSurfaceLockWrite))) != 0 ? kIOReturnCannotLock : kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surface_read_lock(IOAccelSurfaceInformation* info, size_t* infoSize)
{
	return surface_read_lock_options(kIOAccelSurfaceLockInDontCare, info, infoSize);
}

HIDDEN
IOReturn CLASS::surface_read_unlock()
{
	return surface_read_unlock_options(kIOAccelSurfaceLockInDontCare);
}

HIDDEN
IOReturn CLASS::surface_write_lock(IOAccelSurfaceInformation* info, size_t* infoSize)
{
	return surface_write_lock_options(kIOAccelSurfaceLockInAccel, info, infoSize);
}

HIDDEN
IOReturn CLASS::surface_write_unlock()
{
	return surface_write_unlock_options(kIOAccelSurfaceLockInDontCare);
}

HIDDEN
IOReturn CLASS::surface_control(uintptr_t selector, uintptr_t arg, uint32_t* result)
{
	VMsvga2Surface* root=gpuPresentationRoot();
	if(root && selector>=16U && selector<=18U){
		if(!result || arg!=root->m_framebufferIndex)return kIOReturnBadArgument;
		if(selector==17U){*result=1;return kIOReturnSuccess;}
		if(selector==18U){*result=root->m_gpu_invalidated?0x80000000U:0;root->m_gpu_invalidated=false;return kIOReturnSuccess;}
		// The single composition allocation can be seeded before a GPU hold
		// exists. Thereafter readiness is real host submission capacity.
		if(!root->m_gpu_service_ready){*result=1;return kIOReturnSuccess;}
		bool ready=false;IOReturn rc=m_provider->gpuPresentationReady(&ready);
		*result=ready?1U:0U;return rc;
	}
	SFLog(2, "%s[%#x](%lu, %lu, out)\n", __FUNCTION__, m_wID, selector, arg);

	/*
	 * Note: cases 4 & 5 have something to do with surface volatility.
	 *   This function is called from several locations
	 *   in CoreGraphics, and once from OpenGL.
	 */
	switch (selector) {
		case 1U:
			/*
			 * called from _CGXSynchronizeAcceleratedSurface, arg N/A, result ignored
			 */
			return kIOReturnSuccess;
		case 4U:
			/*
			 * called from CGLSetPBufferVolatileState, arg N/A, result passed to caller
			 */
			*result = 1U;
			return kIOReturnSuccess;
		case 5U:
			/*
			 * called from
			 *   CGXBackingStorePerformCompression with arg == 1, result used
			 *   destroyBackingSurface with arg == 0, result ignored
			 *   synchronizeBackingSurface with arg == 0, result ignored
			 *   allocateBackingSurface with arg == 1, result ignored
			 *   CGXBackingStoreDataAccess with arg == 0, result ignored
			 */
			/*
			 * remembers arg as a boolean value
			 * sets result to either 0 or 1
			 */
			*result = 1U;
			return kIOReturnSuccess;
	}
	return kIOReturnBadArgument;
}

HIDDEN
IOReturn CLASS::set_shape_backing_length(eIOAccelSurfaceShapeBits options,
										 uintptr_t framebufferIndex,
										 IOVirtualAddress backing,
										 size_t rowbytes,
										 size_t backingLength,
										 IOAccelDeviceRegion const* rgn /* , size_t rgnSize */)
{
	return set_shape_backing_length_ext(options, framebufferIndex, backing, rowbytes, backingLength, rgn,  rgn ? IOACCEL_SIZEOF_DEVICE_REGION(rgn) : 0);
}

HIDDEN
IOReturn CLASS::set_shape_backing_length_ext(eIOAccelSurfaceShapeBits options,
											 uintptr_t framebufferIndex,
											 mach_vm_address_t backing,
											 size_t rowbytes,
											 size_t backingLength,
											 IOAccelDeviceRegion const* rgn,
											 size_t rgnSize)
{
	bool bAllocShapeOk, bFromGFB;
	ContentRead access(this,!m_gpu_presentation && !m_gpu_primary);if(access.error)return access.error;
	VMsvga2Surface* gpu_root=gpuPresentationRoot();
	if(m_gpu_primary && !gpu_root)return kIOReturnOffline;
	FamilyMetadata metadata(gpu_root);if(metadata.error)return metadata.error;
	if(gpu_root && options==0x1000U){
		// One persistent composition backing: copying prior contents onto
		// themselves is already satisfied, not a framebuffer blit.
		return backing?kIOReturnUnsupported:kIOReturnSuccess;
	}
	if(gpu_root && backing)return kIOReturnUnsupported;

#if 0
	int const expectedOptions = kIOAccelSurfaceShapeIdentityScaleBit | kIOAccelSurfaceShapeFrameSyncBit;
#endif

	SFLog(3, "%s[%#x](%#x, %lu, %#llx, %ld, %lu, %p, %lu)\n",
		  __FUNCTION__,
		  m_wID,
		  options,
		  framebufferIndex,
		  backing,
		  rowbytes,
		  backingLength,
		  rgn,
		  rgnSize);

	if (!rgn || rgnSize < IOACCEL_SIZEOF_DEVICE_REGION(rgn))
		return kIOReturnBadArgument;
	// Native CGL's pbuffer shape has sized bounds but an empty visibility
	// rectangle: it is an offscreen allocation, not a visible window. Validate
	// before changing metadata and do not discard it as empty display damage.
	if (m_cgl_pbuffer && (backing || rgn->bounds.x || rgn->bounds.y ||
		rgn->bounds.w<=0 || rgn->bounds.h<=0 || rgn->bounds.w>8192 || rgn->bounds.h>8192))
		return kIOReturnBadArgument;

	if (rgnSize > 4096U) {	// sanity check
		SFLog(1, "%s: rgnSize == %lu too large, rejecting\n", __FUNCTION__, rgnSize);
		return kIOReturnNoSpace;
	}

#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3) {
		SFLog(3, "%s:   rgn->num_rects == %u, rgn->bounds == %d, %d, %d, %d\n",
			  __FUNCTION__,
			  FMT_U(rgn->num_rects),
			  rgn->bounds.x,
			  rgn->bounds.y,
			  rgn->bounds.w,
			  rgn->bounds.h);
		for (uint32_t i = 0U; i < rgn->num_rects; ++i) {
			SFLog(3, "%s:   rgn->rect[%u] == %d, %d, %d, %d\n",
				  __FUNCTION__,
				  i,
				  rgn->rect[i].x,
				  rgn->rect[i].y,
				  rgn->rect[i].w,
				  rgn->rect[i].h);
		}
	}
#endif

	clearLastRegion();
	bzero(&m_client_backing, sizeof m_client_backing);
#if 0
	if (!(options & kIOAccelSurfaceShapeNonBlockingBit))	// driver doesn't support waiting on locks
		return kIOReturnUnsupported;
	if ((options & expectedOptions) != expectedOptions)
		return kIOReturnSuccess;
#endif
	if (!m_cgl_pbuffer && !bVideoMode && isRegionEmpty(rgn))
		return kIOReturnSuccess;
	if (!bHaveID)
		return kIOReturnNotReady;
	if (backing) {
		size_t min_row_bytes = static_cast<size_t>(rgn->bounds.w) * m_bytes_per_pixel;
		if (static_cast<intptr_t>(rowbytes) <= 0)
			rowbytes = min_row_bytes;
		else if (rowbytes < min_row_bytes)
			return kIOReturnOverrun;
		size_t min_size = static_cast<size_t>(rgn->bounds.h) * rowbytes;
		if (!backingLength)
			backingLength = min_size;
		else if (backingLength < min_size)
			return kIOReturnOverrun;
		m_client_backing.addr = backing;
		m_client_backing.rowbytes = rowbytes;
		m_client_backing.size = backingLength;
		if (m_backing.vtb.gart_ptr)		// mark client backing changed
			m_backing.vtb.gart_ptr = 2U;
	}
	if (!m_last_shape) {
		m_last_shape = OSData::withBytes(rgn, static_cast<unsigned>(rgnSize));
		bAllocShapeOk = (m_last_shape != 0);
	} else
		bAllocShapeOk = m_last_shape->initWithBytes(rgn, static_cast<unsigned>(rgnSize));
	if (!bAllocShapeOk) {
		bzero(&m_client_backing, sizeof m_client_backing);
		return kIOReturnNoMemory;
	}
	m_last_region = static_cast<IOAccelDeviceRegion const*>(m_last_shape->getBytesNoCopy());
	m_framebufferIndex = static_cast<uint32_t>(framebufferIndex);
	// CGLSetPBuffer supplies the pbuffer's exact extent with shape options
	// NonBlocking|WaitEnabled (0x81), not the display's IdentityScale flag.
	// It makes no GA AllocateSurface/ScaleSurface call. This independent
	// allocation therefore takes its source size from that validated shape.
	if ((options & kIOAccelSurfaceShapeIdentityScaleBit) || m_cgl_pbuffer) {
		if(gpu_root){
			const int width=gpu_root->m_screenInfo.w,height=gpu_root->m_screenInfo.h;
			if(rgn->bounds.x<0 || rgn->bounds.y<0 || rgn->bounds.w<=0 || rgn->bounds.h<=0 ||
			   rgn->bounds.x+rgn->bounds.w>width || rgn->bounds.y+rgn->bounds.h>height)return kIOReturnBadArgument;
			if(m_scale.source.w!=width || m_scale.source.h!=height)m_gpu_invalidated=true;
			m_scale.source.w=width;m_scale.source.h=height;
			memcpy(&m_scale.buffer,&rgn->bounds,sizeof rgn->bounds);bFromGFB=false;
		}else if (m_wID != 1U ||
			checkOptionAC(VMW_OPTION_AC_PACKED_BACKING)) {
			m_scale.source.w = rgn->bounds.w;
			m_scale.source.h = rgn->bounds.h;
			m_scale.buffer.x = 0;
			m_scale.buffer.y = 0;
			m_scale.buffer.w = m_scale.source.w;
			m_scale.buffer.h = m_scale.source.h;
			bFromGFB = false;
		} else {
			memcpy(&m_scale.buffer, &rgn->bounds, sizeof rgn->bounds);
			m_scale.source.w = static_cast<int16_t>(m_screenInfo.w);
			m_scale.source.h = static_cast<int16_t>(m_screenInfo.h);
			bFromGFB = true;
		}
		calculateScaleParameters(bFromGFB);
	}
	videoReshape();
	/*
	 * TBD: this is ad-hoc code to prevent
	 *   a deadlock in the WindowServer
	 *   during Window Grab (OS 10.6).
	 *
	 *   Note: Window Grab works on OS 10.7, check what changed.
	 */
	if (!gpu_root && m_wID == 1U && options == 0x5U)
		bSkipWriteLockOnce = true;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark VMsvga22DContext Support Methods
#pragma mark -

HIDDEN
IOReturn CLASS::context_set_surface(uint32_t vmware_pixel_format, uint32_t apple_pixel_format)
{
	if (!m_provider) return kIOReturnOffline;
	ContentRead access(this,vmware_pixel_format || bVideoMode ||
		(apple_pixel_format==kIO32BGRAPixelFormat && m_surfaceFormat!=SVGA3D_A8R8G8B8));
	if(access.error)return access.error;
	SFLog(2, "%s[%#x](%#x, %#x)\n", __FUNCTION__, m_wID, vmware_pixel_format, apple_pixel_format);

	if (!vmware_pixel_format) {
		surface_video_off();
		bVideoMode = false;
		if (apple_pixel_format == kIO32BGRAPixelFormat) {
			m_surfaceFormat = SVGA3D_A8R8G8B8;
			m_bytes_per_pixel = sizeof(uint32_t);
			m_pixel_format = kIOAccelSurfaceModeColorDepth8888;
		}
		if (!haveFrontBuffer())
			Start3D();
		return kIOReturnSuccess;
	}
	if (checkOptionAC(VMW_OPTION_AC_NO_YUV))
		return kIOReturnUnsupported;
	bVideoMode = true;
	m_video.unit.enabled = 0;
	m_video.vmware_pixel_format = vmware_pixel_format;
	switch (vmware_pixel_format) {
		case VMWARE_FOURCC_UYVY:
			m_surfaceFormat = SVGA3D_UYVY;
			break;
		case VMWARE_FOURCC_YUY2:
			m_surfaceFormat = SVGA3D_YUY2;
			break;
	}
	m_bytes_per_pixel = sizeof(uint16_t);
	m_pixel_format = apple_pixel_format;
	Cleanup3D();		// Note: release the master surface so that a YUV surface doesn't thwart resolution changes
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::context_scale_surface(IOOptionBits options, uint32_t width, uint32_t height)
{
	ContentRead access(this);if(access.error)return access.error;
	if (!m_provider) return kIOReturnOffline;
	SFLog(2, "%s[%#x](%#x, %u, %u)\n", __FUNCTION__, m_wID, FMT_U(options), width, height);

	if (!(options & kIOBlitFixedSource))
		return kIOReturnSuccess;
	m_scale.source.w = static_cast<int16_t>(width);
	m_scale.source.h = static_cast<int16_t>(height);
	m_scale.buffer.x = 0;
	m_scale.buffer.y = 0;
	m_scale.buffer.w = m_scale.source.w;
	m_scale.buffer.h = m_scale.source.h;
	calculateScaleParameters();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::context_lock_memory(task_t context_owning_task, mach_vm_address_t* address, mach_vm_size_t* rowBytes,bool wait)
{
	if(m_gpu_primary){
		VMsvga2Surface* root=m_gpu_primary;root->retain();
		IOReturn error=root->context_lock_memory(context_owning_task,address,rowBytes,wait);
		root->release();return error;
	}
	// A 2D client can retain this object after its originating CGS client
	// closes. Cleanup has released backing and clientClose clears provider;
	// never try to allocate/map through that now-null provider.
	if (!m_provider) return kIOReturnOffline;
	SFLog(3, "%s[%#x](%p, %p, %p)\n", __FUNCTION__, m_wID, context_owning_task, address, rowBytes);

	if (!address || !rowBytes)
		return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	IOLockLock(m_content_lock);
	while(wait && !m_content_closing && (bIsLocked&(128U>>vmSurfaceLockContext))){
		// Do not deadlock a thread that already owns this surface through
		// another 2D client. Other threads sleep on the release event; there
		// is no timeout/retry polling and no host renderer lock is held here.
		if(m_context_owner==current_thread()){
			IOLockUnlock(m_content_lock);return kIOReturnCannotLock;
		}
		++m_context_waiters;
		int awakened=IOLockSleep(m_content_lock,const_cast<uint8_t*>(&bIsLocked),THREAD_ABORTSAFE);
		--m_context_waiters;
		if(awakened==THREAD_INTERRUPTED){
			IOLockUnlock(m_content_lock);return kIOReturnAborted;
		}
	}
	if(m_content_closing){IOLockUnlock(m_content_lock);return kIOReturnOffline;}
	bool already_context=OSTestAndSet(vmSurfaceLockContext,&bIsLocked);
	if(!already_context)m_context_owner=current_thread();
	IOReturn materialized=already_context?kIOReturnCannotLock:materializeGPU();
	if(!already_context && materialized){
		m_context_owner=0;OSTestAndClear(vmSurfaceLockContext,&bIsLocked);
		if(m_context_waiters)IOLockWakeup(m_content_lock,const_cast<uint8_t*>(&bIsLocked),false);
	}
	IOLockUnlock(m_content_lock);
	if(materialized)return materialized;
	if (!allocBacking() ||
		!mapBacking(context_owning_task, 1U)) {
		IOLockLock(m_content_lock);m_context_owner=0;
		OSTestAndClear(vmSurfaceLockContext, &bIsLocked);
		if(m_context_waiters)IOLockWakeup(m_content_lock,const_cast<uint8_t*>(&bIsLocked),false);
		IOLockUnlock(m_content_lock);
		return kIOReturnNoMemory;
	}
	if (bVideoMode &&
		(m_provider->HaveScreen() || !m_provider->Have3D()))
		setup_trick_buffer();	// Note: error ignored
	*address = m_backing.map[1]->getAddress();
	*rowBytes = m_scale.reserved[1];
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::context_lock_backing(task_t task, struct VIMSurfaceMapping* output,bool wait)
{
	if(m_gpu_primary){
		VMsvga2Surface* root=m_gpu_primary;root->retain();
		IOReturn error=root->context_lock_backing(task,output,wait);
		root->release();return error;
	}
	bzero(output, sizeof *output);
	mach_vm_address_t address = 0;
	mach_vm_size_t pitch = 0;
	IOReturn result = context_lock_memory(task, &address, &pitch,wait);
	if (result != kIOReturnSuccess)
		return result;
	// Geometry and backing are sampled under the same context lock. Bounds
	// describe the subrectangle in the source allocation, not the whole VRAM.
	// Surface 1 is WindowServer's display-sized shadow buffer. Its shape is a
	// changing drawing view, not the size/origin of this mapped allocation.
	// context_get_geometry supplies that distinct view to the GLI adapter.
	const bool primary = (m_wID == 1U);
	const uint64_t offset = (primary ? 0 : m_scale.reserved[2]) + (m_backing.vtb.md ? m_backing.offset : 0);
	const uint32_t width = static_cast<uint32_t>(primary ? m_scale.source.w : m_scale.buffer.w);
	const uint32_t height = static_cast<uint32_t>(primary ? m_scale.source.h : m_scale.buffer.h);
	const uint64_t length = m_backing.map[1]->getSize();
	const bool rgba16=m_cgl_pbuffer && m_surfaceFormat==SVGA3D_A16B16G16R16 && m_bytes_per_pixel==8;
	if ((m_bytes_per_pixel != 4 && !rgba16) || !width || !height || pitch < uint64_t(width)*m_bytes_per_pixel ||
		offset > length || uint64_t(height-1)*pitch + uint64_t(width)*m_bytes_per_pixel > length-offset) {
		context_unlock_memory(0);
		return kIOReturnUnsupported;
	}
	output->address = address;
	output->length = length;
	output->layout.version = VIM_FRAMEBUFFER_LAYOUT_VERSION;
	output->layout.width = width;
	output->layout.height = height;
	output->layout.row_bytes = static_cast<uint32_t>(pitch);
	output->layout.offset = static_cast<uint32_t>(offset);
	output->layout.bits_per_pixel = m_bytes_per_pixel*8U;
	return kIOReturnSuccess;
}

IOReturn CLASS::context_lock_gpu_backing(VIMSurfaceMapping* output)
{
    if(m_gpu_primary)return m_gpu_primary->context_lock_gpu_backing(output);
    if(!m_provider || !m_content_lock)return kIOReturnOffline;
    if(!output)return kIOReturnBadArgument;
    bzero(output,sizeof(*output));IOLockLock(m_content_lock);
    IOReturn error=kIOReturnSuccess;
    if(m_content_closing)error=kIOReturnOffline;
    else if(bIsLocked&(128U>>vmSurfaceLockContext))error=kIOReturnCannotLock;
    else if(!m_gpu_hold || m_native_write_active || m_content_writers ||
        (bIsLocked&(128U>>vmSurfaceLockWrite)))error=kIOReturnNotReady;
    else if(!bHaveID || !isBackingValid() || !isSourceValid() ||
        (!m_gpu_presentation && (m_scale.buffer.x || m_scale.buffer.y || m_scale.buffer.w!=m_identity_width ||
        m_scale.buffer.h!=m_identity_height)))error=kIOReturnUnsupported;
    if(!error) {
        OSTestAndSet(vmSurfaceLockContext,&bIsLocked);
        m_context_owner=current_thread();
        output->layout.version=VIM_FRAMEBUFFER_LAYOUT_VERSION;
        output->layout.width=m_identity_width;output->layout.height=m_identity_height;
        output->layout.row_bytes=m_identity_pitch;output->layout.bits_per_pixel=32;
        // address/length stay zero: this result must never be dereferenced.
    }
    IOLockUnlock(m_content_lock);return error;
}

HIDDEN
IOReturn CLASS::context_get_pixel_format(struct VIMSurfacePixelFormat* output)
{
    if(!output)return kIOReturnBadArgument;
    bzero(output,sizeof(*output));
    if(!bHaveID || !isSourceValid())return kIOReturnNotReady;
    if(m_cgl_pbuffer && m_surfaceFormat==SVGA3D_A16B16G16R16 && m_bytes_per_pixel==8)
        output->format=VIM_GL_PIXEL_RGBA16;
    else if(m_bytes_per_pixel==4 && (m_surfaceFormat==SVGA3D_X8R8G8B8 || m_surfaceFormat==SVGA3D_A8R8G8B8))
        output->format=VIM_GL_PIXEL_BGRA8;
    else return kIOReturnUnsupported;
    output->version=VIM_SURFACE_PIXEL_FORMAT_VERSION;output->bits_per_pixel=m_bytes_per_pixel*8U;
    return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::context_get_drawing_size(struct VIMSurfaceDrawingSize* output)
{
    if(!output)return kIOReturnBadArgument;
    bzero(output,sizeof(*output));
    // Physical Intel 10.8 set_shape_backing_length_ext preserves rect[0]'s
    // dimensions separately when a client backing has exactly one rectangle.
    // get_surface_size returns that logical extent, not the padded bounds.
    // Require the unambiguous zero-origin form; never infer a canvas from a
    // changing visibility union, nor modify the CPU allocation/scale state.
    if(!bHaveID || !isSourceValid() || !m_client_backing.addr || !m_last_region ||
       m_last_region->num_rects!=1 || m_wID==1U || gpuPresentationRoot() || bVideoMode)
        return kIOReturnUnsupported;
    int width=m_last_region->rect[0].w,height=m_last_region->rect[0].h;
    if(m_last_region->rect[0].x || m_last_region->rect[0].y || width<=0 || height<=0 ||
       width>m_scale.buffer.w || height>m_scale.buffer.h)
        return kIOReturnUnsupported;
    output->width=width;output->height=height;
    return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::context_get_geometry(struct VIMSurfaceGeometry* output)
{
	if (!output)
		return kIOReturnBadArgument;
	bzero(output, sizeof *output);
	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	output->version = VIM_SURFACE_GEOMETRY_VERSION;
	output->source_width = static_cast<uint32_t>(m_scale.source.w);
	output->source_height = static_cast<uint32_t>(m_scale.source.h);
	output->x = m_scale.buffer.x;
	output->y = m_scale.buffer.y;
	output->width = static_cast<uint32_t>(m_scale.buffer.w);
	output->height = static_cast<uint32_t>(m_scale.buffer.h);
	VMsvga2Surface* root=gpuPresentationRoot();
	if(root)output->reserved=root->m_wID;
	return kIOReturnSuccess;
}

IOReturn CLASS::context_get_vram(VIMSurfaceVRAM* output,bool dirty)
{
    if(m_gpu_primary)return m_gpu_primary->context_get_vram(output,dirty);
    if(!output || !m_content_lock || !m_provider)return kIOReturnBadArgument;
    bzero(output,sizeof(*output));IOLockLock(m_content_lock);
    IOReturn error=kIOReturnSuccess;
    if(!(bIsLocked&(128U>>vmSurfaceLockContext)) || m_native_write_active || m_content_writers ||
       (bIsLocked&(128U>>vmSurfaceLockWrite)))error=kIOReturnCannotLock;
    else if(!bHaveID || !isSourceValid() || !m_backing.self || m_backing.vtb.md ||
        m_client_backing.addr || m_gpu_hold || m_bytes_per_pixel!=4)error=kIOReturnUnsupported;
    else if(!m_scale.reserved[1] || uint64_t(m_scale.source.h)*m_scale.reserved[1]>m_backing.size)
        error=kIOReturnUnsupported;
    if(!error){
        output->version=VIM_SURFACE_VRAM_VERSION;
        output->width=m_scale.source.w;output->height=m_scale.source.h;output->row_bytes=m_scale.reserved[1];
        output->offset=m_backing.offset;output->length=m_backing.size;copyContentState(&output->state);
        if(dirty)error=m_provider->directVRAMDirty(output->offset,output->length);
    }
    IOLockUnlock(m_content_lock);return error;
}

IOReturn CLASS::context_get_ram(VIMSurfaceRAM* output,uint32_t first,bool dirty)
{
    if(m_gpu_primary)return m_gpu_primary->context_get_ram(output,first,dirty);
    if(!output || !m_content_lock || !m_provider)return kIOReturnBadArgument;
    bzero(output,sizeof(*output));IOLockLock(m_content_lock);IOReturn error=kIOReturnSuccess;
    IOMemoryDescriptor* md=m_backing.vtb.md;
    uint64_t length=md?md->getLength():0;
    // A client may wrap the entire framebuffer aperture even for a small
    // drawable. Export only the pages covering its validated active prefix,
    // not every page in the descriptor. The descriptor stays pinned as before.
    uint64_t active=uint64_t(m_scale.source.h)*m_scale.reserved[1]+m_backing.offset;
    uint64_t rounded=(active+4095)&~uint64_t(4095);
    if(!(bIsLocked&(128U>>vmSurfaceLockContext)) || m_native_write_active || m_content_writers ||
       (bIsLocked&(128U>>vmSurfaceLockWrite)))error=kIOReturnCannotLock;
    else if(!bHaveID || !isSourceValid() || !md || !m_prepared_backing || m_gpu_hold ||
        m_bytes_per_pixel!=4 || !length || !active || rounded>VIM_GL_SCATTER_MAX_BYTES ||
        m_backing.offset>length || uint64_t(m_scale.source.h)*m_scale.reserved[1]>length-m_backing.offset)
        error=kIOReturnUnsupported;
    if(!error){
        uint32_t total=static_cast<uint32_t>(rounded/4096);
        if(first!=UINT32_MAX && first>=total)error=kIOReturnBadArgument;
        // Geometry can change without replacing the underlying descriptor.
        if(!error && m_shared_ram_pages && m_shared_ram_page_count!=total){
            IOFree(m_shared_ram_pages,m_shared_ram_page_count*sizeof(uint64_t));
            m_shared_ram_pages=0;m_shared_ram_page_count=0;
        }
        if(!error && (first!=UINT32_MAX || dirty) && !m_shared_ram_pages){
            uint64_t* pages=static_cast<uint64_t*>(IOMalloc(total*sizeof(uint64_t)));
            if(!pages)error=kIOReturnNoMemory;
            else {
                for(uint32_t i=0;i<total;++i){
                    IOByteCount span=0;addr64_t address=VIMPhysicalSegment(md,uint64_t(i)*4096,&span);
                    if(!address || (address&4095) || !span){error=kIOReturnUnsupported;break;}
                    pages[i]=address;
                }
                if(error)IOFree(pages,total*sizeof(uint64_t));
                else {m_shared_ram_pages=pages;m_shared_ram_page_count=total;}
            }
        }
        if(!error){
            VIMSurfaceVRAM& b=output->backing;b.version=VIM_SURFACE_RAM_VERSION;
            b.width=m_scale.source.w;b.height=m_scale.source.h;b.row_bytes=m_scale.reserved[1];
            b.offset=m_backing.offset;b.length=rounded;copyContentState(&b.state);output->first=first;output->total=total;
            if(first!=UINT32_MAX){
                output->count=total-first<VIM_SURFACE_RAM_PAGE_CHUNK?total-first:VIM_SURFACE_RAM_PAGE_CHUNK;
                memcpy(output->pages,m_shared_ram_pages+first,output->count*sizeof(uint64_t));
            }
            if(dirty)error=m_provider->directRAMDirty(m_shared_ram_pages,m_shared_ram_page_count);
        }
    }
    IOLockUnlock(m_content_lock);return error;
}

IOReturn CLASS::context_copy_ram_owner(VMsvga2PreparedBacking** output)
{
    if(!output)return kIOReturnBadArgument;*output=0;
    if(m_gpu_primary)return m_gpu_primary->context_copy_ram_owner(output);
    if(!m_content_lock)return kIOReturnOffline;
    IOLockLock(m_content_lock);IOReturn error=kIOReturnSuccess;
    if(m_content_closing || !m_provider)error=kIOReturnOffline;
    else if(!(bIsLocked&(128U>>vmSurfaceLockContext)))error=kIOReturnNotOpen;
    else if(!m_prepared_backing || !m_backing.vtb.md ||
        m_prepared_backing->memory()!=m_backing.vtb.md || m_gpu_hold)error=kIOReturnUnsupported;
    if(!error){m_prepared_backing->retain();*output=m_prepared_backing;}
    IOLockUnlock(m_content_lock);return error;
}

IOReturn CLASS::context_retain_ram_backing(VIMSurfaceVRAM const* expected,VMsvga2PreparedBacking** output,
    VMsvga22DContext* endpoint)
{
    if(!output)return kIOReturnBadArgument;
    *output=0;
    if(!expected || !endpoint || expected->version!=VIM_SURFACE_RAM_VERSION)return kIOReturnBadArgument;
    if(m_gpu_primary)return m_gpu_primary->context_retain_ram_backing(expected,output,endpoint);
    if(!m_content_lock)return kIOReturnOffline;
    IOLockLock(m_content_lock);IOReturn error=kIOReturnSuccess;
    IOMemoryDescriptor* memory=m_backing.vtb.md;
    uint64_t active=uint64_t(m_scale.source.h)*m_scale.reserved[1]+m_backing.offset;
    uint64_t rounded=(active+4095)&~uint64_t(4095);
    if(m_content_closing || !m_provider)error=kIOReturnOffline;
    else if(!(bIsLocked&(128U>>vmSurfaceLockContext)) || m_context_owner!=current_thread())
        error=kIOReturnNotOpen;
    else if(m_active_backing_loan)error=kIOReturnBusy;
    else if(!bHaveID || m_wID==1U || bSkipWriteLockOnce || !isSourceValid() || !memory || !m_prepared_backing ||
        m_prepared_backing->memory()!=memory || m_gpu_hold || m_native_write_active ||
        m_content_writers || m_content_readers || (bIsLocked&((128U>>vmSurfaceLockWrite)|(128U>>vmSurfaceLockRead))) ||
        m_bytes_per_pixel!=4 || !active || rounded>VIM_GL_SCATTER_MAX_BYTES ||
        m_backing.offset>memory->getLength() ||
        uint64_t(m_scale.source.h)*m_scale.reserved[1]>memory->getLength()-m_backing.offset)
        error=kIOReturnUnsupported;
    if(!error){
        VIMSurfaceState state;copyContentState(&state);
        if(expected->width!=static_cast<uint32_t>(m_scale.source.w) ||
            expected->height!=static_cast<uint32_t>(m_scale.source.h) ||
            expected->row_bytes!=m_scale.reserved[1] || expected->offset!=m_backing.offset ||
            expected->length!=rounded || memcmp(&expected->state,&state,sizeof(state)))
            error=kIOReturnNotReady;
        else {
            // This is a pin/lifetime loan only. The caller MUST separately
            // retain CPU exclusion and drain every GPU use before release.
            m_prepared_backing->retain();*output=m_active_backing_loan=m_prepared_backing;
            endpoint->retain();m_backing_loan_endpoint=endpoint;
        }
    }
    IOLockUnlock(m_content_lock);return error;
}

void CLASS::context_cancel_ram_backing_loan(VMsvga2PreparedBacking* owner)
{
    if(m_gpu_primary){m_gpu_primary->context_cancel_ram_backing_loan(owner);return;}
    if(!m_content_lock || !owner)return;
    IOLockLock(m_content_lock);VMsvga22DContext* endpoint=0;
    if(m_active_backing_loan==owner){
        m_active_backing_loan=0;endpoint=m_backing_loan_endpoint;m_backing_loan_endpoint=0;
        IOLockWakeup(m_content_lock,&m_active_backing_loan,false);
    }
    IOLockUnlock(m_content_lock);
    if(endpoint)endpoint->release();
}
IOReturn CLASS::context_register_writer_backing(VMsvga2PreparedBacking* owner,OSObject* gate)
{
    if(!owner || !gate || !m_content_lock || m_gpu_primary)return kIOReturnUnsupported;
    IOLockLock(m_content_lock);IOReturn error=kIOReturnSuccess;
    if(m_content_closing || !m_provider)error=kIOReturnOffline;
    else if(m_active_backing_loan!=owner || m_writer_backing || m_future_backing.ticket ||
        !(bIsLocked&(128U>>vmSurfaceLockContext)))error=kIOReturnNotOpen;
    if(!error)m_writer_backing=gate;
    IOLockUnlock(m_content_lock);return error;
}
OSObject* CLASS::context_copy_writer_backing()
{
    if(!m_content_lock || m_gpu_primary)return 0;
    IOLockLock(m_content_lock);OSObject* gate=0;
    if(!m_content_closing && m_provider && m_active_backing_loan && m_writer_backing &&
       m_future_backing.ticket && !m_future_retiring && (bIsLocked&(128U>>vmSurfaceLockContext))){
        gate=m_writer_backing;gate->retain();
    }
    IOLockUnlock(m_content_lock);return gate;
}
IOReturn CLASS::context_set_writer_thread(VMsvga2PreparedBacking* owner,OSObject* gate,bool active)
{
    if(!m_content_lock || !owner || !gate)return kIOReturnBadArgument;
    IOLockLock(m_content_lock);IOReturn error=kIOReturnSuccess;
    if(m_active_backing_loan!=owner || m_writer_backing!=gate || !(bIsLocked&(128U>>vmSurfaceLockContext)))
        error=kIOReturnNotOpen;
    else if(active && (m_content_closing || !m_provider))error=kIOReturnOffline;
    else m_context_owner=active?current_thread():0;
    IOLockUnlock(m_content_lock);return error;
}
IOReturn CLASS::context_publish_writer_future(VMsvga2PreparedBacking* owner,OSObject* gate,uint64_t ticket,uint64_t epoch)
{
    if(!owner || !gate || !ticket || !epoch || !m_content_lock || m_gpu_primary)return kIOReturnBadArgument;
    IOLockLock(m_content_lock);IOReturn error=kIOReturnSuccess;
    if(m_content_closing || !m_provider)error=kIOReturnOffline;
    else if(m_active_backing_loan!=owner || m_writer_backing!=gate ||
        !(bIsLocked&(128U>>vmSurfaceLockContext)))error=kIOReturnNotOpen;
    else if(epoch<=m_content_epoch || (m_future_backing.ticket && epoch<=m_future_backing.state.content_epoch))
        error=kIOReturnNotReady;
    if(!error){
        copyContentState(&m_future_backing.state);m_future_backing.state.content_epoch=epoch;
        m_future_backing.state.flags=VIM_SURFACE_STATE_CONTEXT_LOCKED|VIM_SURFACE_STATE_GPU_NEWER|
            VIM_SURFACE_STATE_GPU_PENDING|VIM_SURFACE_STATE_GPU_FUTURE_SEALED;
        m_future_backing.ticket=ticket;m_context_owner=0;
    }
    IOLockUnlock(m_content_lock);return error;
}
IOReturn CLASS::context_mark_backing_pending(VMsvga2PreparedBacking* owner)
{
    if(m_gpu_primary)return m_gpu_primary->context_mark_backing_pending(owner);
    if(!owner || !m_content_lock)return kIOReturnBadArgument;
    IOLockLock(m_content_lock);
    bool valid=m_active_backing_loan==owner && (bIsLocked&(128U>>vmSurfaceLockContext));
    // The submitter may now legitimately wait through another CPU endpoint:
    // completion no longer needs that thread. Keep the context bit/pin held.
    if(valid)m_context_owner=0;
    IOLockUnlock(m_content_lock);return valid?kIOReturnSuccess:kIOReturnNotOpen;
}

IOReturn CLASS::context_mark_backing_future(VMsvga2PreparedBacking* owner,uint64_t ticket)
{
    if(m_gpu_primary)return m_gpu_primary->context_mark_backing_future(owner,ticket);
    if(!owner || !ticket || !m_content_lock)return kIOReturnBadArgument;
    IOLockLock(m_content_lock);IOReturn error=kIOReturnSuccess;
    if(m_content_closing || !m_provider)error=kIOReturnOffline;
    else if(m_active_backing_loan!=owner || !(bIsLocked&(128U>>vmSurfaceLockContext)) ||
        m_future_backing.ticket)error=kIOReturnNotOpen;
    else if(m_content_epoch==UINT64_MAX)error=kIOReturnNoResources;
    if(!error){
        copyContentState(&m_future_backing.state);++m_future_backing.state.content_epoch;
        m_future_backing.state.flags=VIM_SURFACE_STATE_CONTEXT_LOCKED|VIM_SURFACE_STATE_GPU_NEWER|
            VIM_SURFACE_STATE_GPU_PENDING|VIM_SURFACE_STATE_GPU_FUTURE_SEALED;
        m_future_backing.ticket=ticket;m_context_owner=0;
    }
    IOLockUnlock(m_content_lock);return error;
}
IOReturn CLASS::context_acquire_backing_future(VIMSurfaceMapping* mapping,VIMSurfaceFutureRead* future)
{
    if(m_gpu_primary)return m_gpu_primary->context_acquire_backing_future(mapping,future);
    if(!mapping || !future || !m_content_lock)return kIOReturnBadArgument;
    bzero(mapping,sizeof(*mapping));bzero(future,sizeof(*future));
    IOLockLock(m_content_lock);IOReturn error=kIOReturnSuccess;
    if(m_content_closing || !m_provider)error=kIOReturnOffline;
    else if(m_future_retiring || !m_active_backing_loan || !m_future_backing.ticket ||
        !(bIsLocked&(128U>>vmSurfaceLockContext)))error=kIOReturnNotReady;
    else if(m_future_readers>=256)error=kIOReturnNoResources;
    if(!error){
        ++m_future_readers;*future=m_future_backing;
        mapping->layout.version=VIM_FRAMEBUFFER_LAYOUT_VERSION;
        mapping->layout.width=m_identity_width;mapping->layout.height=m_identity_height;
        mapping->layout.row_bytes=m_identity_pitch;mapping->layout.bits_per_pixel=32;
        // No CPU pointer, length, or backing offset is exposed.
    }
    IOLockUnlock(m_content_lock);return error;
}
void CLASS::context_release_backing_future()
{
    if(m_gpu_primary){m_gpu_primary->context_release_backing_future();return;}
    IOLockLock(m_content_lock);
    if(m_future_readers && !--m_future_readers)IOLockWakeup(m_content_lock,&m_future_readers,false);
    IOLockUnlock(m_content_lock);
}

HIDDEN
IOReturn CLASS::context_get_clip(struct VIMSurfaceClip* output)
{
	if (!output)
		return kIOReturnBadArgument;
	bzero(output, sizeof *output);
	if (!m_last_shape)
		return kIOReturnNotReady;
	// LockSurfaceBacking maps the primary allocation from zero; other CGS
	// drawables map the subrectangle starting at buffer.x/y.
	if (!VIMSurfaceClipFromRegion(m_last_region,m_last_shape->getLength(),
		m_wID == 1U || m_gpu_primary ? m_scale.buffer.x : 0,
		m_wID == 1U || m_gpu_primary ? m_scale.buffer.y : 0,output))
		return kIOReturnNotReady;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::context_get_state(VIMSurfaceState* output)
{
	if(m_gpu_primary)return m_gpu_primary->context_get_state(output);
	if (!m_provider) return kIOReturnOffline;
	if (!output || !m_content_lock) return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid() || !isBackingValid() ||
		!(bIsLocked & (128U>>vmSurfaceLockContext))) return kIOReturnNotOpen;
	IOLockLock(m_content_lock);copyContentState(output);IOLockUnlock(m_content_lock);
	return output->object ? kIOReturnSuccess : kIOReturnNotReady;
}

/* Initial CPU handoff endpoint. This does not mark a surface GPU-newer or
 * waive existing native locking. In particular, client-owned shadow memory
 * is deliberately ineligible until its external CPU access is mediated. */
IOReturn CLASS::context_materialize_gpu(VIMSurfaceMaterialize const* request,VIMSurfaceState* output)
{
    if(m_gpu_primary)return m_gpu_primary->context_materialize_gpu(request,output);
    if(!m_provider || !m_content_lock)return kIOReturnOffline;
    if(!request || !output || !request->gpu_epoch)return kIOReturnBadArgument;
    IOLockLock(m_content_lock);
    VIMSurfaceState state;copyContentState(&state);
    IOReturn error=kIOReturnSuccess;
    if(!(state.flags&VIM_SURFACE_STATE_CONTEXT_LOCKED))error=kIOReturnNotOpen;
    else if((state.flags&(VIM_SURFACE_STATE_CPU_WRITER|VIM_SURFACE_STATE_GPU_NEWER)) ||
        m_content_readers || (bIsLocked&(128U>>vmSurfaceLockRead)))error=kIOReturnCannotLock;
    else if(request->object!=state.object || request->incarnation!=state.incarnation || request->cpu_epoch!=state.content_epoch)error=kIOReturnNotReady;
    else if(isClientBackingValid() || !isBackingValid() || m_bytes_per_pixel!=4 ||
            m_identity_width>8192 || m_identity_height>8192)error=kIOReturnUnsupported;
    uint32_t hold=0;
    if(!error)error=m_provider->holdGPUSurface(request->namespace_id,state.object,state.incarnation,
        m_identity_width,m_identity_height,&hold);
    IOMemoryDescriptor* memory=0;bool temporary=false,prepared=false;
    if(!error) {
        memory=m_backing.vtb.md;
        if(!memory && m_backing.self) {
            memory=IOMemoryDescriptor::withAddress(m_backing.self,m_backing.size,kIODirectionInOut);
            temporary=true;
            if(memory) { error=memory->prepare();prepared=!error; }
        }
        if(!memory)error=kIOReturnNoMemory;
    }
    if(!error) {
        error=m_provider->readGPUSurface(hold,request->gpu_epoch,0,0,m_identity_width,m_identity_height,
            memory,m_backing.vtb.md?m_backing.offset:0,m_identity_pitch);
        // A failed transfer can have changed a prefix. Invalidate the old
        // CPU epoch even on failure; never claim it still names old pixels.
        ++m_content_epoch;
    }
    if(temporary && memory) { if(prepared)memory->complete();memory->release(); }
    if(hold) { IOReturn released=m_provider->releaseGPUSurface(hold);if(!error)error=released; }
    if(!error)copyContentState(output);
    IOLockUnlock(m_content_lock);return error;
}

IOReturn CLASS::context_defer_gpu(VIMSurfaceMaterialize const* request,VIMSurfaceState* output)
{
    if(m_gpu_primary)return m_gpu_primary->context_defer_gpu(request,output);
    if(!m_provider || !m_content_lock)return kIOReturnOffline;
    if(!request || !output || !request->gpu_epoch)return kIOReturnBadArgument;
    IOLockLock(m_content_lock);
    VIMSurfaceState state;copyContentState(&state);
    IOReturn error=kIOReturnSuccess;
    if(!(state.flags&VIM_SURFACE_STATE_CONTEXT_LOCKED))error=kIOReturnNotOpen;
    else if((state.flags&VIM_SURFACE_STATE_CPU_WRITER) || m_content_readers ||
        (bIsLocked&(128U>>vmSurfaceLockRead)))error=kIOReturnCannotLock;
    else if(request->object!=state.object || request->incarnation!=state.incarnation ||
        request->cpu_epoch!=state.content_epoch || request->gpu_epoch<=state.content_epoch)error=kIOReturnNotReady;
    // Client-owned primary memory has CPU access outside the driver's lock
    // contract. Do not defer it until that separate contract is implemented.
    else if(m_provider->Have3D() || m_provider->HaveScreen() || (m_wID==1 && !m_gpu_presentation) || bVideoMode || isClientBackingValid() || !isBackingValid() ||
        m_bytes_per_pixel!=4 || !m_identity_width || !m_identity_height ||
        m_identity_width>8192 || m_identity_height>8192)error=kIOReturnUnsupported;
    uint32_t hold=0;
    if(!error)error=m_provider->holdGPUSurface(request->namespace_id,state.object,state.incarnation,
        m_identity_width,m_identity_height,&hold,request->gpu_epoch);
    if(!error) {
        // Revoke the publishing client's CPU mapping before RAM becomes stale.
        // It must unlock read-only; a later CPU acquire materializes the version.
        releaseBackingMap(1U);if(m_gpu_presentation){releaseBackingMap(0U);m_gpu_service_ready=true;}
        discardGPU();m_gpu_hold=hold;
        m_gpu_epoch=m_content_epoch=request->gpu_epoch;copyContentState(output);
    }
    IOLockUnlock(m_content_lock);return error;
}

HIDDEN
IOReturn CLASS::context_unlock_memory(uint32_t* swapFlags,bool read_only,VIMSurfaceState* state,
    VMsvga2PreparedBacking* completed_loan,uint64_t writer_epoch,OSObject* writer_gate)
{
	if(m_gpu_primary)return m_gpu_primary->context_unlock_memory(swapFlags,read_only,state,completed_loan,writer_epoch,writer_gate);
	SFLog(3, "%s[%#x]()\n", __FUNCTION__, m_wID);

	if (swapFlags)
		*swapFlags = 0;
	IOLockLock(m_content_lock);
	if((writer_epoch || writer_gate) && (!writer_epoch || !writer_gate || !completed_loan ||
	   m_writer_backing!=writer_gate || writer_epoch<m_content_epoch ||
	   (m_future_backing.ticket && writer_epoch<m_future_backing.state.content_epoch))){
		IOLockUnlock(m_content_lock);return kIOReturnNotPermitted;
	}
	if(m_writer_backing && !writer_gate){IOLockUnlock(m_content_lock);return kIOReturnBusy;}
	if((m_active_backing_loan && m_active_backing_loan!=completed_loan) ||
	   (completed_loan && m_active_backing_loan!=completed_loan)){
		IOLockUnlock(m_content_lock);return kIOReturnBusy;
	}
	// Completion can race GPU-reader acquisition. Keep its ticket and source
	// exclusion alive until the last reader has queued its immutable GPU copy.
	// No transport lock is held; reader close also returns this reservation.
	if(completed_loan){
		m_future_retiring=true;
		while(m_future_readers)IOLockSleep(m_content_lock,&m_future_readers,THREAD_UNINT);
		bzero(&m_future_backing,sizeof(m_future_backing));
		m_writer_backing=0;
		m_future_retiring=false;
	}
	IOReturn materialized=read_only?kIOReturnSuccess:materializeGPU();
	IOLockUnlock(m_content_lock);
	if(materialized)return materialized;
	// Keep the context bit until its mapping has gone away: another client
	// must not acquire it and then have its mapping released underneath it.
	releaseBackingMap(1U);
	IOLockLock(m_content_lock);
	if(writer_epoch)m_content_epoch=writer_epoch;
	else if (!read_only) ++m_content_epoch; // legacy unlock conservatively means written
	if (state) { copyContentState(state);state->flags&=~VIM_SURFACE_STATE_CONTEXT_LOCKED; }
	m_context_owner=0;
	VMsvga22DContext* endpoint=0;
	if(completed_loan){
		m_active_backing_loan=0;endpoint=m_backing_loan_endpoint;m_backing_loan_endpoint=0;
		IOLockWakeup(m_content_lock,&m_active_backing_loan,false);
	}
	bool closing=m_content_closing;
	OSTestAndClear(vmSurfaceLockContext, &bIsLocked);
	if(m_context_waiters)IOLockWakeup(m_content_lock,const_cast<uint8_t*>(&bIsLocked),false);
	IOLockUnlock(m_content_lock);
	if(endpoint)endpoint->release();
	if(closing)return kIOReturnOffline;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
	// Snow's compositor flushes its GL primary without issuing the later
	// IOAccelFlushSurfaceOnFramebuffers handoff. A physical GL driver draws
	// directly into the front buffer; our paired primary is a shadow. Publish
	// its completed GL write through the existing bounded framebuffer blitter
	// before acknowledging that write. Read loans and offscreen surfaces must
	// never present, and newer GPU-primary/companion ownership is unchanged.
	if(!read_only && m_wID==1U && !m_gpu_presentation && !m_gpu_primary &&
		m_provider && !m_provider->Have3D() && !m_provider->HaveScreen() &&
		m_provider->getProperty("VIMDirectGLCompositor"))
		return surface_flush(1U<<m_framebufferIndex,0);
#endif
	return kIOReturnSuccess;
}

/* Native MPHWFillRegion uses the solid blitter while resizing a CGS backing.
 * On actual 13F1911 NVIDIA, the CGS operation masks the existing RGB bits
 * and clears alpha: ff123456 on ff3366cc becomes 00122444, ffffffff keeps
 * RGB and clears alpha, and ff000000 clears all channels. This is neither
 * literal ARGB storage nor inversion. Publish the CPU write epoch so live GL
 * imports can observe it. Coordinates are buffer-local, as for the existing
 * surface copy paths; obtainKernelPtrs already applies the buffer offset. */
HIDDEN
IOReturn CLASS::fill_rectangles(uint32_t color,IOBlitRectangle const* rects,size_t rectsSize)
{
	if (!m_provider) return kIOReturnOffline;
	if (!rects || !rectsSize || rectsSize%sizeof(*rects)) return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid()) return kIOReturnNotReady;
	if (bVideoMode || m_bytes_per_pixel!=4 ||
		(m_surfaceFormat!=SVGA3D_X8R8G8B8 && m_surfaceFormat!=SVGA3D_A8R8G8B8)) return kIOReturnUnsupportedMode;
	const int width=m_scale.buffer.w,height=m_scale.buffer.h;
	const uint64_t pitch=m_scale.reserved[1];
	if (width<0 || height<0 || m_scale.buffer.x<0 || m_scale.buffer.y<0 ||
		static_cast<int64_t>(m_scale.buffer.x)+width>m_scale.source.w ||
		static_cast<int64_t>(m_scale.buffer.y)+height>m_scale.source.h ||
		pitch<static_cast<uint64_t>(m_scale.source.w)*4 || (pitch&3)) return kIOReturnUnsupportedMode;
	const size_t count=rectsSize/sizeof(*rects);
	bool touched=false;
	// Validate the complete request before any writes. Signed coordinates may
	// be clipped, but negative sizes and truncated records are not rectangles.
	for (size_t i=0;i<count;++i) {
		if (rects[i].width<0 || rects[i].height<0) return kIOReturnBadArgument;
		if (rects[i].width && rects[i].height && rects[i].x<width && rects[i].y<height &&
			static_cast<int64_t>(rects[i].x)+rects[i].width>0 &&
			static_cast<int64_t>(rects[i].y)+rects[i].height>0) touched=true;
	}
	if (!touched) return kIOReturnSuccess;
	if (!allocBacking()) return kIOReturnNoMemory;
	const uint64_t view_offset=static_cast<uint64_t>(m_scale.buffer.y)*pitch+static_cast<uint64_t>(m_scale.buffer.x)*4;
	VMsvga2Surface* owner=m_gpu_primary?m_gpu_primary:this;
	const uint64_t allocation_offset=view_offset+(owner->m_backing.vtb.md?owner->m_backing.offset:0);
	const uint64_t allocation_size=owner->m_backing.vtb.md?owner->m_backing.vtb.md->getLength():owner->m_backing.size;
	if (view_offset!=m_scale.reserved[2] || allocation_offset>allocation_size ||
		(static_cast<uint64_t>(height)-1)*pitch+static_cast<uint64_t>(width)*4>allocation_size-allocation_offset)
		return kIOReturnNotWritable;
	IOVirtualAddress base;vm_size_t limit;IOMemoryMap* holder=0;
	IOReturn rc=obtainKernelPtrs(&base,&limit,&holder);
	if (rc!=kIOReturnSuccess) return rc;
	if (width<=0 || height<=0 || pitch<static_cast<uint64_t>(width)*4 ||
		(static_cast<uint64_t>(height)-1)*pitch+static_cast<uint64_t>(width)*4>limit) {
		if (holder) holder->release();return kIOReturnNotWritable;
	}
	{
		ContentWrite writing(this);
		if(writing.error) { if(holder)holder->release();return writing.error; }
		const uint32_t mask=color&0x00ffffffU;
		for (size_t i=0;i<count;++i) {
			int64_t left=rects[i].x,top=rects[i].y;
			int64_t right=left+rects[i].width,bottom=top+rects[i].height;
			if (left<0) left=0;if (top<0) top=0;
			if (right>width) right=width;if (bottom>height) bottom=height;
			if (right<=left || bottom<=top) continue;
			for (int64_t y=top;y<bottom;++y) {
				uint32_t* row=reinterpret_cast<uint32_t*>(base+static_cast<uint64_t>(y)*pitch+left*4);
				if (!mask) memset32(row,0,static_cast<size_t>(right-left));
				else for (int64_t x=left;x<right;++x) *row++&=mask;
			}
		}
	}
	if (holder) holder->release();return kIOReturnSuccess;
}

/*
 * This routine blits a region from the master surface to a CGS surface.
 *   The WindowServer calls this function while holding a write-lock on the surface.
 *   It expects to be able to combine the result of the blit with other graphics
 *   rendered into the surface and then flush it to screen.  As a result, we cannot
 *   fully perform the blit in host VRAM.  We must copy the data into guest memory or
 *   the WindowServer won't be able to combine it.
 *   In principle, if the WindowServer didn't expect to be able to combine, it would
 *   be possible to perform the whole blit in host VRAM only, which would make it
 *   more efficient.
 */
HIDDEN
IOReturn CLASS::copy_framebuffer_region_to_self(uint32_t framebufferIndex,
												int destX,
												int destY,
												IOAccelDeviceRegion const* region,
												size_t regionSize)
{
	if (!m_provider) return kIOReturnOffline;
	IOReturn rc;
	VMsvga2Accel::ExtraInfo extra;

	SFLog(3, "%s[%#x](%u, %d, %d, %p, %lu)\n", __FUNCTION__, m_wID, framebufferIndex, destX, destY, region, regionSize);

	if (!region || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(region))
		return kIOReturnBadArgument;

	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	VMsvga2Surface* root=gpuPresentationRoot();
	/* A framebuffer SOURCE does not depend on the destination belonging to
	 * its display family. An ordinary window/screenshot destination must read
	 * the same canonical pixels, not the obsolete software GFB. Hold the
	 * provider-discovered primary through the entire transaction; the native
	 * primary ID is established by set_id_mode, not a guest address/CGS window.
	 */
	struct PrimaryReference {
		VMsvga2Surface* surface;
		PrimaryReference():surface(0) {}
		~PrimaryReference(){if(surface)surface->release();}
	} primary;
	if(!root && m_provider->getProperty("VIMDirectGLGPUPresentation")){
		primary.surface=m_provider->findSurfaceForID(1U);
		if(primary.surface){primary.surface->retain();root=primary.surface->gpuPresentationRoot();}
	}
	if(root && root->m_gpu_service_ready){
		if(framebufferIndex!=root->m_framebufferIndex)return kIOReturnBadArgument;
		/* The native display is now the GPU family, not QEMU's old GFB.
		 * A caller without the paired GLI pre-copy route still gets correct
		 * pixels. Materialize once, snapshot the sources before any overlap,
		 * and use the same native view-relative destination coordinates.
		 * This deliberately slower CPU recovery is not the normal GPU path. */
		if(region->num_rects>VIM_SURFACE_CLIP_RECTS || regionSize>4096U ||
		   m_bytes_per_pixel!=4 || root->m_bytes_per_pixel!=4)return kIOReturnUnsupported;
		if(!allocBacking())return kIOReturnNoMemory;
		/* For an external destination, its ContentWrite below does not acquire
		 * or materialize the source. Retain a coherent source read for the
		 * snapshot. The same-family case is already acquired by ContentWrite;
		 * acquiring it twice would weaken the native lock contract. */
		ContentRead reading(root,root!=gpuPresentationRoot());
		if(reading.error)return reading.error;
		ContentWrite writing(this);if(writing.error)return writing.error;
		IOVirtualAddress src=0,dst=0;vm_size_t src_limit=0,dst_limit=0;
		IOMemoryMap *src_holder=0,*dst_holder=0;
		rc=root->obtainKernelPtrs(&src,&src_limit,&src_holder);if(rc)return rc;
		const uint64_t pitch=root->m_scale.reserved[1],length=pitch*root->m_scale.source.h;
		const uint64_t source_offset=root->m_scale.reserved[2];
		src-=source_offset;src_limit+=source_offset;
		if(!length || length>src_limit || length>UINT32_MAX){
			if(src_holder)src_holder->release();return kIOReturnOverrun;
		}
		void* snapshot=IOMalloc(static_cast<vm_size_t>(length));
		if(!snapshot){if(src_holder)src_holder->release();return kIOReturnNoMemory;}
		memcpy(snapshot,reinterpret_cast<void const*>(src),static_cast<size_t>(length));
		if(src_holder)src_holder->release();
		rc=obtainKernelPtrs(&dst,&dst_limit,&dst_holder);
		if(!rc){
			SVGAGuestImage source_image,destination_image;SVGASignedPoint source_delta,destination_delta;
			bzero(&source_image,sizeof source_image);bzero(&destination_image,sizeof destination_image);
			bzero(&source_delta,sizeof source_delta);
			source_image.pitch=static_cast<uint32_t>(pitch);source_image.ptr.offset=static_cast<uint32_t>(length);
			destination_image.pitch=m_scale.reserved[1];destination_image.ptr.offset=static_cast<uint32_t>(dst_limit);
			destination_delta.x=destX-region->bounds.x;destination_delta.y=destY-region->bounds.y;
			uint64_t single_storage[(sizeof(IOAccelDeviceRegion)+sizeof(IOAccelBounds)+7)/8];
			IOAccelDeviceRegion* clipped=const_cast<IOAccelDeviceRegion*>(region);
			if(!clipped->num_rects){
				clipped=reinterpret_cast<IOAccelDeviceRegion*>(single_storage);
				clipped->num_rects=1;clipped->bounds=region->bounds;clipped->rect[0]=region->bounds;
			}
			clipRegionToBuffer(clipped,destination_delta.x,destination_delta.y);
			rc=m_provider->genericBlitCopy(dst,&destination_image,&destination_delta,
				reinterpret_cast<IOVirtualAddress>(snapshot),&source_image,&source_delta,clipped,4);
		}
		if(dst_holder)dst_holder->release();IOFree(snapshot,static_cast<vm_size_t>(length));return rc;
	}
	if (bVideoMode) {
		SFLog(1, "%s[%#x]: called for YUV surface - unsupported\n", __FUNCTION__, m_wID);
		return kIOReturnUnsupported;
	}
	if (m_surfaceFormat != SVGA3D_X8R8G8B8) {
		SFLog(1, "%s[%#x]: called for surface format %d - unsupported\n", __FUNCTION__, m_wID, m_surfaceFormat);
		return kIOReturnUnsupportedMode;
	}
	/*
	 * TBD: if the surface is already locked and the source
	 *   has grown, the following line may release existing
	 *   maps, without any way to notify the client.
	 *   [it is probably incorrect logic for the client to
	 *    grow the source while holding a lock on the surface.
	 *    maybe the code should check this and return an error
	 *    if an attempt is made to rescale while the surface
	 *    is locked.]
	 */
	if (!allocBacking())
		return kIOReturnNoMemory;
	bzero(&extra, sizeof extra);
	ContentWrite writing(this);
	if(writing.error)return writing.error;
	extra.mem_gmr_id = m_backing.vtb.gmr_id;
	extra.mem_offset_in_gmr = m_backing.offset + m_scale.reserved[2];
	extra.mem_pitch = m_scale.reserved[1];
	extra.dstDeltaX = destX - region->bounds.x;
	extra.dstDeltaY = destY - region->bounds.y;
	clipRegionToBuffer(const_cast<IOAccelDeviceRegion*>(region),
					   extra.dstDeltaX,
					   extra.dstDeltaY);
	if (m_provider->isPrimaryScreenActive()) {
		rc = m_provider->blitFromScreen(framebufferIndex,
										region,
										&extra,
										&m_backing.vtb.fence);
	} else if (bHaveMasterSurface) {
#if 0
		/*
		 * Note: This does not work if offset comes out negative.
		 */
		extra.mem_offset_in_bar1 +=
			extra.dstDeltaY * static_cast<int>(extra.mem_pitch) +
			extra.dstDeltaX * static_cast<int>(m_bytes_per_pixel);
		extra.dstDeltaX = 0;
		extra.dstDeltaY = 0;
#else
		/*
		 * TBD: In Workstation 6.5, the coordinates are reversed,
		 *   so it's no longer supported.
		 */
		extra.srcDeltaX = extra.dstDeltaX;
		extra.srcDeltaY = extra.dstDeltaY;
		extra.dstDeltaX = 0;
		extra.dstDeltaY = 0;
#endif
		rc = m_provider->surfaceDMA2D(m_provider->getMasterSurfaceID(),
									  SVGA3D_READ_HOST_VRAM,
									  region,
									  &extra,
									  &m_backing.vtb.fence);
	} else {
		IOVirtualAddress base;
		vm_size_t limit_from_base;
		IOMemoryMap* holder;
		rc = obtainKernelPtrs(&base, &limit_from_base, &holder);
		if (rc != kIOReturnSuccess)
			return rc;
		extra.mem_offset_in_gmr = 0U;
		rc = m_provider->blitGFB(framebufferIndex,
								 region,
								 &extra,
								 base,
								 limit_from_base,
								 0);
		if (holder)
			holder->release();
#if LOGGING_LEVEL >= 1
		if (rc != kIOReturnSuccess)
			SFLog(1, "%s[%#x]: blitGFB failed, error == %#x\n", __FUNCTION__, m_wID, rc);
#endif
		return rc;
	}
	if (rc != kIOReturnSuccess)
		return kIOReturnNotReadable;
	m_backing.vtb.sync(m_provider);		// Note: regrettable but necessary - even RingDoorBell didn't do the job when swinging windows around
	return kIOReturnSuccess;
}

/*
 * surface-to-framebuffer is not supported yet.  It's not difficult to do, but the
 *   WindowsServer blits by using surface_flush() and QuickTime blits with SwapSurface,
 *   so it's not really necessary.
 */
HIDDEN
IOReturn CLASS::copy_self_region_to_framebuffer(uint32_t framebufferIndex,
												int destX,
												int destY,
												IOAccelDeviceRegion const* region,
												size_t regionSize)
{
	SFLog(1, "%s: Copy from surface(%#x) to framebuffer(%u) - unsupported\n", __FUNCTION__,
		  m_wID, framebufferIndex);
	if (!region || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(region))
		return kIOReturnBadArgument;
	return kIOReturnUnsupported;
}

HIDDEN
IOReturn CLASS::copy_surface_region_to_self(class VMsvga2Surface* source_surface,
											int destX,
											int destY,
											IOAccelDeviceRegion const* region,
											size_t regionSize)
{
	if (!m_provider || (source_surface && !source_surface->m_provider)) return kIOReturnOffline;
	IOReturn rc;
	IOAccelDeviceRegion* rgn;
	IOVirtualAddress dst_base, src_base;
	vm_size_t dst_limit, src_limit;
	SVGAGuestImage dst_image, src_image;
	SVGASignedPoint dst_delta, src_delta;
	IOMemoryMap* holders[2];

	if (!source_surface)
		return kIOReturnBadArgument;

	SFLog(3, "%s[%#x](%#x, %d, %d, %p, %lu)\n", __FUNCTION__, m_wID, source_surface->m_wID, destX, destY, region, regionSize);

	if (!region || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(region))
		return kIOReturnBadArgument;
	if (!bHaveID || !isSourceValid())
		return kIOReturnNotReady;
	if (!source_surface->bHaveID || !source_surface->isBackingValid())
		return kIOReturnNotReady;
	if (m_surfaceFormat != source_surface->m_surfaceFormat ||
		m_bytes_per_pixel != source_surface->m_bytes_per_pixel) {
		SFLog(1, "%s: called for source surface[%#x] format %d, dest surface[%#x] format %d - unsupported\n",
			  __FUNCTION__, source_surface->m_wID, source_surface->m_surfaceFormat, m_wID, m_surfaceFormat);
		return kIOReturnUnsupported;
	}
	/*
	 * TBD: see comment in copy_framebuffer_region_to_self()
	 *   about growing the source while locked.
	 */
	if (!allocBacking())
		return kIOReturnNoMemory;
	dst_delta.x = destX - region->bounds.x;
	ContentWrite writing(this);
	if(writing.error)return writing.error;
	ContentRead reading(source_surface);
	if(reading.error)return reading.error;
	dst_delta.y = destY - region->bounds.y;
	bzero(&src_delta, sizeof src_delta);
	rgn = const_cast<IOAccelDeviceRegion*>(region);
	clipRegionToBuffer(rgn,
					   dst_delta.x,
					   dst_delta.y);
	dst_image.pitch = m_scale.reserved[1];
	src_image.pitch = source_surface->m_scale.reserved[1];
	rc = obtainKernelPtrs(&dst_base, &dst_limit, &holders[0]);
	if (rc != kIOReturnSuccess)
		return rc;
	rc = source_surface->obtainKernelPtrs(&src_base, &src_limit, &holders[1]);
	if (rc != kIOReturnSuccess) {
		if (holders[0])
			holders[0]->release();
		return rc;
	}
	dst_image.ptr.offset = static_cast<uint32_t>(dst_limit);
	src_image.ptr.offset = static_cast<uint32_t>(src_limit);
	if (!rgn->num_rects) {
		/*
		 * Dumbass WindowServer...
		 */
		memcpy(&rgn->rect[0], &rgn->bounds, sizeof rgn->bounds);
		++rgn->num_rects;
	}
	rc = m_provider->genericBlitCopy(dst_base,
									 &dst_image,
									 &dst_delta,
									 src_base,
									 &src_image,
									 &src_delta,
									 region,
									 static_cast<uint8_t>(m_bytes_per_pixel));
	if (holders[1])
		holders[1]->release();
	if (holders[0])
		holders[0]->release();
#if LOGGING_LEVEL >= 1
	if (rc != kIOReturnSuccess)
		SFLog(1, "%s[%#x]: genericBlitCopy failed, error == %#x\n", __FUNCTION__, m_wID, rc);
#endif
	return rc;
}

HIDDEN
IOReturn CLASS::surface_video_off()
{
	if (!bVideoMode || !m_video.unit.enabled)
		return kIOReturnSuccess;
	m_video.unit.enabled = 0;
	m_provider->VideoSetReg(m_video.stream_id, SVGA_VIDEO_ENABLED, 0);
	m_provider->FreeStreamID(m_video.stream_id);
	m_video.stream_id = SVGA_ID_INVALID;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surface_flush_video(uint32_t* swapFlags)
{
	SFLog(3, "%s[%#x]()\n", __FUNCTION__, m_wID);

	if (swapFlags)
		*swapFlags = 0;			// Note: setting *swapflags = 2 tells the client to flush the CGS surface after the swap
	if (!bHaveID || !m_last_region || !isBackingValid())
		return kIOReturnNotReady;
	if (!bVideoMode) {
		SFLog(1, "%s[%#x]: called for non-YUV surface - unsupported\n", __FUNCTION__, m_wID);
		return kIOReturnUnsupported;
	}
	/*
	 * Note: we don't SyncFIFO right after a flush, which means the backing may be reused
	 *   while there are still outgoing video flushes in the FIFO.  This may cause some tearing.
	 */
	if (m_video.unit.enabled)
		return m_provider->VideoSetRegsInRange(m_video.stream_id, 0, 0U, 0U, &m_backing.vtb.fence);
	m_video.stream_id = m_provider->AllocStreamID();
	if (!isIdValid(m_video.stream_id))
		return kIOReturnNoResources;
	setVideoRegs();
	setVideoDest();
	m_video.unit.enabled = 1U;
#if LOGGING_LEVEL >= 1
	if (m_log_level >= 3)
		for (size_t i = 0U; i < SVGA_VIDEO_NUM_REGS; ++i)
			SFLog(3, "%s:   reg[%lu] == %#x\n", __FUNCTION__, i, reinterpret_cast<uint32_t const*>(&m_video.unit)[i]);
#endif
	return m_provider->VideoSetRegsInRange(m_video.stream_id, &m_video.unit, SVGA_VIDEO_ENABLED, SVGA_VIDEO_DST_SCREEN_ID, &m_backing.vtb.fence);
}
