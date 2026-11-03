/*
 *  SVGADevice.cpp
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

#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include "SVGADevice.h"
#include "common_fb.h"
#include "vmw_options_fb.h"
#include "VLog.h"
#include "../vminclude/vim_passthrough_opengl.h"
#include "../vminclude/vim_display_panel.h"

#include "svga_apple_header.h"
#include "svga_reg.h"
#include "svga_overlay.h"
#include "svga_escape.h"
#include "svga_apple_footer.h"

#pragma mark -
#pragma mark Macros
#pragma mark -

#define CLASS SVGADevice

#define BOUNCE_BUFFER_SIZE 0x10000U

#ifdef REQUIRE_TRACING
#warning Building for Fusion Host/Mac OS X Server Guest
#endif

#if LOGGING_LEVEL >= 1
#define LogPrintf(log_level, ...) do { if (log_level <= logLevelFB) VLog("SVGADev: ", ##__VA_ARGS__); } while (false)
#else
#define LogPrintf(log_level, ...)
#endif

#define TO_BYTE_PTR(x) reinterpret_cast<uint8_t*>(const_cast<uint32_t*>(x))
#define HasFencePassedUnguarded(fifo, fence) (static_cast<int32_t>(fifo[SVGA_FIFO_FENCE] - fence) >= 0)

#pragma mark -
#pragma mark Static Functions
#pragma mark -

OS_INLINE uint32_t count_bits(uint32_t mask)
{
	mask = ((mask & 0xAAAAAAAAU) >> 1) + (mask & 0x55555555U);
	mask = ((mask & 0xCCCCCCCCU) >> 2) + (mask & 0x33333333U);
	mask = ((mask & 0xF0F0F0F0U) >> 4) + (mask & 0x0F0F0F0FU);
	mask = ((mask & 0xFF00FF00U) >> 8) + (mask & 0x00FF00FFU);
	return ((mask & 0xFFFF0000U) >> 16) + (mask & 0x0000FFFFU);
}

#pragma mark -
#pragma mark Private Methods
#pragma mark -

__attribute__((visibility("hidden")))
void CLASS::FIFOFull()
{
	WriteReg(SVGA_REG_SYNC, 1);
	ReadReg(SVGA_REG_BUSY);
}

#pragma mark -
#pragma mark Public Methods
#pragma mark -

bool CLASS::Init()
{
	LogPrintf(2, "%s: \n", __FUNCTION__);
	m_provider = 0;
#if 0
	m_bar0_map = 0;
#endif
	m_bar2_map = 0;
	m_fifo_ptr = 0;
	m_cursor_ptr = 0;
	m_bounce_buffer = 0;
	m_next_fence = 1;
	m_capabilities = 0;
	return true;
}

/*
 * Note: GCC produces incorrect code when inlining ReadReg & WriteReg
 *   due to mishandling register-clobber in __asm__.
 */
__attribute__((noinline))
uint32_t CLASS::ReadReg(uint32_t index)
{
	__asm__ volatile ( "outl %0, %1" : : "a"(index), "d"(static_cast<uint16_t>(m_io_base + SVGA_INDEX_PORT)) );
	__asm__ volatile ( "inl %1, %0" : "=a"(index) : "d"(static_cast<uint16_t>(m_io_base + SVGA_VALUE_PORT)) );
	return index;
}

bool CLASS::NativePanelInterruptPending()
{
	uint32_t status;
	__asm__ volatile ( "inl %1, %0" : "=a"(status) : "d"(static_cast<uint16_t>(m_io_base + SVGA_IRQSTATUS_PORT)) );
	return status == 1;
}
bool CLASS::NativeBackingInterruptPending()
{
    uint32_t status;
    __asm__ volatile ("inl %1, %0" : "=a"(status) :
        "d"(static_cast<uint16_t>(m_io_base+VIM_GL_NOTIFY_STATUS_PORT)) : "memory");
    return status==1;
}
void CLASS::EnableNativeBackingInterrupt(bool enabled)
{
    uint32_t value=enabled?1:0;
    __asm__ volatile ("outl %0, %1" : : "a"(value),
        "d"(static_cast<uint16_t>(m_io_base+VIM_GL_NOTIFY_ENABLE_PORT)) : "memory");
}

__attribute__((noinline))
void CLASS::WriteReg(uint32_t index, uint32_t value)
{
	__asm__ volatile ( "outl %0, %1" : : "a"(index), "d"(static_cast<uint16_t>(m_io_base + SVGA_INDEX_PORT)) );
	__asm__ volatile ( "outl %0, %1" : : "a"(value), "d"(static_cast<uint16_t>(m_io_base + SVGA_VALUE_PORT)) );
}

__attribute__((noinline))
void CLASS::WriteDirectGLDoorbell(uint32_t offset)
{
    // Caller serializes its packet lifetime. Indexed/geometry users also
    // retain the ordinary device lock; the dedicated completion queue needs
    // only its own packet lock because this port never touches that latch.
    // The
    // memory clobber keeps packet stores before, and result reads after,
    // the synchronous I/O exit. No shared register-index update is needed.
    __asm__ volatile ("outl %0, %1" : : "a"(offset),
        "d"(static_cast<uint16_t>(m_io_base+VIM_GL_DOORBELL_PORT)) : "memory");
}

void CLASS::Cleanup()
{
	if (m_provider)
		m_provider = 0;
#if 0
	if (m_bar0_map) {
		m_bar0_map->release();
		m_bar0_map = 0;
	}
#endif
	if (m_bar2_map) {
		m_bar2_map->release();
		m_bar2_map = 0;
	}
	m_fifo_ptr = 0;
	if (m_bounce_buffer) {
		IOFree(m_bounce_buffer, BOUNCE_BUFFER_SIZE);
		m_bounce_buffer = 0;
	}
	m_capabilities = 0;
}

bool CLASS::Start(IOPCIDevice* provider)
{
	uint32_t host_bpp, guest_bpp, reg_id;
	IODeviceMemory* bar;

	LogPrintf(2, "%s: \n", __FUNCTION__);
	m_provider = provider;
	m_bounce_buffer = 0;
	if (logLevelFB >= 3) {
		LogPrintf(3, "%s: PCI bus %u device %u function %u\n", __FUNCTION__,
				  provider->getBusNumber(),
				  provider->getDeviceNumber(),
				  provider->getFunctionNumber());
		LogPrintf(3, "%s: PCI device %#04x vendor %#04x revision %#02x\n", __FUNCTION__,
				  provider->configRead16(kIOPCIConfigDeviceID),
				  provider->configRead16(kIOPCIConfigVendorID),
				  provider->configRead8(kIOPCIConfigRevisionID));
		LogPrintf(3, "%s: PCI subsystem %#04x vendor %#04x\n", __FUNCTION__,
				  provider->configRead16(kIOPCIConfigSubSystemID),
				  provider->configRead16(kIOPCIConfigSubSystemVendorID));
	}
	provider->setMemoryEnable(true);
	provider->setIOEnable(true);
	bar = provider->getDeviceMemoryWithIndex(0U);
	if (!bar) {
		LogPrintf(1, "%s: Failed to map the I/O registers.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_io_base = static_cast<uint16_t>(bar->getPhysicalAddress());
#if 0	/* VMwareGfx 5.x */
	WriteReg(SVGA_REG_ENABLE, 0U);
#endif
	WriteReg(SVGA_REG_ID, SVGA_ID_2);
	reg_id = ReadReg(SVGA_REG_ID);
	LogPrintf(3, "%s: REG_ID=%#08x\n", __FUNCTION__, reg_id);
	if (reg_id != SVGA_ID_2) {
		LogPrintf(1, "%s: REG_ID != %#08lx\n", __FUNCTION__, SVGA_ID_2);
		Cleanup();
		return false;
	}
#if 0	/* VMwareGfx 5.x */
	WriteReg(SVGA_REG_CONFIG_DONE, 0U);
	WriteReg(SVGA_REG_ENABLE, 1U);
#endif
	m_capabilities = ReadReg(SVGA_REG_CAPABILITIES);
	LogPrintf(3, "%s: caps=%#08x\n", __FUNCTION__, m_capabilities);
#ifdef REQUIRE_TRACING
	if (!HasCapability(SVGA_CAP_TRACES)) {
		LogPrintf(1, "%s: CAP_TRACES failed\n", __FUNCTION__);
		Cleanup();
		return false;
	}
#endif
#if 0
	/*
	 * VMwareGfx 5.x moved here from FIFOInit()
	 */
	if (!HasCapability(SVGA_CAP_EXTENDED_FIFO)) {
		LogPrintf(1, "%s: CAP_EXTENDED_FIFO failed\n", __FUNCTION__);
		Cleanup();
		return false;
	}
#endif
	m_fifo_size = ReadReg(SVGA_REG_MEM_SIZE);
	bar = provider->getDeviceMemoryWithIndex(2U);
	if (!bar) {
	bar2_error:
		LogPrintf(1, "%s: Failed to map the FIFO.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_bar2_map = bar->createMappingInTask(kernel_task, 0U, kIOMapAnywhere, 0U, m_fifo_size);
	if (!m_bar2_map)
		goto bar2_error;
	m_fifo_ptr = reinterpret_cast<uint32_t*>(m_bar2_map->getVirtualAddress());
#if 0
	/*
	 * VMwareGfx 5.x
	 */
	if (!m_fifo_ptr) {
		LogPrintf(1, "%s: Failed to get VA for FIFO.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
#endif
	m_fb_offset = ReadReg(SVGA_REG_FB_OFFSET);
	m_fb_size = ReadReg(SVGA_REG_FB_SIZE);
	m_vram_size = ReadReg(SVGA_REG_VRAM_SIZE);
	m_max_width = ReadReg(SVGA_REG_MAX_WIDTH);
	m_max_height = ReadReg(SVGA_REG_MAX_HEIGHT);
	host_bpp = ReadReg(SVGA_REG_HOST_BITS_PER_PIXEL);
	guest_bpp = ReadReg(SVGA_REG_BITS_PER_PIXEL);
	m_width = ReadReg(SVGA_REG_WIDTH);
	m_height = ReadReg(SVGA_REG_HEIGHT);
	m_pitch = ReadReg(SVGA_REG_BYTES_PER_LINE);
	if (HasCapability(SVGA_CAP_GMR)) {
		m_max_gmr_ids = ReadReg(SVGA_REG_GMR_MAX_IDS);
		m_max_gmr_descriptor_length = ReadReg(SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH);
	}
	if (HasCapability(SVGA_CAP_GMR2)) {
		m_max_gmr_pages = ReadReg(SVGA_REG_GMRS_MAX_PAGES);
		m_total_memory = ReadReg(SVGA_REG_MEMORY_SIZE);
	}
	/*
	 * Note: in VMwareGfx 5.x logLevel of following printouts is 0
	 *   Additionally, num_displays is printed after bpp, from ReadReg(SVGA_REG_NUM_DISPLAYS)
	 */
	LogPrintf(3, "%s: SVGA max w, h=%u, %u : host_bpp=%u, bpp=%u\n", __FUNCTION__, m_max_width, m_max_height, host_bpp, guest_bpp);
	LogPrintf(3, "%s: SVGA VRAM size=%u FB size=%u, FIFO size=%u\n", __FUNCTION__, m_vram_size, m_fb_size, m_fifo_size);
	if (HasCapability(SVGA_CAP_GMR))
		LogPrintf(3, "%s: SVGA max GMR IDs == %u, max GMR descriptor length == %u\n", __FUNCTION__, m_max_gmr_ids, m_max_gmr_descriptor_length);
	if (HasCapability(SVGA_CAP_GMR2))
		LogPrintf(3, "%s: SVGA max GMR Pages == %u, total dedicated device memory == %u\n", __FUNCTION__,
				  m_max_gmr_pages, m_total_memory);
#if 0	/* VMwareGfx 5.x */
	WriteReg(SVGA_REG_DISPLAY_ID, 0U);
	if (!FIFOInit()) {
		LogPrintf(1, "%s: Failed FIFOInit.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
#endif
	if (HasCapability(SVGA_CAP_TRACES))
		WriteReg(SVGA_REG_TRACES, 1);
#if 0
	/*
	 * VMwareGfx 5.x
	 */
	SetMode(1024U, 768U, 32U);
#endif
	m_bounce_buffer = static_cast<uint8_t*>(IOMalloc(BOUNCE_BUFFER_SIZE));
	if (!m_bounce_buffer) {
		LogPrintf(1, "%s: Failed to allocate the bounce buffer.\n", __FUNCTION__);
		Cleanup();
		return false;
	}
	m_cursor_ptr = 0;
	provider->setProperty("VMwareSVGACapabilities", static_cast<uint64_t>(m_capabilities), 32U);
	return true;
}

void CLASS::Disable()
{
	WriteReg(SVGA_REG_ENABLE, 0);
}

#pragma mark -
#pragma mark FIFO Methods
#pragma mark -

bool CLASS::IsFIFORegValid(uint32_t reg) const
{
	return m_fifo_ptr[SVGA_FIFO_MIN] > (reg << 2);
}

bool CLASS::HasFIFOCap(uint32_t mask) const
{
	// In the original four-register FIFO, word four is command data, not caps.
	return HasCapability(SVGA_CAP_EXTENDED_FIFO) &&
		(m_fifo_ptr[SVGA_FIFO_CAPABILITIES] & mask) != 0;
}

bool CLASS::FIFOInit()
{
	uint32_t fifo_capabilities;

	LogPrintf(2, "%s: FIFO: min=%u, size=%u\n", __FUNCTION__,
			  static_cast<unsigned>(SVGA_FIFO_NUM_REGS * sizeof(uint32_t)), m_fifo_size);
	/*
	 * Moved to Start() in VMwareGfx 5.x
	 */
	bool extended = HasCapability(SVGA_CAP_EXTENDED_FIFO);
	// QEMU implements the original FIFO. Reserve/commit already has a
	// non-reservable bounce-buffer path; use it rather than inventing caps.
	m_fifo_ptr[SVGA_FIFO_MIN] = static_cast<uint32_t>((extended ? SVGA_FIFO_NUM_REGS : 4U) * sizeof(uint32_t));
	m_fifo_ptr[SVGA_FIFO_MAX] = m_fifo_size;
	m_fifo_ptr[SVGA_FIFO_NEXT_CMD] = m_fifo_ptr[SVGA_FIFO_MIN];
	m_fifo_ptr[SVGA_FIFO_STOP] = m_fifo_ptr[SVGA_FIFO_MIN];
	WriteReg(SVGA_REG_CONFIG_DONE, 1);
	fifo_capabilities = extended ? m_fifo_ptr[SVGA_FIFO_CAPABILITIES] : 0;
	m_provider->setProperty("VMwareSVGAFIFOCapabilities", static_cast<uint64_t>(fifo_capabilities), 32U);
	LogPrintf(1, "%s: %s FIFO, capabilities=%#x\n", __FUNCTION__,
			  extended ? "extended" : "basic", fifo_capabilities);
	m_reserved_size = 0;
	m_using_bounce_buffer = false;
	return true;
}

void* CLASS::FIFOReserve(size_t bytes)
{
	uint32_t volatile* fifo = m_fifo_ptr;
	uint32_t max = fifo[SVGA_FIFO_MAX];
	uint32_t min = fifo[SVGA_FIFO_MIN];
	uint32_t next_cmd = fifo[SVGA_FIFO_NEXT_CMD];
	bool reservable = HasFIFOCap(SVGA_FIFO_CAP_RESERVE);

	if (bytes > BOUNCE_BUFFER_SIZE ||
		bytes > (max - min)) {
		LogPrintf(1, "FIFO command too large %lu > %u or (%u - %u)\n",
			bytes, BOUNCE_BUFFER_SIZE, max, min);
		return 0;
	}
	if (bytes % sizeof(uint32_t)) {
		LogPrintf(1, "FIFO command length not 32-bit aligned %lu\n", bytes);
		return 0;
	}
	if (m_reserved_size) {
		LogPrintf(1, "FIFOReserve before FIFOCommit, reservedSize=%lu\n", m_reserved_size);
		return 0;
	}
	m_reserved_size = bytes;
	while (true) {
		uint32_t stop = fifo[SVGA_FIFO_STOP];
		bool reserve_in_place = false;
		bool need_bounce = false;
		if (next_cmd >= stop) {
			if (next_cmd + bytes < max ||
				(next_cmd + bytes == max && stop > min)) {
				reserve_in_place = true;
			} else if ((max - next_cmd) + (stop - min) <= bytes) {
				FIFOFull();
			} else {
				need_bounce = true;
			}
		} else {
			if (next_cmd + bytes < stop) {
				reserve_in_place = true;
			} else {
				FIFOFull();
			}
		}
		if (reserve_in_place) {
			if (reservable || bytes <= sizeof(uint32_t)) {
				m_using_bounce_buffer = false;
				if (reservable) {
					fifo[SVGA_FIFO_RESERVED] = static_cast<uint32_t>(bytes);
				}
				return TO_BYTE_PTR(fifo) + next_cmd;
			} else {
				need_bounce = true;
			}
		}
		if (need_bounce) {
			m_using_bounce_buffer = true;
			return m_bounce_buffer;
		}
	}
}

void* CLASS::FIFOReserveCmd(uint32_t type, size_t bytes)
{
	uint32_t* cmd = static_cast<uint32_t*>(FIFOReserve(bytes + sizeof type));
	if (!cmd)
		return 0;
	*cmd++ = type;
	return cmd;
}

void* CLASS::FIFOReserveEscape(uint32_t nsid, size_t bytes)
{
	size_t padded_bytes = (bytes + 3UL) & ~3UL;
	uint32_t* header = static_cast<uint32_t*>(FIFOReserve(padded_bytes + 3U * sizeof(uint32_t)));
	if (!header)
		return 0;
	*header = SVGA_CMD_ESCAPE;
	header[1] = nsid;
	header[2] = static_cast<uint32_t>(bytes);
	return header + 3;
}

void CLASS::FIFOCommit(size_t bytes)
{
	uint32_t volatile* fifo = m_fifo_ptr;
	uint32_t next_cmd = fifo[SVGA_FIFO_NEXT_CMD];
	uint32_t max = fifo[SVGA_FIFO_MAX];
	uint32_t min = fifo[SVGA_FIFO_MIN];
	bool reservable = HasFIFOCap(SVGA_FIFO_CAP_RESERVE);

	if (bytes % sizeof(uint32_t)) {
		LogPrintf(1, "FIFO command length not 32-bit aligned %lu\n", bytes);
		return;
	}
	if (!m_reserved_size) {
		LogPrintf(1, "FIFOCommit before FIFOReserve, reservedSize == 0\n");
		return;
	}
	m_reserved_size = 0;
	if (m_using_bounce_buffer) {
		uint8_t* buffer = m_bounce_buffer;
		if (reservable) {
			uint32_t chunk_size = max - next_cmd;
			if (bytes < chunk_size)
				chunk_size = static_cast<uint32_t>(bytes);
			fifo[SVGA_FIFO_RESERVED] = static_cast<uint32_t>(bytes);
			memcpy(TO_BYTE_PTR(fifo) + next_cmd, buffer, chunk_size);
			memcpy(TO_BYTE_PTR(fifo) + min, buffer + chunk_size, bytes - chunk_size);
		} else {
			uint32_t* dword = reinterpret_cast<uint32_t*>(buffer);
			while (bytes) {
				fifo[next_cmd / static_cast<uint32_t>(sizeof *dword)] = *dword++;
				next_cmd += static_cast<uint32_t>(sizeof *dword);
				if (next_cmd == max)
					next_cmd = min;
				fifo[SVGA_FIFO_NEXT_CMD] = next_cmd;
				bytes -= sizeof *dword;
			}
		}
	}
	if (!m_using_bounce_buffer || reservable) {
		next_cmd += static_cast<uint32_t>(bytes);
		if (next_cmd >= max)
			next_cmd -= (max - min);
		fifo[SVGA_FIFO_NEXT_CMD] = next_cmd;
	}
	if (reservable)
		fifo[SVGA_FIFO_RESERVED] = 0;
}

void CLASS::FIFOCommitAll()
{
	LogPrintf(2, "%s: reservedSize=%lu\n", __FUNCTION__, m_reserved_size);
	FIFOCommit(m_reserved_size);
}

#pragma mark -
#pragma mark Fence Methods
#pragma mark -

uint32_t CLASS::InsertFence()
{
	uint32_t fence;
	uint32_t* cmd;

	if (!HasFIFOCap(SVGA_FIFO_CAP_FENCE))
		return 1;
	if (!m_next_fence)
		m_next_fence = 1;
	fence = m_next_fence++;
	cmd = static_cast<uint32_t*>(FIFOReserve(2U * sizeof(uint32_t)));
	if (!cmd)
		return 0;
	*cmd = SVGA_CMD_FENCE;
	cmd[1] = fence;
	FIFOCommitAll();
	return fence;
}

bool CLASS::HasFencePassed(uint32_t fence) const
{
	if (!fence)
		return true;
	if (!HasFIFOCap(SVGA_FIFO_CAP_FENCE))
		return false;
	return HasFencePassedUnguarded(m_fifo_ptr, fence);
}

void CLASS::SyncToFence(uint32_t fence)
{
	uint32_t volatile* fifo = m_fifo_ptr;

	if (!fence)
		return;
	if (!HasFIFOCap(SVGA_FIFO_CAP_FENCE)) {
		WriteReg(SVGA_REG_SYNC, 1);
		while (ReadReg(SVGA_REG_BUSY)) ;
		return;
	}
	if (HasFencePassedUnguarded(fifo, fence))
		return;
	WriteReg(SVGA_REG_SYNC, 1);
	while (!HasFencePassedUnguarded(fifo, fence)) {
		if (ReadReg(SVGA_REG_BUSY))
			continue;
		if (!HasFencePassedUnguarded(fifo, fence))
			LogPrintf(1, "%s: HasFencePassed failed!\n", __FUNCTION__);
		break;
	}
}

void CLASS::RingDoorBell()
{
	if (IsFIFORegValid(SVGA_FIFO_BUSY)) {
		if (!m_fifo_ptr[SVGA_FIFO_BUSY]) {
			m_fifo_ptr[SVGA_FIFO_BUSY] = 1;
			WriteReg(SVGA_REG_SYNC, 1);
		}
	} else {
		WriteReg(SVGA_REG_SYNC, 1);
	}
}

void CLASS::SyncFIFO()
{
	/*
	 * Crude, but effective
	 */
	WriteReg(SVGA_REG_SYNC, 1);
	while (ReadReg(SVGA_REG_BUSY));
}

#pragma mark -
#pragma mark Cursor Methods
#pragma mark -

void CLASS::setCursorState(uint32_t x, uint32_t y, bool visible)
{
	if (checkOptionFB(VMW_OPTION_FB_CURSOR_BYPASS_2) ||
		!HasFIFOCap(SVGA_FIFO_CAP_CURSOR_BYPASS_3)) {
		// CURSOR_BYPASS_2
		WriteReg(SVGA_REG_CURSOR_ID, 1U);
		WriteReg(SVGA_REG_CURSOR_X, x);
		WriteReg(SVGA_REG_CURSOR_Y, y);
		WriteReg(SVGA_REG_CURSOR_ON, visible ? 1U : 0U);
		return;
	}
	// CURSOR_BYPASS_3
	m_fifo_ptr[SVGA_FIFO_CURSOR_ON] = visible ? 1U : 0U;
	m_fifo_ptr[SVGA_FIFO_CURSOR_X] = x;
	m_fifo_ptr[SVGA_FIFO_CURSOR_Y] = y;
	++m_fifo_ptr[SVGA_FIFO_CURSOR_COUNT];
}

void CLASS::setCursorState(uint32_t screenId, uint32_t x, uint32_t y, bool visible)
{
	if (!HasFIFOCap(SVGA_FIFO_CAP_CURSOR_BYPASS_3)) {
		setCursorState(x, y, visible);
		return;
	}
	if (HasFIFOCap(SVGA_FIFO_CAP_SCREEN_OBJECT | SVGA_FIFO_CAP_SCREEN_OBJECT_2))
		m_fifo_ptr[SVGA_FIFO_CURSOR_SCREEN_ID] = screenId;
	// CURSOR_BYPASS_3
	m_fifo_ptr[SVGA_FIFO_CURSOR_ON] = visible ? 1U : 0U;
	m_fifo_ptr[SVGA_FIFO_CURSOR_X] = x;
	m_fifo_ptr[SVGA_FIFO_CURSOR_Y] = y;
	++m_fifo_ptr[SVGA_FIFO_CURSOR_COUNT];
}

void* CLASS::BeginDefineAlphaCursor(uint32_t width, uint32_t height, uint32_t bytespp)
{
	size_t cmd_len;
	SVGAFifoCmdDefineAlphaCursor* cmd;

	LogPrintf(2, "%s: %ux%u @ %u\n", __FUNCTION__, width, height, bytespp);
	cmd_len = sizeof *cmd + width * height * bytespp;
	cmd = static_cast<SVGAFifoCmdDefineAlphaCursor*>(FIFOReserveCmd(SVGA_CMD_DEFINE_ALPHA_CURSOR, cmd_len));
	if (!cmd)
		return 0;
	m_cursor_ptr = cmd;
	LogPrintf(2, "%s: cmdLen=%lu cmd=%p fifo=%p\n",
		__FUNCTION__, cmd_len, cmd, cmd + 1);
	return cmd + 1;
}

bool CLASS::EndDefineAlphaCursor(uint32_t width, uint32_t height, uint32_t bytespp, uint32_t hotspot_x, uint32_t hotspot_y)
{
	size_t cmd_len;
	SVGAFifoCmdDefineAlphaCursor* cmd = static_cast<SVGAFifoCmdDefineAlphaCursor*>(m_cursor_ptr);

	LogPrintf(2, "%s: %ux%u+%u+%u @ %u\n",
		__FUNCTION__, width, height, hotspot_x, hotspot_y, bytespp);
	if (!cmd)
		return false;
	cmd->id = 1;
	cmd->hotspotX = hotspot_x;
	cmd->hotspotY = hotspot_y;
	cmd->width = width;
	cmd->height = height;
	cmd_len = sizeof(uint32_t) + sizeof *cmd + width * height * bytespp;
	LogPrintf(3, "%s: cmdLen=%lu cmd=%p\n", __FUNCTION__, cmd_len, cmd);
	FIFOCommit(cmd_len);
	m_cursor_ptr = 0;
	return true;
}

void CLASS::SetMode(uint32_t width, uint32_t height, uint32_t bpp)
{
	/*
	 * LogLevel 0 in VMwareGfx 5.x
	 */
	LogPrintf(2, "%s: mode w,h=%u, %u bpp=%u\n", __FUNCTION__, width, height, bpp);
	SyncFIFO();
	WriteReg(SVGA_REG_WIDTH, width);
	WriteReg(SVGA_REG_HEIGHT, height);
	WriteReg(SVGA_REG_BITS_PER_PIXEL, bpp);
	// Keep exact visible dimensions, but negotiate rows usable by the shared
	// GPU backing path. Older QEMU devices retain their original packed rows.
	if (ReadReg(VIM_PANEL_CAPABILITIES_REGISTER) == VIM_PANEL_MAGIC &&
		HasCapability(SVGA_CAP_PITCHLOCK))
		WriteReg(SVGA_REG_PITCHLOCK, ((width * (bpp / 8)) + 31U) & ~31U);
	WriteReg(SVGA_REG_ENABLE, 1);
	m_pitch = ReadReg(SVGA_REG_BYTES_PER_LINE);
	LogPrintf(2, "%s: pitch=%u\n", __FUNCTION__, m_pitch);
	m_fb_size = ReadReg(SVGA_REG_FB_SIZE);
	m_width = width;
	m_height = height;
}

bool CLASS::UpdateFramebuffer(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
	LogPrintf(2, "%s: xy=%u %u, wh=%u %u\n", __FUNCTION__, x, y, width, height);
	SVGAFifoCmdUpdate* cmd = static_cast<SVGAFifoCmdUpdate*>(FIFOReserveCmd(SVGA_CMD_UPDATE, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->x = x;
	cmd->y = y;
	cmd->width = width;
	cmd->height = height;
	FIFOCommitAll();
	return true;
}

#pragma mark -
#pragma mark Video Methods
#pragma mark -

bool CLASS::BeginVideoSetRegs(uint32_t streamId, size_t numItems, struct SVGAEscapeVideoSetRegs **setRegs)
{
	SVGAEscapeVideoSetRegs* cmd;
	size_t cmd_size = sizeof *cmd - sizeof cmd->items + numItems * sizeof cmd->items[0];
	cmd = static_cast<SVGAEscapeVideoSetRegs*>(FIFOReserveEscape(SVGA_ESCAPE_NSID_VMWARE, cmd_size));
	if (!cmd)
		return false;
	cmd->header.cmdType = SVGA_ESCAPE_VMWARE_VIDEO_SET_REGS;
	cmd->header.streamId = streamId;
	*setRegs = cmd;
	return true;
}

bool CLASS::VideoSetRegsInRange(uint32_t streamId, struct SVGAOverlayUnit const* regs, uint32_t minReg, uint32_t maxReg)
{
	uint32_t const* regArray = reinterpret_cast<uint32_t const*>(regs);
	uint32_t const numRegs = maxReg - minReg + 1;
	SVGAEscapeVideoSetRegs *setRegs;
	uint32_t i;

	if (minReg > maxReg)
		return true;

	if (!BeginVideoSetRegs(streamId, numRegs, &setRegs))
		return false;

	for (i = 0; i < numRegs; i++) {
		setRegs->items[i].registerId = i + minReg;
		setRegs->items[i].value = regArray[i + minReg];
	}

	FIFOCommitAll();
	return true;
}

bool CLASS::VideoSetRegsWithMask(uint32_t streamId, struct SVGAOverlayUnit const* regs, uint32_t regMask)
{
	uint32_t const* regArray = reinterpret_cast<uint32_t const*>(regs);
	uint32_t i, numRegs;
	SVGAEscapeVideoSetRegs* setRegs;

	if (!regMask)
		return true;
	numRegs = count_bits(regMask);
	if (!BeginVideoSetRegs(streamId, numRegs, &setRegs))
		return false;
	for (numRegs = i = 0; regMask; (++i), (regMask >>= 1))
		if (regMask & 1U) {
			setRegs->items[numRegs].registerId = i;
			setRegs->items[numRegs].value = regArray[i];
			++numRegs;
		}

	FIFOCommitAll();
	return true;
}

bool CLASS::VideoSetReg(uint32_t streamId, uint32_t registerId, uint32_t value)
{
	SVGAEscapeVideoSetRegs* setRegs;

	if (!BeginVideoSetRegs(streamId, 1, &setRegs))
		return false;
	setRegs->items[0].registerId = registerId;
	setRegs->items[0].value = value;
	FIFOCommitAll();
	return true;
}

bool CLASS::VideoFlush(uint32_t streamId)
{
	SVGAEscapeVideoFlush* cmd;

	cmd = static_cast<SVGAEscapeVideoFlush*>(FIFOReserveEscape(SVGA_ESCAPE_NSID_VMWARE, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->cmdType = SVGA_ESCAPE_VMWARE_VIDEO_FLUSH;
	cmd->streamId = streamId;
	FIFOCommitAll();
	return true;
}

#pragma mark -
#pragma mark Added Methods
#pragma mark -

bool CLASS::get3DHWVersion(uint32_t* HWVersion) const
{
	if (!HWVersion)
		return false;
	if (m_fifo_ptr[SVGA_FIFO_MIN] <= static_cast<uint32_t>(sizeof(uint32_t) * SVGA_FIFO_GUEST_3D_HWVERSION))
		return false;
	if (HasFIFOCap(SVGA_FIFO_CAP_3D_HWVERSION_REVISED))
		*HWVersion = m_fifo_ptr[SVGA_FIFO_3D_HWVERSION_REVISED];
	else
		*HWVersion = m_fifo_ptr[SVGA_FIFO_3D_HWVERSION];
	return true;
}

void CLASS::RegDump()
{
	uint32_t regs[SVGA_REG_TOP];

	for (uint32_t i = SVGA_REG_ID; i < SVGA_REG_TOP; ++i)
		regs[i] = ReadReg(i);
	m_provider->setProperty("VMwareSVGADump", static_cast<void*>(&regs[0]), static_cast<unsigned>(sizeof regs));
}

uint32_t const* CLASS::get3DCapsBlock() const
{
	if (m_fifo_ptr[SVGA_FIFO_MIN] <= static_cast<uint32_t>(sizeof(uint32_t) * SVGA_FIFO_3D_CAPS_LAST))
		return 0;
	return m_fifo_ptr + SVGA_FIFO_3D_CAPS;
}

bool CLASS::RectCopy(uint32_t const* copyRect)
{
	SVGAFifoCmdRectCopy* cmd = static_cast<SVGAFifoCmdRectCopy*>(FIFOReserveCmd(SVGA_CMD_RECT_COPY, sizeof *cmd));
	if (!cmd)
		return false;
	memcpy(&cmd->srcX, copyRect, 6U * sizeof(uint32_t));
	FIFOCommitAll();
	return true;
}

bool CLASS::RectFill(uint32_t color, uint32_t const* rect)
{
	SVGAFifoCmdFrontRopFill* cmd = static_cast<SVGAFifoCmdFrontRopFill*>(FIFOReserveCmd(SVGA_CMD_FRONT_ROP_FILL, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->color = color;
	memcpy(&cmd->x, rect, 4U * sizeof(uint32_t));
	cmd->rop = SVGA_ROP_COPY;
	FIFOCommitAll();
	return true;
}

bool CLASS::UpdateFramebuffer2(uint32_t const* rect)
{
	SVGAFifoCmdUpdate* cmd = static_cast<SVGAFifoCmdUpdate*>(FIFOReserveCmd(SVGA_CMD_UPDATE, sizeof *cmd));
	if (!cmd)
		return false;
	memcpy(&cmd->x, rect, 4U * sizeof(uint32_t));
	FIFOCommitAll();
	return true;
}

bool CLASS::defineGMR(uint32_t gmrId, uint32_t ppn)
{
	if (!HasCapability(SVGA_CAP_GMR))
		return false;
	WriteReg(SVGA_REG_GMR_ID, gmrId);
	WriteReg(SVGA_REG_GMR_DESCRIPTOR, ppn);
	return true;
}

bool CLASS::defineGMR2(uint32_t gmrId, uint32_t numPages)
{
	SVGAFifoCmdDefineGMR2* cmd = static_cast<SVGAFifoCmdDefineGMR2*>(FIFOReserveCmd(SVGA_CMD_DEFINE_GMR2, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->gmrId = gmrId;
	cmd->numPages = numPages;
	FIFOCommitAll();
	return true;
}

bool CLASS::remapGMR2(uint32_t gmrId, uint32_t flags, uint32_t offsetPages,
					  uint32_t numPages, void const* suffix, size_t suffixSize)
{
	if (!suffix && suffixSize)
		return false;
	SVGAFifoCmdRemapGMR2* cmd = static_cast<SVGAFifoCmdRemapGMR2*>(FIFOReserveCmd(SVGA_CMD_REMAP_GMR2,
																				  sizeof *cmd + suffixSize));
	if (!cmd)
		return false;
	cmd->gmrId = gmrId;
	cmd->flags = static_cast<SVGARemapGMR2Flags>(flags);
	cmd->offsetPages = offsetPages;
	cmd->numPages = numPages;
	if (suffix)
		memcpy(&cmd[1], suffix, suffixSize);
	FIFOCommitAll();
	return true;
}
