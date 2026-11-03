/*
 *  VMsvga2.cpp
 *  VMsvga2
 *
 *  Created by Zenith432 on July 2nd 2009.
 *  Copyright 2009-2012 Zenith432. All rights reserved.
 *
 */

/**********************************************************
 * Portions Copyright 2009 VMware, Inc.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************/

#include <stdarg.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/version.h>
#include "VMsvga2.h"
#include "vmw_options_fb.h"
#include "VLog.h"
#include "../vminclude/vim_framebuffer.h"
#include "../vminclude/vim_display_panel.h"
#include "../vminclude/vim_passthrough_opengl.h"

#include "svga_apple_header.h"
#include "svga_reg.h"
#include "svga_apple_footer.h"

#define CLASS VMsvga2
#define super IOFramebuffer
OSDefineMetaClassAndStructors(VMsvga2, IOFramebuffer);

#if LOGGING_LEVEL >= 1
#define LogPrintf(log_level, ...) do { if (log_level <= logLevelFB) VLog("IOFB: ", ##__VA_ARGS__); } while (false)
#else
#define LogPrintf(log_level, ...)
#endif

#define FMT_D(x) static_cast<int>(x)
#define FMT_U(x) static_cast<unsigned>(x)
#include "VMsvga2Panel.inc"
#include "VMsvga2VBL.inc"

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1056
/*
 * The availability of hotspot information in IOHardwareCursorInfo
 * was silently introduced in OS 10.5.6 without any versioning change.
 */
#define HAVE_CURSOR_HOTSPOT
#warning Building for OS 10.5.6 and later
#endif

#ifndef HAVE_CURSOR_HOTSPOT
#define	GetShmem(instance)	((StdFBShmem_t *)(instance->priv))
#endif

typedef unsigned long long __m64 __attribute__((vector_size(8), may_alias));

static __attribute__((used)) char const copyright[] = "Copyright 2009-2012 Zenith432";

static char const pixelFormatStrings[] = IO32BitDirectPixels "\0";

unsigned __attribute__((visibility("hidden"))) vmw_options_fb = 0U;

int __attribute__((visibility("hidden"))) logLevelFB = LOGGING_LEVEL;

#pragma mark -
#pragma mark Private Methods
#pragma mark -

void CLASS::publishFramebufferLayout()
{
	VIMFramebufferLayout layout;
	layout.version = VIM_FRAMEBUFFER_LAYOUT_VERSION;
	layout.width = svga.getCurrentWidth();
	layout.height = svga.getCurrentHeight();
	layout.row_bytes = svga.getCurrentPitch();
	layout.offset = svga.getCurrentFBOffset();
	layout.bits_per_pixel = 32;
	setProperty(VIM_FRAMEBUFFER_LAYOUT_KEY, &layout, sizeof layout);
}

__attribute__((visibility("hidden")))
void CLASS::Cleanup()
{
	cleanupVirtualVBL();
	cleanupNativePanelInterrupt();
	cancelRefreshTimer();
	if (m_restore_call)
		thread_call_cancel(m_restore_call);
	deleteRefreshTimer();
#if 0
	if (svga.HasCapability(0xFFFFFFFFU))
		svga.Disable();
#endif
	svga.Cleanup();
	if (m_restore_call) {
		thread_call_free(m_restore_call);
		m_restore_call = 0;
	}
	if (m_cursor_image) {
		IOFree(m_cursor_image, 16384U);
		m_cursor_image = 0;
	}
	if (m_edid) {
		IOFree(m_edid, m_edid_size);
		m_edid_size = 0U;
		m_edid = 0;
	}
	if (m_iolock) {
		IOLockFree(m_iolock);
		m_iolock = 0;
	}
}

__attribute__((visibility("hidden")))
uint32_t CLASS::FindDepthMode(IOIndex depth)
{
	return depth ? 0 : 32;
}

__attribute__((visibility("hidden")))
DisplayModeEntry const* CLASS::GetDisplayMode(IODisplayModeID displayMode)
{
	if (displayMode == CUSTOM_MODE_ID)
		return &customMode;
	if (displayMode >= 1 && displayMode <= NUM_DISPLAY_MODES)
		return &modeList[displayMode - 1];
	LogPrintf(1, "%s: Bad mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	return 0;
}

__attribute__((visibility("hidden")))
void CLASS::IOSelectToString(IOSelect io_select, char* output)
{
	*output = static_cast<char>(io_select >> 24);
	output[1] = static_cast<char>(io_select >> 16);
	output[2] = static_cast<char>(io_select >> 8);
	output[3] = static_cast<char>(io_select);
	output[4] = '\0';
}

__attribute__((visibility("hidden")))
IODisplayModeID CLASS::TryDetectCurrentDisplayMode(IODisplayModeID defaultMode) const
{
	IODisplayModeID tableDefault = 0;
	uint32_t w = svga.getCurrentWidth();
	uint32_t h = svga.getCurrentHeight();
	for (IODisplayModeID i = 1; i < NUM_DISPLAY_MODES; ++i) {
		if (w == modeList[i].width && h == modeList[i].height)
			return i + 1;
		if (modeList[i].flags & kDisplayModeDefaultFlag)
			tableDefault = i + 1;
	}
	return tableDefault ? : defaultMode;
}

#pragma mark -
#pragma mark Cursor Methods
#pragma mark -

IOReturn CLASS::setCursorState(SInt32 x, SInt32 y, bool visible)
{
	if (!checkOptionFB(VMW_OPTION_FB_FIFO_INIT))
		return kIOReturnUnsupported;
	LogPrintf(2, "%s: xy=%d %d visi=%d\n", __FUNCTION__, FMT_D(x), FMT_D(y), visible ? 1 : 0);
	x += m_hotspot_x;
	if (x < 0)
		x = 0;
	y += m_hotspot_y;
	if (y < 0)
		y = 0;
	IOLockLock(m_iolock);
	svga.setCursorState(
		static_cast<uint32_t>(x),
		static_cast<uint32_t>(y),
		visible);
	IOLockUnlock(m_iolock);
	return kIOReturnSuccess;
}

__attribute__((visibility("hidden"), always_inline))
static inline
__m64 _my_pinsrw3(__m64 a, int d)
{
	// The sole caller inserts alpha into lane3. Keep the instruction's
	// immediate explicit; newer kernel SDKs need not defer this constraint
	// until an ordinary integer function argument has been inlined.
	__asm__ volatile ("pinsrw %2, %1, %0" : "+y"(a) : "r"(d), "K"(3));
	return a;
}

__attribute__((visibility("hidden")))
void CLASS::ConvertAlphaCursor(uint32_t* cursor, uint32_t width, uint32_t height)
{
	/*
	 * Pre-multiply alpha cursor
	 */
#ifdef VECTORIZE
	static __m64 const datum = { 0x808180818081ULL };
	__m64 const mm_zero = { 0ULL };
	__m64 mm0;
	uint32_t alpha, num_pixels;
	for (num_pixels = width * height; num_pixels; --num_pixels, ++cursor) {
		alpha = (*cursor) >> 24U;
		if (!alpha || static_cast<uint8_t>(alpha) == 255U)
			continue;
		mm0 = __builtin_ia32_punpcklbw((__m64){*cursor}, mm_zero);
		*cursor = __builtin_ia32_vec_ext_v2si(
			__builtin_ia32_packuswb(
			_my_pinsrw3(
			__builtin_ia32_psrlwi(
			__builtin_ia32_pmulhuw(
			__builtin_ia32_pmullw(
			__builtin_ia32_pshufw(mm0, 255), mm0), datum), 7), alpha), mm_zero), 0);
	}
	__builtin_ia32_emms();
#else /* VECTORIZE */
	uint32_t num_pixels, pixel, alpha, r, g, b;
#if 0
	LogPrintf(2, "%s: %ux%u pixels @ %p\n", __FUNCTION__, width, height, cursor);
#endif
	for (num_pixels = width * height; num_pixels; --num_pixels, ++cursor) {
		pixel = *cursor;
		alpha = pixel >> 24;
		if (!alpha || static_cast<uint8_t>(alpha) == 255U)
			continue;
		b = (pixel & 0xFFU) * alpha / 255U;
		g = ((pixel >> 8) & 0xFFU) * alpha / 255U;
		r = ((pixel >> 16) & 0xFFU) * alpha / 255U;
		*cursor = (pixel & 0xFF000000U) | (r << 16) | (g << 8) | b;
	}
#endif /* VECTORIZE */
}

IOReturn CLASS::setCursorImage(void* cursorImage)
{
	void* device_cursor;
#ifndef HAVE_CURSOR_HOTSPOT
	static int once = 1;
	StdFBShmem_t *shmem;
#else
	uint16_t const* p_hotspots;
#endif
	IOHardwareCursorDescriptor curd;
	IOHardwareCursorInfo curi;

	if (!checkOptionFB(VMW_OPTION_FB_FIFO_INIT))
		return kIOReturnUnsupported;
	LogPrintf(2, "%s: cursor wxh=%dx%d\n", __FUNCTION__, 64, 64);
	if (!m_cursor_image) {
		m_cursor_image = IOMalloc(16384U);
		if (!m_cursor_image) {
			LogPrintf(1, "%s: out of memory on cacheCursorMemory\n", __FUNCTION__);
			return kIOReturnUnsupported;
		}
	}
	m_hotspot_x = 0;
	m_hotspot_y = 0;
	bzero(&curd, sizeof curd);
	curd.majorVersion = kHardwareCursorDescriptorMajorVersion;
	curd.minorVersion = kHardwareCursorDescriptorMinorVersion;
	curd.width = 64U;
	curd.height = 64U;
	curd.bitDepth = 32U;
	curd.supportedSpecialEncodings = kInvertingEncodedPixel;
	curd.specialEncodings[kInvertingEncoding] = 0xFF000000U;
	bzero(&curi, sizeof curi);
	curi.majorVersion = kHardwareCursorInfoMajorVersion;
	curi.minorVersion = kHardwareCursorInfoMinorVersion;
	curi.hardwareCursorData = static_cast<uint8_t*>(m_cursor_image);
#ifdef HAVE_CURSOR_HOTSPOT
	p_hotspots = reinterpret_cast<uint16_t const*>(&curi.hardwareCursorData + 1);
#else
	shmem = GetShmem(this);
	if (shmem) {
		if (shmem->version == kIOFBTenPtTwoShmemVersion) {
			unsigned u = (cursorImage != 0 ? 1U : 0U);
			m_hotspot_x = shmem->hotSpot[u].x;
			m_hotspot_y = shmem->hotSpot[u].y;
#if 0
			if (shmem->cursorSize[u].width < 64)
				curd.width = shmem->cursorSize[u].width;
			if (shmem->cursorSize[u].height < 64)
				curd.height = shmem->cursorSize[u].height;
#endif
		} else if (once) {
			LogPrintf(1, "%s: Unknown version %d.\n", __FUNCTION__, shmem->version);
			once = 0;
		}
	}
#endif
	LogPrintf(3, "%s: cursor %p: desc %ux%u @ %u\n", __FUNCTION__,
			  curi.hardwareCursorData, FMT_U(curd.width), FMT_U(curd.height), FMT_U(curd.bitDepth));
	if (!convertCursorImage(cursorImage, &curd, &curi)) {
		LogPrintf(1, "%s: convertCursorImage() failed\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	if (!curi.cursorWidth || !curi.cursorHeight) {
		LogPrintf(1, "%s: convertCursorImage() returned bad size %ux%u", __FUNCTION__,
				  FMT_U(curi.cursorWidth), FMT_U(curi.cursorHeight));
		return kIOReturnUnsupported;
	}
	LogPrintf(3, "%s: cursor %p: info %ux%u\n", __FUNCTION__,
			  curi.hardwareCursorData, FMT_U(curi.cursorWidth), FMT_U(curi.cursorHeight));
#ifdef HAVE_CURSOR_HOTSPOT
	LogPrintf(3, "%s: hotspots: %d vs %d (x), %d vs %d (y)\n", __FUNCTION__,			// Added
			  m_hotspot_x, FMT_D(*p_hotspots), m_hotspot_y, FMT_D(p_hotspots[1]));
	m_hotspot_x = *p_hotspots;
	m_hotspot_y = p_hotspots[1];
#endif
	ConvertAlphaCursor(reinterpret_cast<uint32_t*>(curi.hardwareCursorData),
					   curi.cursorWidth,
					   curi.cursorHeight);
	IOLockLock(m_iolock);
	device_cursor = svga.BeginDefineAlphaCursor(curi.cursorWidth, curi.cursorHeight, 4U);
	if (!device_cursor) {
		IOLockUnlock(m_iolock);
		LogPrintf(1, "%s: BeginDefineAlphaCursor() failed\n", __FUNCTION__);
		return kIOReturnUnsupported;
	}
	memcpy(device_cursor, curi.hardwareCursorData, curi.cursorWidth * curi.cursorHeight * 4U);
	svga.EndDefineAlphaCursor(
		curi.cursorWidth,
		curi.cursorHeight,
		4U,
		m_hotspot_x,
		m_hotspot_y);
	IOLockUnlock(m_iolock);
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark IOFramebuffer Methods
#pragma mark -

UInt64 CLASS::getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	return 0ULL;
}

IOReturn CLASS::setInterruptState(void* interruptRef, UInt32 state)
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	if(interruptRef==&m_vbl_intr){
		if(!m_vbl_loop || !m_vbl_timer)return kIOReturnNotReady;
		return m_vbl_loop->runAction(&CLASS::virtualVBLState,this,
			reinterpret_cast<void*>(static_cast<uintptr_t>(state!=0)));
	}
	if (interruptRef != &m_intr)
		return kIOReturnBadArgument;
	m_intr_enabled = (state != 0);
	enableNativePanelInterrupt(m_intr_enabled);
	return kIOReturnSuccess /* kIOReturnUnsupported */;
}

IOReturn CLASS::unregisterInterrupt(void* interruptRef)
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	if(interruptRef==&m_vbl_intr){cleanupVirtualVBL();return kIOReturnSuccess;}
	if (interruptRef != &m_intr)
		return kIOReturnBadArgument;
	bzero(interruptRef, sizeof m_intr);
	m_intr_enabled = false;
	enableNativePanelInterrupt(false);
	return kIOReturnSuccess;
}

IOItemCount CLASS::getConnectionCount()
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	return 1U;
}

IOReturn CLASS::getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth)
{
	if (displayMode)
		*displayMode = m_display_mode;
	if (depth)
		*depth = m_depth_mode;
	LogPrintf(2, "%s: display mode ID=%d, depth mode ID=%d\n", __FUNCTION__,
			  FMT_D(m_display_mode), FMT_D(m_depth_mode));
	return kIOReturnSuccess;
}

IOReturn CLASS::getDisplayModes(IODisplayModeID* allDisplayModes)
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	if (!allDisplayModes)
		return kIOReturnBadArgument;
	if (m_custom_switch) {
		*allDisplayModes = CUSTOM_MODE_ID;
		return kIOReturnSuccess;
	}
	memcpy(allDisplayModes, &m_modes[0], m_num_active_modes * sizeof(IODisplayModeID));
	return kIOReturnSuccess;
}

IOItemCount CLASS::getDisplayModeCount()
{
	IOItemCount r;
	r = m_custom_switch ? 1 : m_num_active_modes;
	LogPrintf(3, "%s: mode count=%u\n", __FUNCTION__, FMT_U(r));
	return r;
}

const char* CLASS::getPixelFormats()
{
	LogPrintf(2, "%s: pixel formats=%s\n", __FUNCTION__, &pixelFormatStrings[0]);
	return &pixelFormatStrings[0];
}

IODeviceMemory* CLASS::getVRAMRange()
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	if (!m_vram)
		return 0;
	if (svga.getVRAMSize() >= m_vram->getLength()) {
		m_vram->retain();
		return m_vram;
	}
	return IODeviceMemory::withSubRange(m_vram, 0U, svga.getVRAMSize());
}

IODeviceMemory* CLASS::getApertureRange(IOPixelAperture aperture)
{
	uint32_t fb_offset, fb_size;
	IODeviceMemory* mem;

	if (aperture != kIOFBSystemAperture) {
		LogPrintf(1, "%s: Failed request for aperture=%d (%d)\n", __FUNCTION__,
				  FMT_D(aperture), kIOFBSystemAperture);
		return 0;
	}
	if (!m_vram)
		return 0;
	IOLockLock(m_iolock);
	fb_offset = svga.getCurrentFBOffset();
	fb_size = svga.getCurrentFBSize();
	IOLockUnlock(m_iolock);
	LogPrintf(2, "%s: aperture=%d, fb offset=%u, fb size=%u\n", __FUNCTION__,
			  FMT_D(aperture), fb_offset, fb_size);
	mem = IODeviceMemory::withSubRange(m_vram, fb_offset, fb_size);
	if (!mem)
		LogPrintf(1, "%s: Failed to create IODeviceMemory, aperture=%d\n", __FUNCTION__, kIOFBSystemAperture);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
	// Tiger registers the kernel connect callback before IOFBOpen installs its
	// userspace interest notification. An early IRQ then leaves suspended=1,
	// messaged=0 forever: there was no listener for that first edge. A server
	// aperture mapping follows IOFBOpen's subscription/mode enumeration; the
	// earlier kernel-only open has no serverConnect. Arm the pending request
	// at this real handoff, without delaying boot or polling for WindowServer.
	if (mem && version_major == 8 && serverConnect && m_panel_interrupt &&
		!captured && !m_panel_uncaptured) {
		m_panel_uncaptured = true;
		setProperty("VIMNativePanelReady",kOSBooleanTrue);
		enableNativePanelInterrupt(m_intr_enabled);
	}
#endif
	return mem;
}

bool CLASS::isConsoleDevice()
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	// IOService explicitly permits no primary provider while detached. The
	// Mavericks framebuffer-open path can ask here during that transition;
	// absence of a boot-display provider means non-console, not a kernel fault.
	IOService* provider = getProvider();
	return provider && 0 != provider->getProperty("AAPL,boot-display");
}

IOReturn CLASS::getAttribute(IOSelect attribute, uintptr_t* value)
{
	IOReturn r;
	char attr[5];

	/*
	 * Also called from base class:
	 *   kIOMirrorDefaultAttribute
	 *   kIOVRAMSaveAttribute
	 *   kIOClamshellStateAttribute
	 */
	if (attribute == kIOHardwareCursorAttribute) {
		if (value)
			*value = checkOptionFB(VMW_OPTION_FB_FIFO_INIT) ? 1U : 0U;
		r = kIOReturnSuccess;
	} else
		r = super::getAttribute(attribute, value);
	if (logLevelFB >= 2) {
		IOSelectToString(attribute, &attr[0]);
		if (value)
			LogPrintf(2, "%s: attr=%s *value=%#08lx ret=%#08x\n", __FUNCTION__, &attr[0], *value, r);
		else
			LogPrintf(2, "%s: attr=%s ret=%#08x\n", __FUNCTION__, &attr[0], r);
	}
	return r;
}

IOReturn CLASS::getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t* value)
{
	IOReturn r;
	char attr[5];

	/*
	 * Also called from base class:
	 *   kConnectionCheckEnable
	 */
	switch (attribute) {
		case kConnectionSupportsAppleSense:
		case kConnectionDisplayParameterCount:
		case kConnectionSupportsLLDDCSense:
		case kConnectionDisplayParameters:
		case kConnectionPower:
		case kConnectionPostWake:
			r = kIOReturnUnsupported;
			break;
		case kConnectionChanged:
			LogPrintf(2, "%s: kConnectionChanged value=%s\n", __FUNCTION__,
					  value ? "non-NULL" : "NULL");
			if (value)
				removeProperty("IOFBConfig");
			r = kIOReturnSuccess;
			break;
		case kConnectionEnable:
			LogPrintf(2, "%s: kConnectionEnable\n", __FUNCTION__);
			if (value)
				*value = 1U;
			r = kIOReturnSuccess;
			break;
		case kConnectionFlags:
			LogPrintf(2, "%s: kConnectionFlags\n", __FUNCTION__);
			if (value)
				*value = m_panel_mode ? kIOConnectionBuiltIn : 0U;
			r = kIOReturnSuccess;
			break;
		case kConnectionSupportsHLDDCSense:
			r = m_edid ? kIOReturnSuccess : kIOReturnUnsupported;
			break;
		default:
			r = super::getAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	if (logLevelFB >= 2) {
		IOSelectToString(attribute, &attr[0]);
		if (value)
			LogPrintf(2, "%s: index=%d, attr=%s *value=%#08lx ret=%#08x\n", __FUNCTION__,
					  FMT_D(connectIndex), &attr[0], *value, r);
		else
			LogPrintf(2, "%s: index=%d, attr=%s ret=%#08x\n", __FUNCTION__,
					  FMT_D(connectIndex), &attr[0], r);
	}
	return r;
}

IOReturn CLASS::setAttribute(IOSelect attribute, uintptr_t value)
{
	IOReturn r;
	char attr[5];

	r = super::setAttribute(attribute, value);
	if (logLevelFB >= 2) {
		IOSelectToString(attribute, &attr[0]);
		LogPrintf(2, "%s: attr=%s value=%#08lx ret=%#08x\n",
				  __FUNCTION__, &attr[0], value, r);
	}
	if (attribute == kIOCapturedAttribute &&
		!value &&
		m_custom_switch == 1U &&
		m_display_mode == CUSTOM_MODE_ID) {
		CustomSwitchStepSet(keepCustomPanelMode() ? 0U : 2U);
	}
	if (attribute==kIOCapturedAttribute && m_panel_interrupt) {
		m_panel_uncaptured=value==0;
		enableNativePanelInterrupt(m_intr_enabled);
		setProperty("VIMNativePanelReady",m_panel_uncaptured?kOSBooleanTrue:kOSBooleanFalse);
	}
	return r;
}

IOReturn CLASS::setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t value)
{
	IOReturn r;
	char attr[5];

	/*
	 * Also called from base class:
	 *   kConnectionColorModesSupported
	 *   kConnectionColorDepthsSupported
	 *   kConnectionDisplayFlags
	 */
	switch (attribute) {
		case kConnectionFlags:
			LogPrintf(2, "%s: kConnectionFlags %lu\n", __FUNCTION__, value);
			r = kIOReturnSuccess;
			break;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
		case kConnectionProbe:
			LogPrintf(2, "%s: kConnectionProbe %lu\n", __FUNCTION__, value);
			r = kIOReturnSuccess;
			break;
#endif
		default:
			r = super::setAttributeForConnection(connectIndex, attribute, value);
			break;
	}
	if (logLevelFB >= 2) {
		IOSelectToString(attribute, &attr[0]);
		LogPrintf(2, "%s: index=%d, attr=%s value=%#08lx ret=%#08x\n", __FUNCTION__,
				  FMT_D(connectIndex), &attr[0], value, r);
	}
	return r;
}

IOReturn CLASS::registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc, OSObject* target, void* ref, void** interruptRef)
{
	char int_type[5];

	if (logLevelFB >= 2) {
		IOSelectToString(interruptType, &int_type[0]);
		LogPrintf(2, "%s: interruptType=%s\n", __FUNCTION__, &int_type[0]);
	}
	/*
	 * Also called from base class:
	 *   kIOFBVBLInterruptType
	 *   kIOFBDisplayPortInterruptType
	 */
	if (interruptType == kIOFBVBLInterruptType)
		return registerVirtualVBL(proc,target,ref,interruptRef);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	if (interruptType == kIOFBMCCSInterruptType)
		return super::registerForInterruptType(interruptType, proc, target, ref, interruptRef);
#endif
	if (interruptType != kIOFBConnectInterruptType)
		return kIOReturnUnsupported;
	bzero(&m_intr, sizeof m_intr);
	m_intr.target = target;
	m_intr.ref = ref;
	m_intr.proc = proc;
	m_intr_enabled = true;
	if (version_major >= 9 && version_major <= 12) {
		// Leopard through Mountain Lion start uncaptured and only emit capture changes
		// when that state changes. There need not be an initial capt=0 call.
		// Read the target SDK's real protected IOFramebuffer state when its
		// connection callback is registered, rather than waiting forever for
		// Mavericks' later uncapture handoff. Subsequent capt changes retain
		// the normal suppression/reenable path; no polling or guessed offset.
		m_panel_uncaptured = !captured;
		setProperty("VIMNativePanelReady",m_panel_uncaptured?kOSBooleanTrue:kOSBooleanFalse);
	}
	enableNativePanelInterrupt(true);
	if (interruptRef)
		*interruptRef = &m_intr;
	return kIOReturnSuccess;
}

IOReturn CLASS::connectFlags(IOIndex connectIndex, IODisplayModeID displayMode, IOOptionBits* flags)
{
	if (!m_panel_mode)
		return super::connectFlags(connectIndex, displayMode, flags);
	if (connectIndex || !flags)
		return kIOReturnBadArgument;
	const DisplayModeEntry* mode = GetDisplayMode(displayMode);
	if (!mode)
		return kIOReturnUnsupportedMode;
	// IOGraphics-471.92.1 replaces getInformationForDisplayMode's safety
	// bits with this result. Inheriting Unsupported loses the default bit:
	// IODisplay then supplies only Valid | Safe for every mode.
	*flags = (mode->flags & kDisplayModeSafetyFlags) & ~kDisplayModeDefaultFlag;
	if (displayMode == m_panel_mode)
		*flags |= kDisplayModeDefaultFlag;
	return kIOReturnSuccess;
}

IOReturn CLASS::getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation* info)
{
	DisplayModeEntry const* dme;

	LogPrintf(2, "%s: mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	if (!info)
		return kIOReturnBadArgument;
	dme = GetDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: Display mode %d not found.\n", __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	bzero(info, sizeof(IODisplayModeInformation));
	info->maxDepthIndex = 0;
	info->nominalWidth = dme->width;
	info->nominalHeight = dme->height;
	info->refreshRate = 60U << 16;
	info->flags = dme->flags;
	if (m_panel_mode) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1070
		// Nominal 110 dpi per UI scale: native IOGraphics (>150 dpi) and
		// CoreGraphics (>160 dpi) recognize 2x from real mode metadata.
		info->imageWidth=static_cast<UInt16>((m_panel_width*254+550*m_panel_scale)/(1100*m_panel_scale));
		info->imageHeight=static_cast<UInt16>((m_panel_height*254+550*m_panel_scale)/(1100*m_panel_scale));
#endif
		info->flags&=~kDisplayModeDefaultFlag;
		// This virtual panel has a fixed driver-owned native preset, not a
		// sensed external timing. Matching IOKit otherwise revalidates the
		// EDID-less preset and drops its default bit during mode-list build.
		if (displayMode==m_panel_mode)info->flags|=kDisplayModeDefaultFlag|kDisplayModeBuiltInFlag;
		// Native 11G63 MPIndexToMode/MPModeDescription already consume this
		// exact bit, although Apple's older 10.7 SDK omits its declaration.
		// It becomes the public kDisplayModeValidForHiResFlag in the 10.8
		// headers. Keep the same mode metadata on every supported guest.
		const UInt32 validForHiResFlag=0x00800000;
		if (displayMode==CUSTOM_MODE_ID && m_panel_scale==2)
			info->flags|=validForHiResFlag;
	}
	LogPrintf(2, "%s: mode ID=%d, max depth=%d, wxh=%ux%u, flags=%#x\n", __FUNCTION__,
			  FMT_D(displayMode), 0, FMT_U(info->nominalWidth), FMT_U(info->nominalHeight), FMT_U(info->flags));
	return kIOReturnSuccess;
}

IOReturn CLASS::getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation* pixelInfo)
{
	DisplayModeEntry const* dme;

	LogPrintf(2, "%s: mode ID=%d\n", __FUNCTION__, FMT_D(displayMode));
	if (!pixelInfo)
		return kIOReturnBadArgument;
	if (aperture != kIOFBSystemAperture) {
		LogPrintf(1, "%s: aperture=%d not supported\n", __FUNCTION__, FMT_D(aperture));
		return kIOReturnUnsupportedMode;
	}
	if (depth) {
		LogPrintf(1, "%s: Depth mode %d not found.\n", __FUNCTION__, FMT_D(depth));
		return kIOReturnBadArgument;
	}
	dme = GetDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: Display mode %d not found.\n", __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	LogPrintf(2, "%s: mode ID=%d, wxh=%ux%u\n", __FUNCTION__,
			  FMT_D(displayMode), dme->width, dme->height);
	bzero(pixelInfo, sizeof(IOPixelInformation));
	pixelInfo->activeWidth = dme->width;
	pixelInfo->activeHeight = dme->height;
	pixelInfo->flags = dme->flags;
	strlcpy(&pixelInfo->pixelFormat[0], &pixelFormatStrings[0], sizeof(IOPixelEncoding));
	pixelInfo->pixelType = kIORGBDirectPixels;
	pixelInfo->componentMasks[0] = 0xFF0000U;
	pixelInfo->componentMasks[1] = 0x00FF00U;
	pixelInfo->componentMasks[2] = 0x0000FFU;
	pixelInfo->bitsPerPixel = 32U;
	pixelInfo->componentCount = 3U;
	pixelInfo->bitsPerComponent = 8U;
	// Match SetMode's negotiated PITCHLOCK, including for queried modes that
	// are not active yet. Old QEMU panels remain packed; native VMware keeps
	// the driver's original padded layout.
	bool packedPanel = m_panel_mode && !svga.HasCapability(SVGA_CAP_PITCHLOCK);
	pixelInfo->bytesPerRow = (packedPanel ? pixelInfo->activeWidth :
		((pixelInfo->activeWidth + 7U) & (~7U))) << 2;
	LogPrintf(2, "%s: bitsPerPixel=%u, bytesPerRow=%u\n", __FUNCTION__, 32U, FMT_U(pixelInfo->bytesPerRow));
	return kIOReturnSuccess;
}

IOReturn CLASS::setDisplayMode(IODisplayModeID displayMode, IOIndex depth)
{
	DisplayModeEntry const* dme;

	LogPrintf(2, "%s: display ID=%d, depth ID=%d\n", __FUNCTION__,
			  FMT_D(displayMode), FMT_D(depth));
	if (depth) {
		LogPrintf(1, "%s: Depth mode %d not found.\n", __FUNCTION__, FMT_D(depth));
		return kIOReturnBadArgument;
	}
	dme = GetDisplayMode(displayMode);
	if (!dme) {
		LogPrintf(1, "%s: Display mode %d not found.\n", __FUNCTION__, FMT_D(displayMode));
		return kIOReturnBadArgument;
	}
	if (m_custom_mode_switched) {
		if (customMode.width == dme->width && customMode.height == dme->height)
			m_custom_mode_switched = false;
		else
			LogPrintf(3, "%s: Not setting mode in virtual hardware\n", __FUNCTION__);
		m_display_mode = displayMode;
		m_depth_mode = 0;
		return kIOReturnSuccess;
	}
#if 0
	/*
	 * VMwareGfx 5.x
	 */
	LogPrintf(0, "%s: (%u) wxh=%ux%u, %u %u\n", __FUNCTION__,
			  displayMode, dme->width, dme->height, 32U, ((dme->width * 4U) + 28U) & 0x1FFFFFE0U);
#endif
	if (!m_accel_updates)
		cancelRefreshTimer();	// Added
	IOLockLock(m_iolock);
	svga.SetMode(dme->width, dme->height, 32U);
	publishFramebufferLayout();
	if (checkOptionFB(VMW_OPTION_FB_REG_DUMP))	// Added
		svga.RegDump();						// Added
	IOLockUnlock(m_iolock);
	m_display_mode = displayMode;
	m_depth_mode = 0;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
	// Darwin8 applies the new mode without the later capt=0 transaction.
	// Its native IOGraphics1.4.7 trace reaches this successful hardware-mode
	// commit but never setAttribute(kIOCapturedAttribute). Acknowledge that
	// actual completion, not a timer or merely publishing a requested mode.
	if (version_major == 8 && m_custom_switch == 1U && displayMode == CUSTOM_MODE_ID)
		CustomSwitchStepSet(2U);
#endif
	LogPrintf(2, "%s: display mode ID=%d, depth mode ID=%d\n", __FUNCTION__,
			  FMT_D(m_display_mode), FMT_D(m_depth_mode));
	if (!m_accel_updates)
		scheduleRefreshTimer(200U /* m_refresh_quantum_ms */);	// Added
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark DDC/EDID Injection Support Methods
#pragma mark -

IOReturn CLASS::getDDCBlock(IOIndex connectIndex, UInt32 blockNumber, IOSelect blockType, IOOptionBits options, UInt8* data, IOByteCount* length)
{
	uint32_t const oneBlock = 128U;
	if (connectIndex == 0 &&
		m_edid &&
		blockNumber &&
		(blockNumber * oneBlock) <= m_edid_size &&
		blockType == kIODDCBlockTypeEDID &&
		data &&
		length &&
		*length >= oneBlock) {
		memcpy(data, &m_edid[(blockNumber - 1U) * oneBlock], oneBlock);
		*length = oneBlock;
		return kIOReturnSuccess;
	}
	return super::getDDCBlock(connectIndex, blockNumber, blockType, options, data, length);
}

bool CLASS::hasDDCConnect(IOIndex connectIndex)
{
	if (connectIndex == 0 && m_edid)
		return true;
	return super::hasDDCConnect(connectIndex);
}

#pragma mark -
#pragma mark Custom Mode Methods
#pragma mark -

bool CLASS::keepCustomPanelMode() const
{
	// The synthetic panel keeps the chosen custom mode in its mode list.
	// The old second hotplug can restore the original desktop geometry
	// while deliberately leaving the hardware in the new scanout mode.
	return version_major >= 13 || (version_major >= 11 && m_panel_mode &&
		svga.getPrimaryScanoutBytes() > SVGA_FB_MAX_TRACEABLE_SIZE);
}

__attribute__((visibility("hidden")))
void CLASS::CustomSwitchStepWait(uint32_t value)
{
	LogPrintf(2, "%s: value=%u.\n", __FUNCTION__, value);
	while (m_custom_switch != value) {
		if (assert_wait(&m_custom_switch, THREAD_UNINT) != THREAD_WAITING)
			continue;
		if (m_custom_switch == value)
			thread_wakeup(&m_custom_switch);
		thread_block(0);
	}
	LogPrintf(2, "%s: done waiting.\n", __FUNCTION__);
}

__attribute__((visibility("hidden")))
void CLASS::CustomSwitchStepSet(uint32_t value)
{
	LogPrintf(2, "%s: value=%u.\n", __FUNCTION__, value);
	m_custom_switch = value;
	thread_wakeup(&m_custom_switch);
}

__attribute__((visibility("hidden")))
void CLASS::EmitConnectChangedEvent()
{
#if 0	/* VMwareGfx 5.x */
	while (!m_intr.proc) {
		LogPrintf(3, "%s: Waiting for WindowServer.\n", __FUNCTION__);
		IOSleep(1000);
	}
	if (!m_intr_enabled)
		return;
#else
	if (!m_intr.proc || !m_intr_enabled)
		return;
#endif
	LogPrintf(3, "%s: Before call.\n", __FUNCTION__);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
	LogPrintf(3, "%s: Tiger capture=%u pending=%u suspended=%u changes=%u messaged=%u.\n",
		__FUNCTION__, captured, configPending, suspended, connectChange, messaged);
#endif
	m_intr.proc(m_intr.target, m_intr.ref);
	LogPrintf(3, "%s: After call.\n", __FUNCTION__);
}

__attribute__((visibility("hidden")))
void CLASS::RestoreAllModes()
{
	uint32_t i;
	IODisplayModeID t;
	DisplayModeEntry const* dme1;
	DisplayModeEntry const* dme2 = 0;

	if (m_custom_switch != 2U) {
		LogPrintf(3, "%s: The user client set another custom resolution (%d)\n", __FUNCTION__, m_custom_switch);
		return;
	}

	dme1 = GetDisplayMode(CUSTOM_MODE_ID);
	if (!dme1)
		return;
	for (i = 0U; i != m_num_active_modes; ++i) {
		dme2 = GetDisplayMode(m_modes[i]);
		if (!dme2)
			continue;
		if (dme2->width != dme1->width || dme2->height != dme1->height)
			goto found_slot;
	}
	return;
found_slot:
	t = m_modes[0];
	m_modes[0] = m_modes[i];
	m_modes[i] = t;
	LogPrintf(3, "%s: Swapped mode IDs in slots 0 and %u.\n", __FUNCTION__, i);
	m_custom_mode_switched = true;
	CustomSwitchStepSet(0U);
	EmitConnectChangedEvent();
}

__attribute__((visibility("hidden")))
void CLASS::_RestoreAllModes(thread_call_param_t param0, thread_call_param_t param1)
{
	static_cast<CLASS*>(param0)->RestoreAllModes();
}

IOReturn CLASS::CustomMode(CustomModeData const* inData, CustomModeData* outData, size_t inSize, size_t* outSize)
{
	DisplayModeEntry const* dme1;
	unsigned w, h;
	uint64_t deadline;

	if (!m_restore_call)
		return kIOReturnUnsupported;
	LogPrintf(2, "%s: inData=%p outData=%p inSize=%lu outSize=%lu.\n", __FUNCTION__,
			  inData, outData, inSize, outSize ? *outSize : 0UL);
	if (!inData) {
		LogPrintf(1, "%s: inData NULL.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (inSize < sizeof(CustomModeData)) {
		LogPrintf(1, "%s: inSize bad.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (!outData) {
		LogPrintf(1, "%s: outData NULL.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	if (!outSize || *outSize < sizeof(CustomModeData)) {
		LogPrintf(1, "%s: *outSize bad.\n", __FUNCTION__);
		return kIOReturnBadArgument;
	}
	dme1 = GetDisplayMode(m_display_mode);
	if (!dme1)
		return kIOReturnUnsupported;
	if (inData->flags & 1U) {
		LogPrintf(3, "%s: Requested custom resolution %ux%u.\n", __FUNCTION__, inData->width, inData->height);
		w = inData->width;
		if (w < 800U)
			w = 800U;
		else if (w > svga.getMaxWidth())
			w = svga.getMaxWidth();
		h = inData->height;
		if (h < 600U)
			h = 600U;
		else if (h > svga.getMaxHeight())
			h = svga.getMaxHeight();
#if 0	/* VMwareGfx 5.x */
		if (((w + 7U) & -8) * h > (1U << 22)) {
			/*
			 * finds max new_w:new_h with same aspect ratio as w:h
			 * such that new_w * new_h <= 1U << 22.
			 */
			uint64_t U; uint32_t M;
			U = (uint64_t) w * (1ULL << 22) / h;
			/* find max M such that (uint64_t) M * M <= U; */
			new_w = M & -8;
			new_h = (new_u * h / w) & -2;
			w = new_w; h = new_h;
		}
#endif
		if (w == dme1->width && h == dme1->height)
#if 0	/* VMwareGfx 5.x */
			LogPrintf(3, "%s: Set resolution to %ux%u already set\n", __FUNCTION__, w, h);
#endif
			goto finish_up;
		customMode.width = w;
		customMode.height = h;
#if 0	/* VMwareGfx 5.x */
		LogPrintf(3, "s: Setting custom resolution to %ux%u.\n", __FUNCTION__, w, h);
#endif
		CustomSwitchStepSet(1U);
		EmitConnectChangedEvent();
		if (keepCustomPanelMode()) {
			CustomSwitchStepWait(0);
			goto finish_up;
		}
		CustomSwitchStepWait(2U);	// TBD: this wait for the WindowServer should be time-bounded
		/*
		 * Following log eliminated in VMwareGfx 5.x
		 */
		LogPrintf(3, "%s: Scheduling RestoreAllModes().\n", __FUNCTION__);
		clock_interval_to_deadline(2000U, kMillisecondScale, &deadline);
		thread_call_enter_delayed(m_restore_call, deadline);
	}
finish_up:
	dme1 = GetDisplayMode(m_display_mode);
	if (!dme1)
		return kIOReturnUnsupported;
	outData->flags = inData->flags;
	outData->width = dme1->width;
	outData->height = dme1->height;
	return kIOReturnSuccess;
}

#pragma mark -
#pragma mark Refresh Timer Methods
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::refreshTimerAction()
{
	IOLockLock(m_iolock);
	// Recovery may keep the firmware's VGA mode until IOGraphics selects an
	// SVGA mode. QEMU does not consume SVGA FIFO commands with ENABLE clear.
	// Repeated timer UPDATEs then fill the ring and FIFOReserve spins while
	// holding this lock, blocking mode changes and the desktop. Do not produce
	// SVGA work while VGA owns scanout. Existing VGA dirty tracking still draws
	// that screen; resume ordinary updates as soon as SetMode enables SVGA.
	// This check is only the fallback refresh timer, not the accelerated path.
	if (svga.ReadReg(SVGA_REG_ENABLE) & SVGA_REG_ENABLE_ENABLE) {
		svga.UpdateFullscreen();
		svga.RingDoorBell();
	}
	IOLockUnlock(m_iolock);
	if (!m_accel_updates)
		scheduleRefreshTimer();
}

__attribute__((visibility("hidden")))
void CLASS::_RefreshTimerAction(thread_call_param_t param0, thread_call_param_t param1)
{
	static_cast<CLASS*>(param0)->refreshTimerAction();
}

__attribute__((visibility("hidden")))
void CLASS::scheduleRefreshTimer(uint32_t milliSeconds)
{
	uint64_t deadline;

	if (m_refresh_call) {
		clock_interval_to_deadline(milliSeconds, kMillisecondScale, &deadline);
		thread_call_enter_delayed(m_refresh_call, deadline);
	}
}

__attribute__((visibility("hidden")))
void CLASS::scheduleRefreshTimer()
{
	scheduleRefreshTimer(m_refresh_quantum_ms);
}

__attribute__((visibility("hidden")))
void CLASS::cancelRefreshTimer()
{
	if (m_refresh_call)
		thread_call_cancel(m_refresh_call);
}

__attribute__((visibility("hidden")))
void CLASS::setupRefreshTimer()
{
	m_refresh_call = thread_call_allocate(&_RefreshTimerAction, this);
}

__attribute__((visibility("hidden")))
void CLASS::deleteRefreshTimer()
{
	if (m_refresh_call) {
		thread_call_free(m_refresh_call);
		m_refresh_call = 0;
	}
}

#pragma mark -
#pragma mark IOService Methods
#pragma mark -

bool CLASS::start(IOService* provider)
{
	uint32_t boot_arg, max_w, max_h;
	OSString* o_name;	// Added
	OSData* o_edid;		// Added

	if (!OSDynamicCast(IOPCIDevice, provider))
		return false;
	m_vbl_loop=0;m_vbl_timer=0;m_vbl_enabled=false;
	m_vbl_period=m_vbl_deadline=0;bzero(&m_vbl_intr,sizeof m_vbl_intr);
	if (!super::start(provider)) {
		LogPrintf(1, "%s: super::start failed.\n", __FUNCTION__);
		return false;
	}
	IOLog("IOFB: start\n");
	VMLog_SendString("log IOFB: start\n");
	if (PE_parse_boot_argn("vmw_log_fb", &boot_arg, sizeof boot_arg))
		logLevelFB = static_cast<int>(boot_arg);
	/*
	 * Begin Added
	 */
	setProperty("VMwareSVGAFBLogLevel", static_cast<uint64_t>(logLevelFB), 32U);
	vmw_options_fb = VMW_OPTION_FB_FIFO_INIT | VMW_OPTION_FB_REFRESH_TIMER | VMW_OPTION_FB_ACCEL;
	if (PE_parse_boot_argn("vmw_options_fb", &boot_arg, sizeof boot_arg))
		vmw_options_fb = boot_arg;
	setProperty("VMwareSVGAFBOptions", static_cast<uint64_t>(vmw_options_fb), 32U);
	m_refresh_quantum_ms = 50U;
	if (PE_parse_boot_argn("vmw_fps", &boot_arg, sizeof boot_arg) &&
		boot_arg && boot_arg <= 100U)
		m_refresh_quantum_ms = 1000U / boot_arg;
	setProperty("VMwareSVGARefreshQuantumMS", static_cast<uint64_t>(m_refresh_quantum_ms), 32U);
	o_edid = OSDynamicCast(OSData, getProperty("EDID"));
	max_w = o_edid ? o_edid->getLength() : 0U;
	if (max_w && max_w <= 256U) {
		m_edid_size = (max_w + 127U) & -128;
		m_edid = static_cast<uint8_t*>(IOMalloc(m_edid_size));
		if (m_edid) {
			memcpy(m_edid, o_edid->getBytesNoCopy(), max_w);
			bzero(&m_edid[max_w], m_edid_size - max_w);
		} else
			m_edid_size = 0U;
	}
	o_name = OSDynamicCast(OSString, getProperty("IOHardwareModel"));
	if (o_name) {
		o_edid = OSData::withBytes(o_name->getCStringNoCopy(), o_name->getLength() + 1);
		if (o_edid) {
			provider->setProperty(gIODTModelKey, o_edid);
			o_edid->release();
		}
	}
	/*
	 * End Added
	 */
	m_restore_call = 0;
	m_iolock = 0;
	m_cursor_image = 0;
	/*
	 * Begin Added
	 */
	m_refresh_call = 0;
	m_intr_enabled = false;
	m_accel_updates = false;
	m_panel_mode=0;m_panel_width=m_panel_height=m_panel_scale=0;
	m_panel_loop=0;m_panel_interrupt=0;
	m_panel_dispatch_gate=0;m_panel_resize_loop=0;m_panel_resize_event=0;
	m_panel_pending_width=m_panel_pending_height=0;
	m_panel_pending_scale=0;
	m_backing_interrupt_supported=false;m_backing_interrupt_owner=0;m_backing_interrupt_action=0;
	m_panel_generation=0;
	m_panel_uncaptured=false;
	/*
	 * End Added
	 */
	m_vram = provider->getDeviceMemoryWithIndex(1U);
	svga.Init();
	if (!svga.Start(static_cast<IOPCIDevice*>(provider)))
		goto fail;
	/*
	 * Begin Added
	 */
	if (checkOptionFB(VMW_OPTION_FB_REG_DUMP))
		svga.RegDump();
	memcpy(&customMode, &modeList[0], sizeof(DisplayModeEntry));
	/*
	 * End Added
	 */
	max_w = svga.getMaxWidth();
	max_h = svga.getMaxHeight();
	m_num_active_modes = 0U;
	/*
	 * Note: VMwareGfx 5.x also checks that
	 *   ((modeList[i].width + 7U) & -8) * modeList[i].height <= (1U << 22)
	 *   {limiting to GFB area <= 16MiB}
	 */
	for (uint32_t i = 0U; i != NUM_DISPLAY_MODES; ++i)
		if (modeList[i].width <= max_w &&
			modeList[i].height <= max_h)
			m_modes[m_num_active_modes++] = i + 1U;
	LogPrintf(3, "%s: Display mode count = %u: %ux%u.\n", __FUNCTION__, m_num_active_modes, max_w, max_h);
	if (m_num_active_modes <= 2U) {
		LogPrintf(1, "%s: Display mode count invalid %u.\n", __FUNCTION__, m_num_active_modes);
		goto fail;
	}
	configureNativePanel(max_w,max_h);
	m_restore_call = thread_call_allocate(&_RestoreAllModes, this);
	if (!m_restore_call) {
		LogPrintf(1, "%s: Failed to allocate timer.\n", __FUNCTION__);
#if 0	// Note: not a critical error
		goto fail;
#endif
	}
	m_custom_switch = 0U;
	m_custom_mode_switched = false;
	if (checkOptionFB(VMW_OPTION_FB_FIFO_INIT)) {	// Added
		if (!svga.FIFOInit()) {
			LogPrintf(1, "%s: Failed FIFOInit.\n", __FUNCTION__);
			goto fail;
		}
		if (!svga.HasCapability(SVGA_CAP_TRACES) && checkOptionFB(VMW_OPTION_FB_REFRESH_TIMER))	// Added
			setupRefreshTimer();																// Added
	}
	m_iolock = IOLockAlloc();
	if (!m_iolock) {
		LogPrintf(1, "%s: Failed to allocate the FIFO mutex.\n", __FUNCTION__);
		goto fail;
	}
	m_display_mode = TryDetectCurrentDisplayMode(3);
	m_depth_mode = 0;
	publishFramebufferLayout();
	setupNativePanelInterrupt(provider);
	scheduleRefreshTimer(1000U /* m_refresh_quantum_ms */);		// Added
	return true;

fail:
	Cleanup();
	super::stop(provider);
	return false;
}

void CLASS::stop(IOService* provider)
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	Cleanup();
	super::stop(provider);
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
/*
 * Note: this method of impersonating VMware's driver(s)
 *   doesn't work on OS 10.6, since Apple have made
 *   passiveMatch() private.  It's virtual only in
 *   the 32-bit kernel.
 *
 * There's an alternative way to impersonate by creating
 *   a fake meta-class for classnames VMwareIOFramebuffer/VMwareGfx
 *   but this involves too much messing around with
 *   IOKit internals.
 */
extern "C" bool _ZN9IOService12passiveMatchEP12OSDictionaryb(IOService*, OSDictionary*, bool);
bool CLASS::passiveMatch(OSDictionary* matching, bool changesOK)
{
	OSString* str;
	if (!matching)
		goto done;
	str = OSDynamicCast(OSString, matching->getObject(gIOProviderClassKey));
	if (!str)
		goto done;
	if (str->isEqualTo("VMwareIOFramebuffer") || str->isEqualTo("VMwareGfx")) {
		LogPrintf(2, "%s: matched against VMwareIOFramebuffer/VMwareGfx\n", __FUNCTION__);
		str = OSString::withCString(getMetaClass()->getClassName());
		if (!str)
			goto done;
		matching->setObject(gIOProviderClassKey, str);
		str->release();
		goto done;
	}
done:
	return _ZN9IOService12passiveMatchEP12OSDictionaryb(this, matching, changesOK);
}
#endif

#pragma mark -
#pragma mark Accelerator Support Methods
#pragma mark -

void CLASS::lockDevice()
{
	IOLockLock(m_iolock);
}

void CLASS::unlockDevice()
{
	IOLockUnlock(m_iolock);
}

bool CLASS::supportsAccel()
{
	return checkOptionFB(VMW_OPTION_FB_FIFO_INIT) && checkOptionFB(VMW_OPTION_FB_ACCEL);
}

void CLASS::useAccelUpdates(bool state)
{
	if (state == m_accel_updates)
		return;
	m_accel_updates = state;
	if (state) {
		cancelRefreshTimer();
		IOLockLock(m_iolock);
		// A matching firmware mode need not trigger setDisplayMode. Claim it
		// when acceleration first owns updates, otherwise QEMU stays in VGA
		// dirty scanout and observes partially written CPU framebuffer rows.
		// Recovery without an accelerated client retains firmware ownership.
		if (!(svga.ReadReg(SVGA_REG_ENABLE) & SVGA_REG_ENABLE_ENABLE) &&
			svga.ReadReg(VIM_GL_FRAMEBUFFER_BLIT_CAPABILITIES_REGISTER) == VIM_GL_FRAMEBUFFER_BLIT_MAGIC) {
			svga.SetMode(svga.getCurrentWidth(), svga.getCurrentHeight(), 32U);
			publishFramebufferLayout();
		}
		if (svga.HasCapability(SVGA_CAP_TRACES))
			svga.WriteReg(SVGA_REG_TRACES, 0U);
		IOLockUnlock(m_iolock);
	} else {
		scheduleRefreshTimer(200U);
		IOLockLock(m_iolock);
		if (svga.HasCapability(SVGA_CAP_TRACES)) {
			svga.WriteReg(SVGA_REG_TRACES, 1U);
			svga.UpdateFullscreen();
			svga.RingDoorBell();
		}
		IOLockUnlock(m_iolock);
	}
	setProperty("VMwareSVGAAccelSynchronize", state);
	LogPrintf(2, "Accelerator Assisted Updates: %s\n", state ? "On" : "Off");
}
