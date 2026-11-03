/*
 *  VMsvga2Accel.cpp
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

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/version.h>
#include <uuid/uuid.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
#include "IOSurfaceRoot.h"
#endif
#include "vmw_options_ac.h"
#include "VLog.h"
#include "VMsvga2Accel.h"
#include "VMsvga2Surface.h"
#include "VMsvga2GLContext.h"
#include "VMsvga22DContext.h"
#include "VMsvga2DVDContext.h"
#include "VMsvga2Device.h"
#include "VMsvga2OCDContext.h"
#include "VMsvga2Allocator.h"
#include "VMsvga2.h"
#include "PhysicalSegment.h"
#include "../vminclude/vim_passthrough_opengl.h"
#include "../vminclude/vim_gl_surface_service.h"
#include "../vminclude/vim_backing_notify_kernel.h"

#define CLASS VMsvga2Accel
#define super IOAccelerator
OSDefineMetaClassAndStructors(VMsvga2Accel, IOAccelerator);

#define HIDDEN __attribute__((visibility("hidden")))

static __attribute__((used)) char const copyright[] = "Copyright 2009-2012 Zenith432";

HIDDEN
unsigned vmw_options_ac = 0U;

#if LOGGING_LEVEL >= 1
#define ACLog(log_level, ...) do { if (log_level <= m_log_level_ac) VLog("IOAC: ", ##__VA_ARGS__); } while(false)
#else
#define ACLog(log_level, ...)
#endif

#define FMT_D(x) static_cast<int>(x)
#define FMT_U(x) static_cast<unsigned>(x)
#define FMT_LU(x) static_cast<size_t>(x)

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
int log_2_32(uint32_t x)
{
	int y = 0;

	if (x & 0xFFFF0000U) { y |= 16; x >>= 16; }
	if (x & 0xFF00U) { y |= 8; x >>= 8; }
	if (x & 0xF0U) { y |= 4; x >>= 4; }
	if (x & 0xCU) { y |= 2; x >>= 2; }
	if (x & 0x2U) { y |= 1; x >>= 1; }
	if (!(x & 0x1U)) return -1;
	return y;
}

static inline
int log_2_64(uint64_t x)
{
	int y = 0;

	if (x & 0xFFFFFFFF00000000ULL) { y |= 32; x >>= 32; }
	y |= log_2_32(static_cast<uint32_t>(x));
	return y;
}

static inline
void memset32(void* dest, uint32_t value, size_t size)
{
	__asm__ volatile ("cld; rep stosl" : "+c" (size), "+D" (dest) : "a" (value) : "memory");
}

static
uint32_t find_bit_in_array32(uint32_t* array, size_t num_entries)
{
	uint32_t x;
	int i;

	for (i = 0; num_entries; ++array, i += 8 * static_cast<int>(sizeof(uint32_t)), --num_entries) {
		x = *array;
		x = (~x & (x + 1U));
		if (!x)
			continue;
		*array |= x;
		i += log_2_32(x);
		return static_cast<uint32_t>(i);
	}
	return static_cast<uint32_t>(-1);
}

static
uint32_t find_bit_in_array64(uint64_t* array, size_t num_entries)
{
	uint64_t x;
	int i;

	for (i = 0; num_entries; ++array, i += 8 * static_cast<int>(sizeof(uint64_t)), --num_entries) {
		x = *array;
		x = (~x & (x + 1ULL));
		if (!x)
			continue;
		*array |= x;
		i += log_2_64(x);
		return static_cast<uint32_t>(i);
	}
	return static_cast<uint32_t>(-1);
}

static
void mask_array64(uint64_t* array, size_t mask_start, size_t mask_end)
{
	size_t firstFull = (mask_start + 63UL) & ~63UL;
	size_t pastFull = mask_end & ~63UL;
	size_t i, past, bytes;

	past = mask_end >= firstFull ? firstFull : mask_end;
	for (i = mask_start; i < past; ++i)
		array[i >> 6] |= 1ULL << (i & 63UL);
	if (pastFull > firstFull) {
		bytes = (pastFull - firstFull) >> 6;
		for (i = firstFull >> 6; bytes; ++i, --bytes)
			array[i] = static_cast<uint64_t>(-1);
	}
	if (pastFull < past)
		pastFull = past;
	for (i = pastFull; i < mask_end; ++i)
		array[i >> 6] |= 1ULL << (i & 63UL);
}

HIDDEN
void set_region(IOAccelDeviceRegion* rgn,
				uint32_t x,
				uint32_t y,
				uint32_t w,
				uint32_t h)
{
	rgn->num_rects = 1U;
	rgn->bounds.x = static_cast<int16_t>(x);
	rgn->bounds.y = static_cast<int16_t>(y);
	rgn->bounds.w = static_cast<int16_t>(w);
	rgn->bounds.h = static_cast<int16_t>(h);
	memcpy(&rgn->rect[0], &rgn->bounds, sizeof rgn->bounds);
}

#if 0
static
void set_region(IOAccelDeviceRegion* rgn,
				IOBlitRectangleStruct const* rects,
				size_t numRects)
{
	size_t i;

	rgn->num_rects = static_cast<uint32_t>(numRects);
	rgn->bounds.x = rects->x;
	rgn->bounds.y = rects->y;
	rgn->bounds.w = rects->x + rects->width;
	rgn->bounds.h = rects->y + rects->height;
	for (i = 1U; i < numRects; ++i) {
		IOBlitRectangleStruct const* src_rect = &rects[i];
		IOAccelBounds* dst_rect = &rgn->rect[i];
		dst_rect->x = src_rect->x;
		dst_rect->y = src_rect->y;
		dst_rect->w = src_rect->width;
		dst_rect->h = src_rect->height;
		if (dst_rect->x < rgn->bounds.x)
			rgn->bounds.x = dst_rect->x;
		if (dst_rect->x + dst_rect->w > rgn->bounds.w)
			rgn->bounds.w = dst_rect->x + dst_rect->w;
		if (dst_rect->y < rgn->bounds.y)
			rgn->bounds.y = dst_rect->y;
		if (dst_rect->y + dst_rect->h > rgn->bounds.h)
			rgn->bounds.h = dst_rect->y + dst_rect->h;
	}
	rgn->bounds.w -= rgn->bounds.x;
	rgn->bounds.h -= rgn->bounds.y;
}
#endif

#if 0
static
void clip_rect(IOBlitRectangleStruct* rect, int w, int h)
{
	if (rect->x < 0) rect->x = 0;
	if (rect->y < 0) rect->y = 0;
	if (rect->width < 0) rect->width = 0;
	if (rect->height < 0) rect->height = 0;
	if (rect->x + rect->width > w) rect->width = w - rect->x;
	if (rect->y + rect->height > h) rect->height = h - rect->y;
}
#endif

static
void convert_rect(IOAccelBounds const* src_rect,
				  SVGASignedRect* dest_rect,
				  SVGASignedPoint const* delta = 0)
{
	dest_rect->left = src_rect->x + (delta ? delta->x : 0);
	dest_rect->right = dest_rect->left + src_rect->w;
	dest_rect->top = src_rect->y + (delta ? delta->y : 0);
	dest_rect->bottom = dest_rect->top + src_rect->h;
}

static inline
uint32_t GMR_VRAM(void)
{
	return static_cast<uint32_t>(-2) /* SVGA_GMR_FRAMEBUFFER */;
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

HIDDEN
void CLASS::Cleanup()
{
	closeBackingNotifications();
	if (m_backing_access_lock) { IOLockFree(m_backing_access_lock);m_backing_access_lock=0; }
	closeSurfaceService();
	if (m_surface_service_lock) { IOLockFree(m_surface_service_lock);m_surface_service_lock=0; }
	if (m_accel_update_lock) {
		IOLockLock(m_accel_update_lock);
		if (m_accel_update_clients && m_framebuffer) m_framebuffer->useAccelUpdates(false);
		m_accel_update_clients = 0;
		m_updating_ga = 0;
		IOLockUnlock(m_accel_update_lock);
		IOLockFree(m_accel_update_lock);
		m_accel_update_lock = 0;
	}
	if (m_framebuffer_blit_buffer) {
		VRAMFree(m_framebuffer_blit_buffer);
		m_framebuffer_blit_buffer = 0;
	}
	if (m_framebuffer_blit_lock) {
		IOLockFree(m_framebuffer_blit_lock);
		m_framebuffer_blit_lock = 0;
	}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	if (m_surface_root) {
		m_surface_root->release();
		m_surface_root = 0;
	}
#endif
	if (bHaveSVGA3D) {
		bHaveSVGA3D = false;
		svga3d.Init(0);
	}
	if (bHaveScreenObject) {
		cleanupPrimaryScreen();
		bHaveScreenObject = false;
		screen.Init(0);
	}
#ifdef FB_NOTIFIER
	if (m_fbNotifier) {
		m_fbNotifier->remove();
		m_fbNotifier = 0;
	}
#endif
	if (m_allocator) {
		m_allocator->release();
		m_allocator = 0;
	}
	if (m_vram_kernel_map) {
		m_vram_kernel_map->release();
		m_vram_kernel_map = 0;
	}
	if (m_framebuffer) {
		m_svga = 0;
		m_framebuffer->release();
		m_framebuffer = 0;
	}
	if (m_iolock) {
		IOLockFree(m_iolock);
		m_iolock = 0;
	}
}

#ifdef TIMING
HIDDEN
void CLASS::timeSyncs()
{
	static uint32_t first_stamp = 0;
	static uint32_t num_syncs = 0;
	static uint32_t last_stamp;
	uint32_t current_stamp;
	uint32_t dummy;
	float freq;

	if (!first_stamp) {
		clock_get_system_microtime(&first_stamp, &dummy);
		last_stamp = first_stamp;
		return;
	}
	++num_syncs;
	clock_get_system_microtime(&current_stamp, &dummy);
	if (current_stamp >= last_stamp + 60) {
		last_stamp = current_stamp;
		freq = static_cast<float>(num_syncs)/static_cast<float>(current_stamp - first_stamp);
		if (m_framebuffer)
			m_framebuffer->setProperty("VMwareSVGASyncFrequency", static_cast<uint64_t>(freq * 1000.0F), 64U);
	}
}
#endif

HIDDEN
bool CLASS::createMasterSurface(uint32_t width, uint32_t height)
{
	IOReturn rc;

	m_master_surface_id = AllocSurfaceID();
	rc = createSurface(m_master_surface_id,
					   SVGA3dSurfaceFlags(0),
					   SVGA3D_X8R8G8B8,
					   width,
					   height);
	if (rc != kIOReturnSuccess) {
		FreeSurfaceID(m_master_surface_id);
		m_master_surface_id = SVGA_ID_INVALID;
	}
	return rc == kIOReturnSuccess;
}

HIDDEN
void CLASS::destroyMasterSurface()
{
	if (static_cast<int>(m_master_surface_id) < 0)
		return;
	destroySurface(m_master_surface_id);
	FreeSurfaceID(m_master_surface_id);
	m_master_surface_id = SVGA_ID_INVALID;
}

HIDDEN
void CLASS::processOptions()
{
	uint32_t boot_arg;

	vmw_options_ac = VMW_OPTION_AC_2D_CONTEXT | VMW_OPTION_AC_SURFACE_CONNECT;
	if (PE_parse_boot_argn("vmw_options_ac", &boot_arg, sizeof boot_arg))
		vmw_options_ac = boot_arg;
	if (PE_parse_boot_argn("-svga3d", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_SVGA3D;
	if (PE_parse_boot_argn("-vmw_no_yuv", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_NO_YUV;
	if (PE_parse_boot_argn("-vmw_direct_blit", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_DIRECT_BLIT;
	if (PE_parse_boot_argn("-vmw_no_screen_object", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_NO_SCREEN_OBJECT;
	if (PE_parse_boot_argn("-vmw_gl", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_GL_CONTEXT;
	if (PE_parse_boot_argn("-vmw_qe", &boot_arg, sizeof boot_arg))
		vmw_options_ac |= VMW_OPTION_AC_QE;
	if (checkOptionAC(VMW_OPTION_AC_QE))
		vmw_options_ac |= VMW_OPTION_AC_GL_CONTEXT;
	setProperty("VMwareSVGAAccelOptions", static_cast<uint64_t>(vmw_options_ac), 32U);
	if (PE_parse_boot_argn("vmw_options_ga", &boot_arg, sizeof boot_arg)) {
		m_options_ga = boot_arg;
		setProperty("VMwareSVGAGAOptions", static_cast<uint64_t>(m_options_ga), 32U);
	}
	if (PE_parse_boot_argn("vmw_log_ac", &boot_arg, sizeof boot_arg))
		m_log_level_ac = static_cast<int>(boot_arg);
	setProperty("VMwareSVGAAccelLogLevel", static_cast<uint64_t>(m_log_level_ac), 32U);
	if (PE_parse_boot_argn("vmw_log_ga", &boot_arg, sizeof boot_arg)) {
		m_log_level_ga = static_cast<int>(boot_arg);
		setProperty("VMwareSVGAGALogLevel", static_cast<uint64_t>(m_log_level_ga), 32U);
	}
	if (PE_parse_boot_argn("vmw_log_gld", &boot_arg, sizeof boot_arg)) {
		m_log_level_gld = static_cast<int>(boot_arg);
		setProperty("VMwareSVGAGLDLogLevel", static_cast<uint64_t>(m_log_level_gld), 32U);
	}
}

HIDDEN
IOReturn CLASS::findFramebuffer()
{
	IOService* provider;
	OSIterator* it;
	OSObject* obj;
	VMsvga2* fb = 0;

	provider = getProvider();
	if (!provider)
		return kIOReturnNotReady;
	it = provider->getClientIterator();
	if (!it)
		return kIOReturnNotFound;
	while ((obj = it->getNextObject()) != 0) {
		fb = OSDynamicCast(VMsvga2, obj);
		if (!fb)
			continue;
		if (!fb->supportsAccel()) {
			fb = 0;
			continue;
		}
		break;
	}
	if (!fb) {
		it->release();
		return kIOReturnNotFound;
	}
	fb->retain();
	m_framebuffer = fb;
	it->release();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::setupAllocator()
{
	IOReturn rc;
	IOByteCount bytes_reserve, s;
	void* p;

	if (m_vram_kernel_map)
		return kIOReturnSuccess;
	if (!m_vram)
		return kIOReturnInternalError;
	s = m_vram->getLength();
	if (m_svga->getVRAMSize() < s)
		s = m_svga->getVRAMSize();
	m_vram_kernel_map = m_vram->createMappingInTask(kernel_task, 0U, kIOMapAnywhere, 0U, s);
	if (!m_vram_kernel_map)
		return kIOReturnInternalError;
	p = reinterpret_cast<void*>(m_vram_kernel_map->getVirtualAddress());
	s &= ~(static_cast<IOByteCount>(PAGE_SIZE - 1));

	if (!p || !s) {
		rc = kIOReturnNoMemory;
		goto exit;
	}
	bytes_reserve = m_svga->getPrimaryScanoutBytes();
	rc = m_allocator->Init(p, s, bytes_reserve);
	if (rc != kIOReturnSuccess) {
		ACLog(1, "%s: Allocator Init(%p, %#lx) failed with %#x\n",
			  __FUNCTION__, p, FMT_LU(s), rc);
		goto exit;
	}
	if (bytes_reserve < s) {
		rc = m_allocator->Release(bytes_reserve, s);
		if (rc != kIOReturnSuccess) {
			ACLog(1, "%s: Allocator Release(%#lx, %#lx) failed with %#x\n",
				  __FUNCTION__, FMT_LU(bytes_reserve), FMT_LU(s), rc);
			goto exit;
		}
		s = bytes_reserve;
		bytes_reserve = 0;
	}
	if (HaveFrontBuffer()) {
		rc = m_allocator->Release(0, s);
		if (rc != kIOReturnSuccess) {
			ACLog(1, "%s: Allocator Release(%#x, %#lx) failed with %#x\n",
				  __FUNCTION__, 0, FMT_LU(s), rc);
			if (!bytes_reserve)			// We already got some memory, so it's ok
				rc = kIOReturnSuccess;
		}
	}
exit:
	if (rc != kIOReturnSuccess) {
		m_vram_kernel_map->release();
		m_vram_kernel_map = 0;
	}
	return rc;
}

#ifdef FB_NOTIFIER
HIDDEN
IOReturn CLASS::fbNotificationHandler(void* ref,
									  class IOFramebuffer* framebuffer,
									  SInt32 event,
									  void* info)
{
	ACLog(3, "%s: ref == %p, framebuffer == %p, event == %d, info == %p\n",
		  __FUNCTION__, ref, framebuffer, event, info);
	return kIOReturnSuccess;
}
#endif

HIDDEN
void CLASS::initPrimaryScreen()
{
	m_primary_screen.w = static_cast<uint32_t>(-1);
	m_primary_screen.h = static_cast<uint32_t>(-1);
}

HIDDEN
void CLASS::cleanupPrimaryScreen()
{
	m_primary_screen.w = static_cast<uint32_t>(-1);
	m_primary_screen.h = static_cast<uint32_t>(-1);
}

#pragma mark -
#pragma mark Methods from IOService
#pragma mark -

bool CLASS::init(OSDictionary* dictionary)
{
	if (!super::init(dictionary))
		return false;
	svga3d.Init(0);
	screen.Init(0);
	m_log_level_ac = LOGGING_LEVEL;
	m_accel_update_lock = 0;
	m_accel_update_clients = 0;
	m_updating_ga = 0;
	m_framebuffer_blit_lock = 0;
	m_surface_service_lock=0;m_surface_service_buffer=0;m_surface_service_token=0;
	m_surface_service_namespace[0]=m_surface_service_namespace[1]=0;m_surface_service_failed=false;
	m_framebuffer_blit_buffer = 0;
	m_framebuffer_blit_supported = -1;
	m_framebuffer_blit_enabled = true;
	m_framebuffer_blit_rejection = 0;
	m_direct_ram_upload_enabled = 1;
	m_direct_ram_bulk_upload_enabled = 1;
	m_direct_command_copy_limit = 65536;
	m_direct_gl_doorbell_supported = 0;m_direct_gl_doorbell_enabled = 1;
	m_direct_gl_parallel_doorbell_enabled = 1;
	m_backing_notify_lock=0;m_backing_access_lock=0;m_backing_notify_buffer=0;m_backing_notify_token=0;m_backing_notify_serial=0;
	m_backing_notify_registered=m_backing_notify_failed=false;bzero(m_backing_notify_jobs,sizeof(m_backing_notify_jobs));
	bzero(&m_backing_access,sizeof(m_backing_access));
	m_framebuffer_blits = m_framebuffer_blit_bytes = m_framebuffer_blit_fallbacks = 0;
	m_log_level_ga = -1;
	m_log_level_gld = -1;
	m_master_surface_id = SVGA_ID_INVALID;
	m_blitbug_result = kIOReturnNotFound;
	m_present_tracker.init();
	initPrimaryScreen();
	return true;
}

bool CLASS::start(IOService* provider)
{
	char pathbuf[256];
	int len;
	OSObject* plug;

	if (!OSDynamicCast(IOPCIDevice, provider))
		return false;
	if (!super::start(provider))
		return false;
	IOLog("IOAC: start\n");
	VMLog_SendString("log IOAC: start\n");
	processOptions();
	/*
	 * TBD: is there a possible race condition here where VMsvga2Accel::start
	 *   is called before VMsvga2 gets attached to its provider?
	 */
	if (findFramebuffer() != kIOReturnSuccess) {
		ACLog(1, "Unable to locate suitable framebuffer\n");
		stop(provider);
		return false;
	}
	m_iolock = IOLockAlloc();
	if (!m_iolock) {
		ACLog(1, "Unable to allocate IOLock\n");
		stop(provider);
		return false;
	}
	m_accel_update_lock = IOLockAlloc();
	if (!m_accel_update_lock) {
		stop(provider);
		return false;
	}
	m_vram = provider->getDeviceMemoryWithIndex(1U);
	m_framebuffer_blit_lock = IOLockAlloc();
	if (!m_framebuffer_blit_lock) {
		stop(provider);
		return false;
	}
	m_surface_service_lock=IOLockAlloc();
	if (!m_surface_service_lock) { stop(provider);return false; }
	m_backing_notify_lock=IOLockAlloc();
	if (!m_backing_notify_lock) { stop(provider);return false; }
	m_backing_access_lock=IOLockAlloc();
	if (!m_backing_access_lock) { stop(provider);return false; }
	m_allocator = VMsvga2Allocator::factory();
	if (!m_allocator) {
		ACLog(1, "Unable to create Allocator\n");
		stop(provider);
		return false;
	}
	m_svga = m_framebuffer->getDevice();
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    // Leopard's QuickTime/Setup Assistant can select this old hardware-video
    // path. A basic FIFO does NOT implement VMware overlay ESCAPE packets;
    // emitting them desynchronizes QEMU's FIFO parser. Refuse only that
    // unavailable overlay path so the client uses its native RGB/GL route.
    // GL textures/YUV shader sampling and the later guest policies are intact.
    if(!m_svga->HasFIFOCap(SVGA_FIFO_CAP_VIDEO) ||
       !m_svga->HasFIFOCap(SVGA_FIFO_CAP_ESCAPE)) {
        vmw_options_ac |= VMW_OPTION_AC_NO_YUV;
        setProperty("VMwareSVGAAccelOptions", static_cast<uint64_t>(vmw_options_ac), 32U);
    }
#endif
	if (!checkOptionAC(VMW_OPTION_AC_NO_SCREEN_OBJECT) && screen.Init(m_svga)) {
		bHaveScreenObject = true;
		ACLog(1, "Screen Object On\n");
	}
	if ((bHaveScreenObject || checkOptionAC(VMW_OPTION_AC_SVGA3D | VMW_OPTION_AC_GL_CONTEXT)) &&
		svga3d.Init(m_svga)) {
		uint32_t hwv = svga3d.getHWVersion();
		bHaveSVGA3D = true;
		ACLog(1, "SVGA3D On, 3D HWVersion == %u.%u\n", SVGA3D_MAJOR_HWVERSION(hwv), SVGA3D_MINOR_HWVERSION(hwv));
	}
	if (checkOptionAC(VMW_OPTION_AC_NO_YUV))
		ACLog(1, "YUV Off\n");
	if (checkOptionAC(VMW_OPTION_AC_GL_CONTEXT) && !HaveGLBaseline()) {
		/*
		 * Disable GL User Clients
		 */
		ACLog(1, "Disabling OpenGL support due to lack of baseline capabilities\n");
		vmw_options_ac &= ~(VMW_OPTION_AC_GL_CONTEXT | VMW_OPTION_AC_QE);
		setProperty("VMwareSVGAAccelOptions", static_cast<uint64_t>(vmw_options_ac), 32U);
	}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	if (checkOptionAC(VMW_OPTION_AC_GL_CONTEXT)) {
		m_surface_root = static_cast<IOSurfaceRoot*>(IOService::waitForService(IOService::nameMatching("IOSurfaceRoot")));
		if (m_surface_root) {
			m_surface_root->retain();
			m_surface_root_uuid = m_surface_root->generateUniqueAcceleratorID(this);
		}
	}
#endif
	mask_array64(&m_gmr_id_mask[0], m_svga->getMaxGMRIDs(), 8U * sizeof m_gmr_id_mask);
	plug = getProperty(kIOCFPlugInTypesKey);
	if (plug)
		m_framebuffer->setProperty(kIOCFPlugInTypesKey, plug);
	len = sizeof pathbuf;
	if (getPath(&pathbuf[0], &len, gIOServicePlane)) {
		m_framebuffer->setProperty(kIOAccelTypesKey, pathbuf);
		m_framebuffer->setProperty(kIOAccelIndexKey, 0ULL, 32U);
		m_framebuffer->setProperty(kIOAccelRevisionKey, static_cast<uint64_t>(kCurrentGraphicsInterfaceRevision), 32U);
	}
	setProperty(kIOAccelRevisionKey, static_cast<uint64_t>(kCurrentGraphicsInterfaceRevision), 32U);
	if (checkOptionAC(VMW_OPTION_AC_QE))
		setProperty("AccelCaps", 3ULL, 32U);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    /* Darwin9 CoreGraphics reads AccelCaps with the old raw registry API
     * into exactly four bytes (_CGXGLDisplayContextInitialize, actual9J3050).
     * A plist integer arrives as OSNumber64: the kernel refuses that read
     * with kIOReturnIPCError, leaving all display caps zero and never loading
     * the GL compositor. Match the physical driver's OSNumber32 contract,
     * preserving the paired renderer's configured capability bits. Later
     * releases use the serialized property API and retain their metadata. */
    if (OSNumber *caps = OSDynamicCast(OSNumber, getProperty("AccelCaps")))
        setProperty("AccelCaps", static_cast<uint64_t>(caps->unsigned32BitValue()), 32U);
#endif
#ifdef FB_NOTIFIER
	m_fbNotifier = m_framebuffer->addFramebufferNotification(OSMemberFunctionCast(IOFramebufferNotificationHandler, this, &CLASS::fbNotificationHandler), this, 0);
	if (!m_fbNotifier)
		ACLog(1, "Unable to register framebuffer notification handler\n");
#endif
	/*
	 * Options Are:
	 *   ATIRadeonX1000GLDriver
	 *   ATIRadeonX2000GLDriver
	 *   AppleIntelGMA950GLDriver
	 *   AppleIntelGMAX3100GLDriver
	 *   GeForce7xxxGLDriver
	 *   GeForce8xxxGLDriver
	 */
	if (checkOptionAC(VMW_OPTION_AC_GL_CONTEXT)) {
#ifdef USE_OWN_GLD
		setProperty("IOGLBundleName", "VMsvga2GLDriver");
#else
		setProperty("IOGLBundleName", "AppleIntelGMA950GLDriver");
#endif
	}
	/*
	 * Similar stupid bug to mentioned below appeared in libGFXShared.dylib
	 *   starting with OS 10.9 if it can't find this property.
	 */
	else if (version_major >= 13)
		setProperty("IOGLBundleName", "");
	/*
	 * Stupid bug in AppleVA attempts to CFRelease a NULL pointer
	 *   if it can't find this property.
	 */
	setProperty("IODVDBundleName", "AppleVADriver");
	if (
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1040
        version_major == 8 ||
#endif
        version_major == 9 || version_major == 10 || version_major == 11 || version_major == 12) {
		// Pre-Mavericks kernels lack kern.bootsessionuuid. Publish one random
		// resource-lifetime identity before WindowServer can open this driver.
		// No guest-agent startup ordering, file access or clock-derived IDs.
		uuid_t resourceNamespace;
		uuid_generate_random(resourceNamespace);
		if (!setProperty(VIM_GL_BOOT_NAMESPACE_PROPERTY, resourceNamespace, sizeof resourceNamespace))
			return false;
	}
	registerService();
	return true;
}

void CLASS::stop(IOService* provider)
{
	ACLog(2, "%s\n", __FUNCTION__);

	Cleanup();
	super::stop(provider);
}

IOReturn CLASS::newUserClient(task_t owningTask,
							  void* securityID,
							  UInt32 type,
							  IOUserClient ** handler)
{
	IOUserClient* client;

	/*
	 * Client Types
	 * 0 - Surface
	 * 1 - GL Context
	 * 2 - 2D Context
	 * 3 - DVD Context
	 * 4 - Device (duplicate for DVD Context in OS 10.5)
	 * 5 - OCD Context
	 */
	ACLog(2, "%s: owningTask==%p, securityID==%p, type==%u\n",
		  __FUNCTION__,
		  owningTask,
		  securityID,
		  FMT_U(type));
	if (!handler)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	switch (type) {
		case kIOAccelSurfaceClientType:
			if (!checkOptionAC(VMW_OPTION_AC_SURFACE_CONNECT))
				return kIOReturnUnsupported;
			/* Native CGL creates application-owned pbuffer surfaces too. The
			 * legacy GA-only admission rule made IOService's five-argument
			 * newUserClient fallback report Unsupported before set_id_mode.
			 * PassthroughOpenGL app clients below are restricted to private pbuffers,
			 * never display/WindowServer surface families. */
			if (m_updating_ga != 0 && m_updating_ga != owningTask &&
				!getProperty("VIMDirectGLCompositor"))
				return kIOReturnNotPrivileged;
			if (setupAllocator() != kIOReturnSuccess)
				return kIOReturnNoMemory;
			client = VMsvga2Surface::withTask(owningTask, securityID, type,
				m_updating_ga != 0 && m_updating_ga != owningTask);
			break;
		case 1:
			if (!checkOptionAC(VMW_OPTION_AC_GL_CONTEXT))
				return kIOReturnUnsupported;
			client = VMsvga2GLContext::withTask(owningTask, securityID, type);
			break;
		case 2:
			if (!checkOptionAC(VMW_OPTION_AC_2D_CONTEXT))
				return kIOReturnUnsupported;
			client = VMsvga22DContext::withTask(owningTask, securityID, type);
			break;
		case 3:
			if (!checkOptionAC(VMW_OPTION_AC_DVD_CONTEXT))
				return kIOReturnUnsupported;
			client = VMsvga2DVDContext::withTask(owningTask, securityID, type);
			break;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
		case 4:
			if (!checkOptionAC(VMW_OPTION_AC_GL_CONTEXT))
				return kIOReturnUnsupported;
			client = VMsvga2Device::withTask(owningTask, securityID, type);
			break;
		case 5:
			if (!checkOptionAC(VMW_OPTION_AC_GL_CONTEXT))
				return kIOReturnUnsupported;
			client = VMsvga2OCDContext::withTask(owningTask, securityID, type);
			break;
#endif
		default:
			return kIOReturnUnsupported;
	}
	if (!client)
		return kIOReturnNoResources;
	if (!client->attach(this)) {
		client->release();
		return kIOReturnInternalError;
	}
	if (!client->start(this)) {
		client->detach(this);
		client->release();
		return kIOReturnInternalError;
	}
	*handler = client;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark SVGA FIFO Sync Methods
#pragma mark -

bool CLASS::directGLAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_REGISTER)==VIM_GL_MAGIC;
    m_direct_gl_doorbell_supported=available &&
        m_svga->ReadReg(VIM_GL_DOORBELL_CAPABILITIES_REGISTER)==VIM_GL_DOORBELL_MAGIC;
    m_framebuffer->unlockDevice();
    // GL enumeration can precede the first CGS surface user client, which
    // previously was the only caller initializing the VRAM allocator.
    if (available && setupAllocator()!=kIOReturnSuccess) return false;
    return available;
}
bool CLASS::directGLDirectReadAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_CAPABILITIES_REGISTER)==VIM_GL_CAP_DIRECT_READ;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLReadOffset(IOMemoryDescriptor* memory,IOByteCount bytes,uint32_t* offset) const
{
    if (!memory || !m_vram || !offset || !bytes || bytes>VIM_GL_CHUNK || memory->getLength()<bytes)
        return false;
    IOByteCount vram_length=0,length=0;
    addr64_t base=VIMPhysicalSegment(m_vram,0,&vram_length);
    addr64_t address=VIMPhysicalSegment(memory,0,&length);
    if (!base || !address || length<bytes || address<base) return false;
    uint64_t relative=address-base;
    if (relative>UINT32_MAX || relative>vram_length || bytes>vram_length-relative ||
        relative>getVRAMSize() || bytes>getVRAMSize()-relative) return false;
    *offset=static_cast<uint32_t>(relative);return true;
}
bool CLASS::directGLDirectWriteAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_WRITE_CAPABILITIES_REGISTER)==VIM_GL_WRITE_MAGIC;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLScatterAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_SCATTER_CAPABILITIES_REGISTER)==VIM_GL_SCATTER_MAGIC;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLRAMScatterAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_RAM_SCATTER_CAPABILITIES_REGISTER)==VIM_GL_RAM_SCATTER_MAGIC;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLMaskedScatterAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_MASKED_SCATTER_CAPABILITIES_REGISTER)==VIM_GL_MASKED_SCATTER_MAGIC;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLRAMUploadAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_RAM_UPLOAD_CAPABILITIES_REGISTER)==VIM_GL_RAM_UPLOAD_MAGIC;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLExchangeAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_EXCHANGE_CAPABILITIES_REGISTER)==VIM_GL_EXCHANGE_MAGIC;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLBulkUploadAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_BULK_UPLOAD_CAPABILITIES_REGISTER)==VIM_GL_BULK_UPLOAD_MAGIC;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLLargeExchangeAvailable()
{
    if (!m_framebuffer || !m_svga) return false;
    m_framebuffer->lockDevice();
    bool available=m_svga->ReadReg(VIM_GL_LARGE_EXCHANGE_CAPABILITIES_REGISTER)==VIM_GL_LARGE_EXCHANGE_MAGIC;
    m_framebuffer->unlockDevice();return available;
}
bool CLASS::directGLVRAMRange(uint64_t* base,uint64_t* length) const
{
    if (!m_vram || !base || !length) return false;
    IOByteCount size=0;addr64_t address=VIMPhysicalSegment(m_vram,0,&size);
    if (!address || !size) return false;
    *base=address;*length=size<getVRAMSize()?size:getVRAMSize();return true;
}
IOReturn CLASS::directGLTransfer(void* buffer)
{
    if (!buffer || !m_framebuffer || !m_svga) return kIOReturnNoDevice;
    // Every caller owns its packet under an endpoint, service or backing
    // lock, and retains the provider through the synchronous transfer. This
    // dedicated port never reads/writes the shared register-index latch or
    // framebuffer geometry. Do not serialize unrelated clients (including
    // a producer needed by a waiting consumer) behind native GL execution.
    // QEMU separately retains/validates its endpoint and device generation
    // while native I/O runs without BQL. The established notification path
    // already uses this same independent doorbell contract.
    if (m_direct_gl_doorbell_supported && m_direct_gl_doorbell_enabled &&
        m_direct_gl_parallel_doorbell_enabled) {
        m_svga->WriteDirectGLDoorbell(static_cast<uint32_t>(offsetInVRAM(buffer)));
        return kIOReturnSuccess;
    }
    // Old hosts and indexed-register users still require latch exclusion.
    m_framebuffer->lockDevice();
    if(m_direct_gl_doorbell_supported && m_direct_gl_doorbell_enabled)
        m_svga->WriteDirectGLDoorbell(static_cast<uint32_t>(offsetInVRAM(buffer)));
    else m_svga->WriteReg(VIM_GL_REGISTER,static_cast<uint32_t>(offsetInVRAM(buffer)));
    m_framebuffer->unlockDevice();return kIOReturnSuccess;
}

#include "VMsvga2FramebufferBlit.inc"
#include "VMsvga2SurfaceService.inc"
#include "VMsvga2BackingNotify.inc"

HIDDEN
IOReturn CLASS::SyncFIFO()
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->SyncFIFO();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::RingDoorBell()
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::SyncToFence(uint32_t fence)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->SyncToFence(fence);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark SVGA FIFO Acceleration Methods for 2D Context
#pragma mark -

HIDDEN
IOReturn CLASS::useAccelUpdates(bool state, task_t owningTask)
{
	if (!m_accel_update_lock || !owningTask) return kIOReturnNotReady;
	IOLockLock(m_accel_update_lock);
	IOReturn result = kIOReturnSuccess;
	if (!m_framebuffer) result = kIOReturnNoDevice;
	else if (state) {
		if (m_updating_ga && m_updating_ga != owningTask) result = kIOReturnExclusiveAccess;
		else if (m_accel_update_clients == UINT32_MAX) result = kIOReturnNoResources;
		else {
			m_updating_ga = owningTask;
			if (!m_accel_update_clients++) m_framebuffer->useAccelUpdates(true);
		}
	} else if (m_updating_ga == owningTask && m_accel_update_clients) {
		if (!--m_accel_update_clients) {
			m_updating_ga = 0;
			m_framebuffer->useAccelUpdates(false);
		}
	}
	// Acquisitions/releases are rare, not a per-frame registry/logging path.
	if (!result) setProperty("VIMAcceleratedUpdateClients", static_cast<uint64_t>(m_accel_update_clients), 32U);
	IOLockUnlock(m_accel_update_lock);
	return result;
}

/*
 * Note: RectCopy, UpdateFramebuffer* and CopyRegion don't work in SVGA3D
 *   mode.  This is ok, since the WindowServer doesn't use them.
 *   UpdateFramebuffer* are called from IOFBSynchronize in GA.  The other functions
 *   are called from the various blitters.  In SVGA3D mode the WindowServer doesn't
 *   use IOFBSynchronize (it uses surface_flush instead), and CopyRegion blits
 *   are done to a surface destination.
 */
HIDDEN
IOReturn CLASS::RectCopy(uint32_t framebufferIndex,
						 struct IOBlitCopyRectangleStruct const* copyRects,
						 size_t copyRectsSize)
{
	size_t i, count = copyRectsSize / sizeof(IOBlitCopyRectangle);
	bool rc;

	if (!count || !copyRects)
		return kIOReturnBadArgument;
	if (HaveFrontBuffer()) {
		DefineRegion<1U> tmpRegion;
		IOReturn rv = kIOReturnSuccess;

		for (i = 0; i < count; ++i) {
			struct IOBlitCopyRectangleStruct const* rect = &copyRects[i];
			set_region(&tmpRegion.r,
					   rect->sourceX,
					   rect->sourceY,
					   rect->width,
					   rect->height);
			rv = CopyRegion(framebufferIndex,
							rect->x,
							rect->y,
							&tmpRegion.r,
							sizeof tmpRegion);
			if (rv != kIOReturnSuccess)
				break;
		}
		return rv;
	}
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	for (i = 0; i < count; ++i) {
		rc = m_svga->RectCopy(reinterpret_cast<uint32_t const*>(&copyRects[i]));
		if (!rc)
			break;
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

#if 0
HIDDEN
IOReturn CLASS::RectFillScreen(uint32_t framebufferIndex,
							   uint32_t color,
							   struct IOBlitRectangleStruct const* rects,
							   size_t numRects)
{
	SVGAColorBGRX c;
	size_t s1, s2;
	void* p;
	IOAccelDeviceRegion* rgn;
	VMsvga2Accel::ExtraInfo extra;

	s1 = sizeof(IOAccelDeviceRegion) * numRects * sizeof(IOAccelBounds);
	rgn = static_cast<IOAccelDeviceRegion*>(IOMalloc(s1));
	if (!rgn)
		return kIOReturnNoMemory;
	set_region(rgn, rects, numRects);

	s2 = rgn->bounds.w * rgn->bounds.h * sizeof(uint32_t);
	p = VRAMMalloc(s2);
	if (!p) {
		IOFree(rgn, s1);
		return kIOReturnNoMemory;
	}
	memset32(p, color, s2 / sizeof(uint32_t));
	bzero(&extra, sizeof extra);
	extra.mem_pitch = rgn->bounds.w * sizeof(uint32_t);
	extra.srcDeltaX = -static_cast<int>(rgn->bounds.x);
	extra.srcDeltaY = -static_cast<int>(rgn->bounds.y);
	c.value = color;
	m_framebuffer->lockDevice();
	extra.mem_gmr_id = GMR_VRAM();
	extra.mem_offset_in_gmr = reinterpret_cast<vm_offset_t>(p) - m_vram_kernel_ptr->getVirtualAddress();
	screen.AnnotateFill(c);
	m_framebuffer->unlockDevice();
	blitToScreen(framebufferIndex,
				 rgn,
				 &extra);
	SyncFIFO();
	VRAMFree(p);
	IOFree(rgn, s1);
	return kIOReturnSuccess;
}
#endif

#if 0
HIDDEN
IOReturn CLASS::RectFill3D(uint32_t color,
						   struct IOBlitRectangleStruct const* rects,
						   size_t numRects)
{
	size_t s;
	uint32_t sid, cid;
	IOAccelDeviceRegion* rgn;
	VMsvga2Accel::ExtraInfo extra;

	if (m_master_surface_retain_count <= 0)
		return kIOReturnSuccess;		// Nothing to do

	s = sizeof(IOAccelDeviceRegion) * numRects * sizeof(IOAccelBounds);
	rgn = static_cast<IOAccelDeviceRegion*>(IOMalloc(s));
	if (!rgn)
		return kIOReturnNoMemory;
	set_region(rgn, rects, numRects);

	sid = getMasterSurfaceID();
	cid = AllocContextID();
	if (createContext(cid) != kIOReturnSuccess) {
		FreeContextID(cid);
		IOFree(rgn, s);
		return kIOReturnError;
	}
	/*
	 * Note: I think MasterSurface needs to have flags
	 *   SVGA3D_SURFACE_HINT_RENDERTARGET for this to work.
	 */
	setRenderTarget(cid, SVGA3D_RT_COLOR0, sid);
	clear(cid,
		  static_cast<SVGA3dClearFlag>(SVGA3D_CLEAR_COLOR),
		  rgn,
		  color,
		  1.0F,
		  0);
	destroyContext(cid);
	FreeContextID(cid);
	bzero(&extra, sizeof extra);
	surfacePresentAutoSync(sid,
						   rgn,
						   &extra);
	IOFree(rgn, s);
	return kIOReturnSuccess;
}
#endif

HIDDEN
IOReturn CLASS::RectFill(uint32_t framebufferIndex,
						 uint32_t color,
						 struct IOBlitRectangleStruct const* rects,
						 size_t rectsSize)
{
	size_t i, count = rectsSize / sizeof(IOBlitRectangle);
	bool rc;

	if (!count || !rects)
		return kIOReturnBadArgument;
	ACLog(2, "%s: color == %#x, numRects == %lu, [%d, %d, %d, %d]\n",
		  __FUNCTION__, color, count, FMT_D(rects->x), FMT_D(rects->y), FMT_D(rects->width), FMT_D(rects->height));
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	for (i = 0; i < count; ++i) {
		rc = m_svga->RectFill(color, reinterpret_cast<uint32_t const*>(&rects[i]));
		if (!rc)
			break;
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

HIDDEN
IOReturn CLASS::UpdateFramebufferAutoRing(uint32_t const* rect)
{
	if (!rect)
		return kIOReturnBadArgument;
	if (HaveFrontBuffer())
		return kIOReturnSuccess;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->UpdateFramebuffer2(rect);
	m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
#ifdef TIMING
	timeSyncs();
#endif
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::CopyRegion(uint32_t framebufferIndex,
						   int destX,
						   int destY,
						   void /* IOAccelDeviceRegion */ const* region,
						   size_t regionSize)
{
	IOAccelDeviceRegion const* rgn = static_cast<IOAccelDeviceRegion const*>(region);
	IOAccelBounds const* rect;
	int deltaX, deltaY;
	uint32_t i, copyRect[6];
	bool rc;

	if (!rgn || regionSize < IOACCEL_SIZEOF_DEVICE_REGION(rgn))
		return kIOReturnBadArgument;
	if (HaveFrontBuffer()) {
		ACLog(1, "%s: called with SVGAScreen or SVGA3D - unsupported\n", __FUNCTION__);
		return kIOReturnSuccess /* pretend to work... kIOReturnUnsupported */;
	}
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	rect = &rgn->bounds;
	if (checkOptionAC(VMW_OPTION_AC_REGION_BOUNDS_COPY)) {
		copyRect[0] = rect->x;
		copyRect[1] = rect->y;
		copyRect[2] = static_cast<uint32_t>(destX);
		copyRect[3] = static_cast<uint32_t>(destY);
		copyRect[4] = rect->w;
		copyRect[5] = rect->h;
		rc = m_svga->RectCopy(&copyRect[0]);
	} else {
		deltaX = destX - rect->x;
		deltaY = destY - rect->y;
		for (i = 0; i < rgn->num_rects; ++i) {
			rect = &rgn->rect[i];
			copyRect[0] = rect->x;
			copyRect[1] = rect->y;
			copyRect[2] = rect->x + deltaX;
			copyRect[3] = rect->y + deltaY;
			copyRect[4] = rect->w;
			copyRect[5] = rect->h;
			rc = m_svga->RectCopy(&copyRect[0]);
			if (!rc)
				break;
		}
	}
	m_framebuffer->unlockDevice();
	return rc ? kIOReturnSuccess : kIOReturnNoMemory;
}

#pragma mark -
#pragma mark Acceleration Methods for Surfaces
#pragma mark -

HIDDEN
IOReturn CLASS::createSurface(uint32_t sid,
							  SVGA3dSurfaceFlags surfaceFlags,
							  SVGA3dSurfaceFormat surfaceFormat,
							  uint32_t width,
							  uint32_t height)
{
	bool rc;
	SVGA3dSize* mipSizes;
	SVGA3dSurfaceFace* faces;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginDefineSurface(sid, surfaceFlags, surfaceFormat, &faces, &mipSizes, 1U);
	if (!rc)
		goto exit;
	faces[0].numMipLevels = 1U;
	mipSizes[0].width = width;
	mipSizes[0].height = height;
	mipSizes[0].depth = 1U;
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::destroySurface(uint32_t sid)
{
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	svga3d.DestroySurface(sid);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surfaceDMA2D(uint32_t sid,
							 SVGA3dTransferType transfer,
							 void /* IOAccelDeviceRegion */ const* region,
							 ExtraInfo const* extra,
							 uint32_t* fence)
{
	bool rc;
	uint32_t i, numCopyBoxes;
	SVGA3dCopyBox* copyBoxes;
	SVGA3dGuestImage guestImage;
	SVGA3dSurfaceImageId hostImage;
	IOAccelDeviceRegion const* rgn;

	if (!extra)
		return kIOReturnBadArgument;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numCopyBoxes = rgn ? rgn->num_rects : 0;
	hostImage.sid = sid;
	hostImage.face = 0;
	hostImage.mipmap = 0;
	guestImage.ptr.gmrId = extra->mem_gmr_id;
	guestImage.ptr.offset = static_cast<uint32_t>(extra->mem_offset_in_gmr);
	guestImage.pitch = static_cast<uint32_t>(extra->mem_pitch);
	m_framebuffer->lockDevice();
	rc = svga3d.BeginSurfaceDMA(&guestImage, &hostImage, transfer, &copyBoxes, numCopyBoxes);
	if (!rc)
		goto exit;
	for (i = 0; i < numCopyBoxes; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dCopyBox* dst = &copyBoxes[i];
		dst->srcx = src->x + extra->srcDeltaX;
		dst->srcy = src->y + extra->srcDeltaY;
		dst->x = src->x + extra->dstDeltaX;
		dst->y = src->y + extra->dstDeltaY;
		dst->w = src->w;
		dst->h = src->h;
		dst->d = 1;
	}
	m_svga->FIFOCommitAll();
	if (fence)
		*fence = m_svga->InsertFence();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surfaceCopy(uint32_t src_sid,
							uint32_t dst_sid,
							void /* IOAccelDeviceRegion */ const* region,
							ExtraInfo const* extra)
{
	bool rc;
	uint32_t i, numCopyBoxes;
	SVGA3dCopyBox* copyBoxes;
	SVGA3dSurfaceImageId srcImage, dstImage;
	IOAccelDeviceRegion const* rgn;

	if (!extra)
		return kIOReturnBadArgument;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numCopyBoxes = rgn ? rgn->num_rects : 0;
	bzero(&srcImage, sizeof srcImage);
	bzero(&dstImage, sizeof dstImage);
	srcImage.sid = src_sid;
	dstImage.sid = dst_sid;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginSurfaceCopy(&srcImage, &dstImage, &copyBoxes, numCopyBoxes);
	if (!rc)
		goto exit;
	for (i = 0; i < numCopyBoxes; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dCopyBox* dst = &copyBoxes[i];
		dst->srcx = src->x + extra->srcDeltaX;
		dst->srcy = src->y + extra->srcDeltaY;
		dst->x = src->x + extra->dstDeltaX;
		dst->y = src->y + extra->dstDeltaY;
		dst->w = src->w;
		dst->h = src->h;
		dst->d = 1;
	}
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surfaceStretch(uint32_t src_sid,
							   uint32_t dst_sid,
							   SVGA3dStretchBltMode mode,
							   void /* IOAccelBounds */ const* src_rect,
							   void /* IOAccelBounds */ const* dest_rect)
{
	SVGA3dBox srcBox, dstBox;
	SVGA3dSurfaceImageId srcImage, dstImage;
	IOAccelBounds const* s_rect;
	IOAccelBounds const* d_rect;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	s_rect = static_cast<IOAccelBounds const*>(src_rect);
	d_rect = static_cast<IOAccelBounds const*>(dest_rect);
	bzero(&srcImage, sizeof srcImage);
	bzero(&dstImage, sizeof dstImage);
	srcImage.sid = src_sid;
	dstImage.sid = dst_sid;
	srcBox.x = static_cast<uint32_t>(s_rect->x);
	srcBox.y = static_cast<uint32_t>(s_rect->y);
	srcBox.z = 0;
	srcBox.w = static_cast<uint32_t>(s_rect->w);
	srcBox.h = static_cast<uint32_t>(s_rect->h);
	srcBox.d = 1;
	dstBox.x = static_cast<uint32_t>(d_rect->x);
	dstBox.y = static_cast<uint32_t>(d_rect->y);
	dstBox.z = 0;
	dstBox.w = static_cast<uint32_t>(d_rect->w);
	dstBox.h = static_cast<uint32_t>(d_rect->h);
	dstBox.d = 1;
	m_framebuffer->lockDevice();
	svga3d.SurfaceStretchBlt(&srcImage, &dstImage, &srcBox, &dstBox, mode);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surfacePresentAutoSync(uint32_t sid,
									   void /* IOAccelDeviceRegion */ const* region,
									   ExtraInfo const* extra)
{
	bool rc;
	uint32_t i, numCopyRects;
	SVGA3dCopyRect* copyRects;
	IOAccelDeviceRegion const* rgn;

	if (!extra)
		return kIOReturnBadArgument;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numCopyRects = rgn ? rgn->num_rects : 0;
	m_framebuffer->lockDevice();
	m_svga->SyncToFence(m_present_tracker.before());
	rc = svga3d.BeginPresent(sid, &copyRects, numCopyRects);
	if (!rc)
		goto exit;
	for (i = 0; i < numCopyRects; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dCopyRect* dst = &copyRects[i];
		dst->srcx = src->x + extra->srcDeltaX;
		dst->srcy = src->y + extra->srcDeltaY;
		dst->x = src->x + extra->dstDeltaX;
		dst->y = src->y + extra->dstDeltaY;
		dst->w = src->w;
		dst->h = src->h;
	}
	m_svga->FIFOCommitAll();
	m_present_tracker.after(m_svga->InsertFence());
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surfacePresentReadback(void /* IOAccelDeviceRegion */ const* region)
{
	bool rc;
	uint32_t i, numRects;
	SVGA3dRect* rects;
	IOAccelDeviceRegion const* rgn;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numRects = rgn ? rgn->num_rects : 0;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginPresentReadback(&rects, numRects);
	if (!rc)
		goto exit;
	for (i = 0; i < numRects; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGA3dRect* dst = &rects[i];
		dst->x = src->x;
		dst->y = src->y;
		dst->w = src->w;
		dst->h = src->h;
	}
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::setRenderTarget(uint32_t cid,
								SVGA3dRenderTargetType rtype,
								uint32_t sid)
{
	SVGA3dSurfaceImageId imageId;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	bzero(&imageId, sizeof imageId);
	imageId.sid = sid;
	m_framebuffer->lockDevice();
	svga3d.SetRenderTarget(cid, rtype, &imageId);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::clear(uint32_t cid,
					  SVGA3dClearFlag flags,
					  void /* IOAccelDeviceRegion */ const* region,
					  uint32_t color,
					  float depth,
					  uint32_t stencil)
{
	bool rc;
	uint32_t i, numRects;
	SVGA3dRect* rects;
	IOAccelDeviceRegion const* rgn;

	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numRects = rgn ? rgn->num_rects : 0;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginClear(cid, flags, color, depth, stencil, &rects, numRects);
	if (!rc)
		goto exit;
	for (i = 0; i < numRects; ++i) {
		rects[i].x = rgn->rect[i].x;
		rects[i].y = rgn->rect[i].y;
		rects[i].w = rgn->rect[i].w;
		rects[i].h = rgn->rect[i].h;
	}
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::createContext(uint32_t cid)
{
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	svga3d.DefineContext(cid);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::destroyContext(uint32_t cid)
{
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	svga3d.DestroyContext(cid);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::drawPrimitives(uint32_t cid,
							   uint32_t numVertexDecls,
							   uint32_t numRanges,
							   SVGA3dVertexDecl const* decls,
							   SVGA3dPrimitiveRange const* ranges)
{
	SVGA3dVertexDecl* ds;
	SVGA3dPrimitiveRange* rs;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	if (!svga3d.BeginDrawPrimitives(cid,
									&ds,
									numVertexDecls,
									&rs,
									numRanges))
		goto exit;
	memcpy(ds, decls, numVertexDecls * sizeof *decls);
	memcpy(rs, ranges, numRanges * sizeof *ranges);
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::setTextureState(uint32_t cid,
								uint32_t numStates,
								SVGA3dTextureState const* states)
{
	SVGA3dTextureState* st;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	if (!svga3d.BeginSetTextureState(cid,
									 &st,
									 numStates))
		goto exit;
	memcpy(st, states, numStates * sizeof *states);
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::setRenderState(uint32_t cid,
							   uint32_t numStates,
							   SVGA3dRenderState const* states)
{
	SVGA3dRenderState* st;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	if (!svga3d.BeginSetRenderState(cid,
									&st,
									numStates))
		goto exit;
	memcpy(st, states, numStates * sizeof *states);
	m_svga->FIFOCommitAll();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::surfaceDMA3DEx(SVGA3dSurfaceImageId const* hostImage,
							   SVGA3dTransferType transfer,
							   SVGA3dCopyBox const* copyBox,
							   ExtraInfoEx const* extra,
							   uint32_t* fence)
{
	bool rc;
	SVGA3dCopyBox* copyBoxes;
	SVGA3dGuestImage guestImage;

	if (!extra || !copyBox)
		return kIOReturnBadArgument;
	if (!bHaveSVGA3D)
		return kIOReturnNoDevice;
	guestImage.ptr.gmrId = extra->mem_gmr_id;
	guestImage.ptr.offset = static_cast<uint32_t>(extra->mem_offset_in_gmr);
	guestImage.pitch = static_cast<uint32_t>(extra->mem_pitch);
	m_framebuffer->lockDevice();
	rc = svga3d.BeginSurfaceDMAwithSuffix(&guestImage,
										  hostImage,
										  transfer,
										  &copyBoxes,
										  1U,
										  static_cast<uint32_t>(extra->mem_limit),
										  *reinterpret_cast<SVGA3dSurfaceDMAFlags const*>(&extra->suffix_flags));
	if (!rc)
		goto exit;
	memcpy(&copyBoxes[0], copyBox, sizeof *copyBox);
	m_svga->FIFOCommitAll();
	if (fence)
		*fence = m_svga->InsertFence();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Screen Support Methods
#pragma mark -

HIDDEN
IOReturn CLASS::createPrimaryScreen(uint32_t width,
									uint32_t height)
{
	SVGAScreenObject new_screen;

	if (!bHaveScreenObject)
		return kIOReturnNoDevice;
	if (width == m_primary_screen.w && height == m_primary_screen.h)
		return kIOReturnSuccess;
	bzero(&new_screen, sizeof new_screen);
	new_screen.structSize = sizeof new_screen;
	new_screen.flags = SVGA_SCREEN_HAS_ROOT | SVGA_SCREEN_IS_PRIMARY;
	new_screen.size.width = width;
	new_screen.size.height = height;
	/*
	 * Note: Screen Object 2 requires allocation of
	 *   guest-side backing for screen ('base-layer').
	 *   However, for the primary screen, the guest-side
	 *   backing is not touched.  So we pretend to allocate
	 *   it, but don't allocate any memory.
	 */
	new_screen.backingStore.ptr.gmrId = GMR_VRAM();
#if 0
	new_screen.backingStore.ptr.offset = 0U;
#endif
	new_screen.backingStore.pitch = (width * sizeof(uint32_t) + 7U) & -8;
	m_framebuffer->lockDevice();
	screen.DefineScreen(&new_screen);
	m_framebuffer->unlockDevice();
	m_primary_screen.w = width;
	m_primary_screen.h = height;
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::blitFromScreen(uint32_t srcScreenId,
							   void /* IOAccelDeviceRegion */ const* region,
							   ExtraInfo const* extra,
							   uint32_t* fence)
{
	uint32_t i, numRects;
	SVGAGuestPtr guestPtr;
	SVGAGMRImageFormat fmt;
	IOAccelDeviceRegion const* rgn;

	if (!extra)
		return kIOReturnBadArgument;
	if (!bHaveScreenObject)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numRects = rgn ? rgn->num_rects : 0;
	guestPtr.gmrId = extra->mem_gmr_id;
	guestPtr.offset = static_cast<uint32_t>(extra->mem_offset_in_gmr);
	fmt.value = 0x1820U;
	m_framebuffer->lockDevice();
	screen.DefineGMRFB(guestPtr,
					   static_cast<uint32_t>(extra->mem_pitch),
					   fmt);
	for (i = 0; i < numRects; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGASignedPoint destOrigin;
		SVGASignedRect srcRect;
		convert_rect(src, &srcRect, reinterpret_cast<SVGASignedPoint const*>(&extra->srcDeltaX));
		destOrigin.x = src->x + extra->dstDeltaX;
		destOrigin.y = src->y + extra->dstDeltaY;
		screen.BlitToGMRFB(&destOrigin, &srcRect, srcScreenId);
	}
	if (fence)
		*fence = m_svga->InsertFence();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::blitToScreen(uint32_t destScreenId,
							 void /* IOAccelDeviceRegion */ const* region,
							 ExtraInfo const* extra,
							 uint32_t* fence)
{
	uint32_t i, numRects;
	SVGAGuestPtr guestPtr;
	SVGAGMRImageFormat fmt;
	IOAccelDeviceRegion const* rgn;

	if (!extra)
		return kIOReturnBadArgument;
	if (!bHaveScreenObject)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numRects = rgn ? rgn->num_rects : 0;
	guestPtr.gmrId = extra->mem_gmr_id;
	guestPtr.offset = static_cast<uint32_t>(extra->mem_offset_in_gmr);
	fmt.value = 0x1820U;
	m_framebuffer->lockDevice();
	screen.DefineGMRFB(guestPtr,
					   static_cast<uint32_t>(extra->mem_pitch),
					   fmt);
	for (i = 0; i < numRects; ++i) {
		IOAccelBounds const* src = &rgn->rect[i];
		SVGASignedPoint srcOrigin;
		SVGASignedRect destRect;
		convert_rect(src, &destRect, reinterpret_cast<SVGASignedPoint const*>(&extra->dstDeltaX));
		srcOrigin.x = src->x + extra->srcDeltaX;
		srcOrigin.y = src->y + extra->srcDeltaY;
		screen.BlitFromGMRFB(&srcOrigin, &destRect, destScreenId);
	}
	if (fence)
		*fence = m_svga->InsertFence();
	m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::blitSurfaceToScreen(uint32_t src_sid,
									uint32_t destScreenId,
									void /* IOAccelBounds */ const* src_rect,
									void /* IOAccelDeviceRegion */ const* dest_region)
{
	bool rc;
	uint32_t i, numRects;
	SVGA3dSurfaceImageId srcImage;
	SVGASignedRect srcRect;
	SVGASignedRect destRect;
	SVGASignedPoint shift;
	SVGASignedRect* clipRects;
	IOAccelDeviceRegion const* rgn;
	IOAccelBounds const* s_rect;

	if (!bHaveScreenObject)
		return kIOReturnNoDevice;
	rgn = static_cast<IOAccelDeviceRegion const*>(dest_region);
	numRects = rgn ? rgn->num_rects : 0;
	s_rect = static_cast<IOAccelBounds const*>(src_rect);
	bzero(&srcImage, sizeof srcImage);
	srcImage.sid = src_sid;
	convert_rect(s_rect, &srcRect);
	convert_rect(&rgn->bounds, &destRect);
	shift.x = -destRect.left;
	shift.y = -destRect.top;
	m_framebuffer->lockDevice();
	rc = svga3d.BeginBlitSurfaceToScreen(&srcImage,
										 &srcRect,
										 destScreenId,
										 &destRect,
										 &clipRects,
										 numRects);
	if (!rc)
		goto exit;
	for (i = 0; i < numRects; ++i)
		convert_rect(&rgn->rect[i], &clipRects[i], &shift);
	m_svga->FIFOCommitAll();
	m_svga->RingDoorBell();
exit:
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark GFB Methods
#pragma mark -

/*
 * Note:
 *   extra->dstDelta coords apply to GMR
 *   extra->srcDelta coords apply to GFB
 */
HIDDEN
IOReturn CLASS::blitGFB(uint32_t framebufferIndex,
						void /* IOAccelDeviceRegion */ const* region,
						ExtraInfo const* extra,
						IOVirtualAddress gmrPtr,
						vm_size_t limitFromGmrPtr,
						int direction)
{
	IOVirtualAddress gfb_base, gmr_base;
	SVGAGuestImage gfb_image, gmr_image;
	SVGASignedPoint gfb_delta, gmr_delta;

	if (!extra)
		return kIOReturnBadArgument;
	if (!m_framebuffer || !m_vram_kernel_map)
		return kIOReturnNotReady;
	gfb_base = m_vram_kernel_map->getVirtualAddress();
	if (!gmrPtr) {
		if (extra->mem_gmr_id == GMR_VRAM())
			gmrPtr = gfb_base;
		else
			return kIOReturnBadArgument;
	}
	m_framebuffer->lockDevice();
	gfb_base += m_svga->getCurrentFBOffset();
	gfb_image.pitch = m_svga->getCurrentPitch();
	gfb_image.ptr.offset = m_svga->getCurrentFBSize();
	/*
	 * TBD: should we lock for the entire blit?
	 */
	m_framebuffer->unlockDevice();
	gmr_base = gmrPtr + extra->mem_offset_in_gmr;
	gmr_image.pitch = static_cast<uint32_t>(extra->mem_pitch);
	gmr_image.ptr.offset = static_cast<uint32_t>(limitFromGmrPtr - extra->mem_offset_in_gmr);
	gmr_delta.x = extra->dstDeltaX;
	gmr_delta.y = extra->dstDeltaY;
	gfb_delta.x = extra->srcDeltaX;
	gfb_delta.y = extra->srcDeltaY;
	if (direction)
		return genericBlitCopy(gfb_base,
							   &gfb_image,
							   &gfb_delta,
							   gmr_base,
							   &gmr_image,
							   &gmr_delta,
							   region,
							   sizeof(uint32_t));
	return genericBlitCopy(gmr_base,
						   &gmr_image,
						   &gmr_delta,
						   gfb_base,
						   &gfb_image,
						   &gfb_delta,
						   region,
						   sizeof(uint32_t));
}

#if 0
HIDDEN
IOReturn CLASS::clearGFB(uint32_t color,
						 struct IOBlitRectangleStruct const* rects,
						 size_t numRects)
{
	size_t i;
	int gfb_pitch, gfb_w, gfb_h;
	int const bytes_per_pixel = sizeof(uint32_t);
	IOVirtualAddress gfb_start;

	if (!m_framebuffer)
		return kIOReturnNotReady;
	m_framebuffer->lockDevice();
	gfb_start = m_vram_kernel_map->getVirtualAddress() + m_svga->getCurrentFBOffset();
	gfb_w = m_svga->getCurrentWidth();
	gfb_h = m_svga->getCurrentHeight();
	gfb_pitch = m_svga->getCurrentPitch();
	/*
	 * TBD: should we lock for the entire blit?
	 */
	m_framebuffer->unlockDevice();
	for (i = 0; i < numRects; ++i) {
		IOBlitRectangleStruct rect = rects[i];
		clip_rect(&rect, gfb_w, gfb_h);
		IOVirtualAddress addr = gfb_start + rect.y * gfb_pitch + rect.x * bytes_per_pixel;
		if (rect.width * bytes_per_pixel == gfb_pitch) {
			memset32(reinterpret_cast<void*>(addr), color, rect.height * rect.width);
			continue;
		}
		for (int j = 0; j < rect.height; ++j) {
			memset32(reinterpret_cast<void*>(addr), color, rect.width);
			addr += gfb_pitch;
		}
	}
	return kIOReturnSuccess;
}
#endif

#pragma mark -
#pragma mark Misc Methods
#pragma mark -

HIDDEN
IOReturn CLASS::getScreenInfo(IOAccelSurfaceReadData* info)
{
	if (!info)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	bzero(info, sizeof *info);
	m_framebuffer->lockDevice();
	info->x = m_svga->getCurrentFBOffset();
	info->y = m_svga->getCurrentFBSize();
	info->w = m_svga->getCurrentWidth();
	info->h = m_svga->getCurrentHeight();
#if IOACCELTYPES_10_5 || (IOACCEL_TYPES_REV < 12 && !defined(kIODescriptionKey))
	info->client_addr = reinterpret_cast<void*>(m_vram_kernel_map->getVirtualAddress());
#else
	info->client_addr = m_vram_kernel_map->getAddress();
#endif
	info->client_row_bytes = m_svga->getCurrentPitch();
	m_framebuffer->unlockDevice();
	ACLog(2, "%s: width == %d, height == %d\n", __FUNCTION__, FMT_D(info->w), FMT_D(info->h));
	return kIOReturnSuccess;
}

HIDDEN
bool CLASS::retainMasterSurface()
{
	bool rc = true;
	uint32_t width, height;

	if (!bHaveSVGA3D)
		return false;
	if (__sync_fetch_and_add(&m_master_surface_retain_count, 1) == 0) {
		m_framebuffer->lockDevice();
		width = m_svga->getCurrentWidth();
		height = m_svga->getCurrentHeight();
		m_framebuffer->unlockDevice();
		ACLog(2, "%s: Master Surface width == %u, height == %u\n", __FUNCTION__, width, height);
		rc = createMasterSurface(width, height);
		if (!rc)
			m_master_surface_retain_count = 0;
	}
	return rc;
}

HIDDEN
void CLASS::releaseMasterSurface()
{
	if (!bHaveSVGA3D)
		return;
	int v = __sync_fetch_and_add(&m_master_surface_retain_count, -1);
	if (v <= 0) {
		m_master_surface_retain_count = 0;
		return;
	}
	if (v == 1)
		destroyMasterSurface();
}

HIDDEN
void CLASS::lockAccel()
{
	IOLockLock(m_iolock);
}

HIDDEN
void CLASS::unlockAccel()
{
	IOLockUnlock(m_iolock);
}

HIDDEN
IOMemoryDescriptor* CLASS::getChannelMemory() const
{
	IOService* provider = getProvider();
	if (!provider)
		return 0;
	return provider->getDeviceMemoryWithIndex(2U);
}

HIDDEN
uint32_t CLASS::getVRAMSize() const
{
	if (m_svga)
		return m_svga->getVRAMSize();
	if (m_vram_kernel_map)
		return static_cast<uint32_t>(m_vram_kernel_map->getLength());
	return 134217728U;
}

HIDDEN
vm_offset_t CLASS::offsetInVRAM(void* vram_ptr) const
{
	if (m_vram_kernel_map)
		return reinterpret_cast<vm_offset_t>(vram_ptr) - m_vram_kernel_map->getVirtualAddress();
	return 0U;
}

HIDDEN
bool CLASS::HaveGLBaseline() const
{
	return m_svga &&
		bHaveSVGA3D &&
		m_svga->HasCapability(SVGA_CAP_GMR);
}

HIDDEN
VMsvga2Surface* CLASS::findSurfaceForID(uint32_t surface_id)
{
	FindSurface fs;

	bzero(&fs, sizeof fs);
	fs.cgsSurfaceID = surface_id;
	messageClients(kIOMessageFindSurface, &fs, sizeof fs);
	return OSDynamicCast(VMsvga2Surface, fs.client);
}

IOReturn CLASS::claimSurfaceID(VMsvga2Surface* surface,uint32_t id)
{
	if(!surface || !m_accel_update_lock)return kIOReturnNotReady;
	// An already claimed unchanged identity cannot acquire a new collision.
	// Keep repeated native mode notifications out of registry iteration.
	if(surface->bHaveID && surface->m_wID==id)return kIOReturnSuccess;
	// Cold identity setup only. Serialize check+publish so two app clients
	// cannot shadow one another or a WindowServer surface with the same ID.
	IOLockLock(m_accel_update_lock);
	FindSurface found;bzero(&found,sizeof(found));found.cgsSurfaceID=id;
	messageClients(kIOMessageFindSurface,&found,sizeof(found));
	// Registry iteration pins clients only inside the callbacks. Copy the
	// admission metadata there; never dereference a returned raw client here.
	IOReturn error=found.client && found.client!=surface &&
		(found.privatePBuffer || surface->m_pbuffer_only)?kIOReturnExclusiveAccess:kIOReturnSuccess;
	if(!error){surface->m_wID=id;surface->bHaveID=true;}
	IOLockUnlock(m_accel_update_lock);return error;
}

HIDDEN
SVGA3D* CLASS::lock3D()
{
	if (!bHaveSVGA3D)
		return 0;
	m_framebuffer->lockDevice();
	return &svga3d;
}

HIDDEN
void CLASS::unlock3D()
{
	if (!m_framebuffer)
		return;
	m_framebuffer->unlockDevice();
}

/*
 * Note:
 *   The ptr.offset field in SVGAGuestImage is used to hold the limit
 *     relative to the base address of the blit area.
 *   The ptr.gmrId field in SVGAGuestImage is ignored.
 */
HIDDEN
IOReturn CLASS::genericBlitCopy(IOVirtualAddress dst_base,
								SVGAGuestImage const* dst_image,
								SVGASignedPoint const* dst_delta,
								IOVirtualAddress src_base,
								SVGAGuestImage const* src_image,
								SVGASignedPoint const* src_delta,
								void /* IOAccelDeviceRegion */ const* region,
								uint8_t bytes_per_pixel)
{
	uint32_t numRects;
	IOVirtualAddress dst_limit, src_limit;
	IOAccelDeviceRegion const* rgn;

	if (!dst_base || !dst_image || !dst_delta ||
		!src_base || !src_image || !src_delta)
		return kIOReturnBadArgument;
	rgn = static_cast<IOAccelDeviceRegion const*>(region);
	numRects = rgn ? rgn->num_rects : 0U;
	if (!numRects)
		return kIOReturnSuccess;
	dst_limit = dst_base + dst_image->ptr.offset;
	src_limit = src_base + src_image->ptr.offset;
	for (uint32_t i = 0U; i != numRects; ++i) {
		IOAccelBounds const* rect = &rgn->rect[i];
		if (rect->w <= 0 || rect->h <= 0)
			continue;
		IOVirtualAddress addr1 = dst_base + (rect->y + dst_delta->y) * dst_image->pitch + (rect->x + dst_delta->x) * bytes_per_pixel;
		IOVirtualAddress addr2 = src_base + (rect->y + src_delta->y) * src_image->pitch + (rect->x + src_delta->x) * bytes_per_pixel;
		size_t l = static_cast<size_t>(rect->w) * bytes_per_pixel;
		for (int16_t j = 0; j != rect->h; ++j) {
			if (addr1 < dst_base || addr1 + l > dst_limit ||
				addr2 < src_base || addr2 + l > src_limit)
				continue;
			memcpy(reinterpret_cast<void*>(addr1),
				   reinterpret_cast<void const*>(addr2),
				   l);
			addr1 += dst_image->pitch;
			addr2 += src_image->pitch;
		}
	}
	return kIOReturnSuccess;
}

HIDDEN
bool CLASS::isPrimaryScreenActive() const
{
	if (!bHaveScreenObject)
		return false;
	return static_cast<int>(m_primary_screen.w) > 0 && static_cast<int>(m_primary_screen.h) > 0;
}

#pragma mark -
#pragma mark Video Methods
#pragma mark -

HIDDEN
IOReturn CLASS::VideoSetRegsInRange(uint32_t streamId,
									struct SVGAOverlayUnit const* regs,
									uint32_t regMin,
									uint32_t regMax,
									uint32_t* fence)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	if (regs)
		m_svga->VideoSetRegsInRange(streamId, regs, regMin, regMax);
	m_svga->VideoFlush(streamId);
	if (fence)
		*fence = m_svga->InsertFence();
	m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::VideoSetReg(uint32_t streamId,
							uint32_t registerId,
							uint32_t value,
							uint32_t* fence)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	m_framebuffer->lockDevice();
	m_svga->VideoSetReg(streamId, registerId, value);
	m_svga->VideoFlush(streamId);
	if (fence)
		*fence = m_svga->InsertFence();
	m_svga->RingDoorBell();
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark ID Allocation Methods
#pragma mark -

HIDDEN
uint32_t CLASS::AllocSurfaceID()
{
	uint32_t r, t;
	uint32_t const num_entries = 4U;

	lockAccel();
	r = find_bit_in_array64(&m_surface_id_mask[m_surface_id_idx], num_entries - m_surface_id_idx);
	if (static_cast<int>(r) < 0) {
		r = (num_entries << 6) + (m_surface_ids_unmanaged++);
		m_surface_id_idx = num_entries;
	} else {
		t = r >> 6;
		r += (m_surface_id_idx << 6);
		m_surface_id_idx += t;
	}
	unlockAccel();
	return r;
}

HIDDEN
void CLASS::FreeSurfaceID(uint32_t sid)
{
	uint32_t r;
	uint32_t const num_entries = 4U;
	r = sid >> 6;
	if (r >= num_entries)
		return;
	lockAccel();
	m_surface_id_mask[r] &= ~(1ULL << (sid & 63U));
	if (r < m_surface_id_idx)
		m_surface_id_idx = r;
	unlockAccel();
}

HIDDEN
uint32_t CLASS::AllocContextID()
{
	uint32_t r;

	lockAccel();
	r = find_bit_in_array64(&m_context_id_mask, 1U);
	if (static_cast<int>(r) < 0)
		r = 8U * static_cast<uint32_t>(sizeof(uint64_t)) + m_context_ids_unmanaged++;
	unlockAccel();
	return r;
}

HIDDEN
void CLASS::FreeContextID(uint32_t cid)
{
	if (cid >= 8U * static_cast<uint32_t>(sizeof(uint64_t)))
		return;
	lockAccel();
	m_context_id_mask &= ~(1ULL << cid);
	unlockAccel();
}

HIDDEN
uint32_t CLASS::AllocStreamID()
{
	uint32_t r;

	lockAccel();
	r = find_bit_in_array32(&m_stream_id_mask, 1U);
	unlockAccel();
	return r;
}

HIDDEN
void CLASS::FreeStreamID(uint32_t streamId)
{
	if (streamId >= 8U * static_cast<uint32_t>(sizeof(uint32_t)))
		return;
	lockAccel();
	m_stream_id_mask &= ~(1U << streamId);
	unlockAccel();
}

HIDDEN
uint32_t CLASS::AllocGMRID()
{
	uint32_t r, t;
	uint32_t const num_entries = 2U;

	lockAccel();
	r = find_bit_in_array64(&m_gmr_id_mask[m_gmr_id_idx], num_entries - m_gmr_id_idx);
	if (static_cast<int>(r) < 0)
		m_gmr_id_idx = num_entries;
	else {
		t = r >> 6;
		r += (m_gmr_id_idx << 6);
		m_gmr_id_idx += t;
	}
	unlockAccel();
	return r;
}

HIDDEN
void CLASS::FreeGMRID(uint32_t gmrId)
{
	uint32_t r;
	uint32_t const num_entries = 2U;
	r = gmrId >> 6;
	if (r >= num_entries)
		return;
	lockAccel();
	m_gmr_id_mask[r] &= ~(1ULL << (gmrId & 63U));
	if (r < m_gmr_id_idx)
		m_gmr_id_idx = r;
	unlockAccel();
}

#pragma mark -
#pragma mark Memory Methods
#pragma mark -

HIDDEN
void* CLASS::VRAMMalloc(size_t bytes)
{
	IOReturn rc;
	void* p = 0;

	if (!m_allocator)
		return 0;
	lockAccel();
	rc = m_allocator->Malloc(bytes, &p);
	unlockAccel();
	if (rc != kIOReturnSuccess)
		ACLog(1, "%s(%lu) failed\n", __FUNCTION__, bytes);
	return p;
}

HIDDEN
void* CLASS::VRAMRealloc(void* ptr, size_t bytes)
{
	IOReturn rc;
	void* newp = 0;

	if (!m_allocator)
		return 0;
	lockAccel();
	rc = m_allocator->Realloc(ptr, bytes, &newp);
	unlockAccel();
	if (rc != kIOReturnSuccess)
		ACLog(1, "%s(%p, %lu) failed\n", __FUNCTION__, ptr, bytes);
	return newp;
}

HIDDEN
void CLASS::VRAMFree(void* ptr)
{
	IOReturn rc;

	if (!m_allocator)
		return;
	lockAccel();
	rc = m_allocator->Free(ptr);
	unlockAccel();
	if (rc != kIOReturnSuccess)
		ACLog(1, "%s(%p) failed\n", __FUNCTION__, ptr);
}

HIDDEN
IOMemoryMap* CLASS::mapVRAMRangeForTask(task_t task, vm_offset_t offset_in_vram, vm_size_t size)
{
	if (!m_vram)
		return 0;
	return m_vram->createMappingInTask(task, 0U, kIOMapAnywhere, offset_in_vram, size);
}

#pragma mark -
#pragma mark GMR Allocation
#pragma mark -

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#define getPhysicalSegment(x, y, z) getPhysicalSegment64(x, y)
#endif

HIDDEN
IOReturn CLASS::createGMR(uint32_t gmrId, IOMemoryDescriptor* md)
{
	IOBufferMemoryDescriptor* helper;
	addr64_t phys_addr;
	IOByteCount offset, length = 0U, helper_length = 0U;
	size_t const dpp = (PAGE_SIZE / sizeof(SVGAGuestMemDescriptor)) - 1U;
	size_t const max_bits = PAGE_SHIFT + 8U * sizeof(uint32_t);
	size_t num_physical_ranges, num_pages, next_page, in_page_count;
	IOVirtualAddress helper_base;
	SVGAGuestMemDescriptor* helper_ptr;
	uint32_t helper_base_ppn, helper_ppn;
	IOReturn rc;

	if (!md)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	if (!m_svga->HasCapability(SVGA_CAP_GMR))
		return kIOReturnUnsupported;
	num_physical_ranges = 0U;
	offset = 0U;
	while ((phys_addr = md->getPhysicalSegment(offset, &length, 0U))) {
		if (phys_addr >> max_bits)
			return kIOReturnUnsupported;
		++num_physical_ranges;
		offset += length;
	}
	if (!num_physical_ranges)
		return kIOReturnBadArgument;
	num_pages = (num_physical_ranges + (dpp - 1U)) / dpp;
	if (num_physical_ranges + num_pages > m_svga->getMaxGMRDescriptorLength())
		return kIOReturnNoResources;
	helper = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
															  kIODirectionInOut,
															  num_pages << PAGE_SHIFT,
															  ((1ULL << max_bits) - 1ULL) & -PAGE_SIZE);
	if (!helper)
		return kIOReturnNoResources;
	rc = helper->prepare();
	if (rc != kIOReturnSuccess) {
		helper->release();
		return rc;
	}
	helper_base = reinterpret_cast<IOVirtualAddress>(helper->getBytesNoCopy());
	helper_ptr = reinterpret_cast<SVGAGuestMemDescriptor*>(helper_base);
	bzero(helper_ptr, num_pages << PAGE_SHIFT);
	helper_ppn = static_cast<uint32_t>(helper->getPhysicalSegment(0U, &helper_length, 0U) >> PAGE_SHIFT);
	helper_base_ppn = helper_ppn;
	++helper_ppn;
	helper_length -= PAGE_SIZE;
	offset = 0U;
	next_page = 1U;
	in_page_count = dpp;
	while ((phys_addr = md->getPhysicalSegment(offset, &length, 0U))) {
		offset += length;
		length += static_cast<IOByteCount>(phys_addr & (PAGE_SIZE - 1U));
		length = (length + (PAGE_SIZE - 1U)) & -PAGE_SIZE;
		helper_ptr->ppn = static_cast<uint32_t>(phys_addr >> PAGE_SHIFT);
		helper_ptr->numPages = static_cast<uint32_t>(length >> PAGE_SHIFT);
		++helper_ptr;
		--in_page_count;
		if (in_page_count)
			continue;
		if (static_cast<intptr_t>(helper_length) > 0)
			;
		else if (next_page < num_pages) {
			helper_ppn = static_cast<uint32_t>(helper->getPhysicalSegment(next_page << PAGE_SHIFT, &helper_length, 0U) >> PAGE_SHIFT);
		} else
			break;
		helper_ptr->ppn = helper_ppn;
		++helper_ppn;
		helper_length -= PAGE_SIZE;
		helper_ptr = reinterpret_cast<SVGAGuestMemDescriptor*>(helper_base + next_page * PAGE_SIZE);
		++next_page;
		in_page_count = dpp;
	}
	m_framebuffer->lockDevice();
	m_svga->defineGMR(gmrId, helper_base_ppn);
	m_framebuffer->unlockDevice();
	helper->complete();
	helper->release();
	return kIOReturnSuccess;
}

HIDDEN
IOReturn CLASS::createGMR2(uint32_t gmrId, IOMemoryDescriptor* md)
{
	addr64_t phys_addr;
	IOByteCount offset, length = 0U;
	size_t const max_bits = PAGE_SHIFT + 8U * sizeof(uint32_t);
	size_t num_pages, list_size;
	uint32_t *ppn_list, *list_iter;

	if (!md)
		return kIOReturnBadArgument;
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	if (!m_svga->HasCapability(SVGA_CAP_GMR2))
		return kIOReturnUnsupported;
	num_pages = (md->getLength() + PAGE_SIZE - 1U) >> PAGE_SHIFT;		// Note: Assumes start offset is zero
	if (!num_pages)
		return kIOReturnBadArgument;
	/*
	 * Note: possible to account for actual number
	 *   of pages in GMRs, but then need to discount
	 *   them in destroyGMR2 as well.
	 */
	if (num_pages > m_svga->getMaxGMRPages())
		return kIOReturnUnsupported;
	list_size = num_pages * sizeof *ppn_list;
	ppn_list = static_cast<uint32_t*>(IOMalloc(list_size));
	if (!ppn_list)
		return kIOReturnNoMemory;
	offset = 0U;
	list_iter = ppn_list;
	while ((phys_addr = md->getPhysicalSegment(offset, &length, 0U))) {
		if (phys_addr >> max_bits) {
			IOFree(ppn_list, list_size);
			return kIOReturnUnsupported;	// Note: can support this by using PPN64
		}
		offset += length;
		length += static_cast<IOByteCount>(phys_addr & (PAGE_SIZE - 1U));
		length = (length + (PAGE_SIZE - 1U)) >> PAGE_SHIFT;
		phys_addr >>= PAGE_SHIFT;
		for (; length; --length) {
			*list_iter++ = static_cast<uint32_t>(phys_addr);
			++phys_addr;
		}
	}
	m_framebuffer->lockDevice();
	if (!m_svga->defineGMR2(gmrId, static_cast<uint32_t>(num_pages))) {
		m_framebuffer->unlockDevice();
		IOFree(ppn_list, list_size);
		return kIOReturnDeviceError;
	}
	m_svga->remapGMR2(gmrId, 0U, 0U, static_cast<uint32_t>(num_pages), ppn_list, list_size);
	m_framebuffer->unlockDevice();
	IOFree(ppn_list, list_size);
	return kIOReturnSuccess;
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#undef getPhysicalSegment
#endif

HIDDEN
IOReturn CLASS::destroyGMR(uint32_t gmrId)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	if (!m_svga->HasCapability(SVGA_CAP_GMR))
		return kIOReturnUnsupported;
	m_framebuffer->lockDevice();
	m_svga->defineGMR(gmrId, 0U);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}

IOReturn CLASS::destroyGMR2(uint32_t gmrId)
{
	if (!m_framebuffer)
		return kIOReturnNoDevice;
	if (!m_svga->HasCapability(SVGA_CAP_GMR2))
		return kIOReturnUnsupported;
	m_framebuffer->lockDevice();
	m_svga->defineGMR2(gmrId, 0U);
	m_framebuffer->unlockDevice();
	return kIOReturnSuccess;
}
