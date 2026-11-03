/*
 *  VMsvga2.h
 *  VMsvga2
 *
 *  Created by Zenith432 on July 2nd 2009.
 *  Copyright 2009-2011 Zenith432. All rights reserved.
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

#ifndef __VMSVGA2_H__
#define __VMSVGA2_H__

#include <IOKit/graphics/IOFramebuffer.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include "SVGADevice.h"
#include "common_fb.h"

class VMsvga2 : public IOFramebuffer
{
	OSDeclareDefaultStructors(VMsvga2);

private:
	SVGADevice svga;				// (now * at 0x10C)
	IODeviceMemory* m_vram;			// offset 0x110
#if 0
	IOMemoryMap* m_vram_kernel_map;	// offset 0x114
	IOVirtualAddress m_vram_kernel_ptr;	// offset 0x118
	IOPhysicalAddress m_fb_offset;	// offset 0x11C
	uint32_t m_aperture_size;		// offset 0x120
#endif
	uint32_t m_num_active_modes;	// offset 0x124
#if 0
	uint32_t m_max_width;			// offset 0x128
	uint32_t m_max_height;			// offset 0x12C
#endif
	IODisplayModeID m_display_mode;	// offset 0x130
	IOIndex m_depth_mode;			// offset 0x134
	thread_call_t m_restore_call;	// offset 0x138
	IODisplayModeID m_modes[NUM_DISPLAY_MODES];	// offset 0x13C (16 entries)
	uint32_t m_custom_switch;		// offset 0x17C
	bool m_custom_mode_switched;	// offset 0x180
#if 0
	uint32_t m_custom_mode_width;	// offset 0x184
	uint32_t m_custom_mode_height;	// offset 0x188
#endif
	struct {
		OSObject* target;
		void* ref;
		IOFBInterruptProc proc;
	} m_intr;						// offset 0x18C
	IOLock* m_iolock;				// offset 0x198
	void* m_cursor_image;			// offset 0x19C
	int32_t m_hotspot_x;			// offset 0x1A0
	int32_t m_hotspot_y;			// offset 0x1A4

	/*
	 * Begin Added
	 */
	bool m_intr_enabled;
	bool m_accel_updates;
	thread_call_t m_refresh_call;
	uint32_t m_refresh_quantum_ms;
	DisplayModeEntry customMode;
	uint32_t m_edid_size;
	uint8_t* m_edid;
	IODisplayModeID m_panel_mode;
	uint32_t m_panel_width,m_panel_height,m_panel_scale;
	uint32_t m_panel_generation;
	bool m_panel_uncaptured;
	IOWorkLoop* m_panel_loop;
	IOInterruptEventSource* m_panel_interrupt;
	IOCommandGate* m_panel_dispatch_gate;
	IOWorkLoop* m_panel_resize_loop;
	IOInterruptEventSource* m_panel_resize_event;
	uint32_t m_panel_pending_width,m_panel_pending_height;
	uint32_t m_panel_pending_scale;
	bool m_backing_interrupt_supported;
	OSObject* m_backing_interrupt_owner;
	void (*m_backing_interrupt_action)(OSObject*);
	// A real virtual-panel VBL source, separate from the software scanout
	// refresh timer and hotplug interrupt. IOFramebuffer owns publication and
	// its normal throttling; never write the shared cursor/clock ABI ourselves.
	struct {
		OSObject* target;
		void* ref;
		IOFBInterruptProc proc;
	} m_vbl_intr;
	IOWorkLoop* m_vbl_loop;
	IOTimerEventSource* m_vbl_timer;
	bool m_vbl_enabled;
	uint64_t m_vbl_period,m_vbl_deadline;
	/*
	 * End Added
	 */

	void Cleanup();
	void publishFramebufferLayout();
	void configureNativePanel(uint32_t,uint32_t);
	void setupNativePanelInterrupt(IOService*);
	void cleanupNativePanelInterrupt();
	void enableNativePanelInterrupt(bool);
	static void panelInterrupt(OSObject*,IOInterruptEventSource*,int);
	static void panelResize(OSObject*,IOInterruptEventSource*,int);
	static IOReturn backingInterruptState(OSObject*,void*,void*,void*,void*);
	static bool panelInterruptFilter(OSObject*,IOFilterInterruptEventSource*);
	IOReturn registerVirtualVBL(IOFBInterruptProc,OSObject*,void*,void**);
	void cleanupVirtualVBL();
	IOReturn scheduleVirtualVBL();
	static void virtualVBL(OSObject*,IOTimerEventSource*);
	static IOReturn virtualVBLState(OSObject*,void*,void*,void*,void*);
	static uint32_t FindDepthMode(IOIndex depth);
	DisplayModeEntry const* GetDisplayMode(IODisplayModeID displayMode);
	static void IOSelectToString(IOSelect io_select, char* output);
	static void ConvertAlphaCursor(uint32_t* cursor, uint32_t width, uint32_t height);
	void CustomSwitchStepWait(uint32_t value);
	void CustomSwitchStepSet(uint32_t value);
	bool keepCustomPanelMode() const;
	void EmitConnectChangedEvent();
	void RestoreAllModes();
	static void _RestoreAllModes(thread_call_param_t param0, thread_call_param_t param1);

	/*
	 * Begin Added
	 */
	void scheduleRefreshTimer(uint32_t milliSeconds);
	void scheduleRefreshTimer();
	void cancelRefreshTimer();
	void refreshTimerAction();
	static void _RefreshTimerAction(thread_call_param_t param0, thread_call_param_t param1);
	void setupRefreshTimer();
	void deleteRefreshTimer();
	IODisplayModeID TryDetectCurrentDisplayMode(IODisplayModeID defaultMode) const;
	/*
	 * End Added
	 */

public:
	UInt64 getPixelFormatsForDisplayMode(IODisplayModeID displayMode, IOIndex depth);
	IOReturn setCursorState(SInt32 x, SInt32 y, bool visible);
	IOReturn setCursorImage(void* cursorImage);
	IOReturn setInterruptState(void* interruptRef, UInt32 state);
	IOReturn unregisterInterrupt(void* interruptRef);
	IOItemCount getConnectionCount();
	IOReturn getCurrentDisplayMode(IODisplayModeID* displayMode, IOIndex* depth);
	IOReturn getDisplayModes(IODisplayModeID* allDisplayModes);
	IOItemCount getDisplayModeCount();
	const char* getPixelFormats();
	IODeviceMemory* getVRAMRange();
	IODeviceMemory* getApertureRange(IOPixelAperture aperture);
	bool isConsoleDevice();
	bool start(IOService* provider);
	void stop(IOService* provider);
	IOReturn getAttribute(IOSelect attribute, uintptr_t* value);
	IOReturn getAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t* value);
	IOReturn setAttribute(IOSelect attribute, uintptr_t value);
	IOReturn setAttributeForConnection(IOIndex connectIndex, IOSelect attribute, uintptr_t value);
	IOReturn registerForInterruptType(IOSelect interruptType, IOFBInterruptProc proc, OSObject* target, void* ref, void** interruptRef);
	IOReturn CustomMode(CustomModeData const* inData, CustomModeData* outData, size_t inSize, size_t* outSize);
	IOReturn getInformationForDisplayMode(IODisplayModeID displayMode, IODisplayModeInformation* info);
	IOReturn getTimingInfoForDisplayMode(IODisplayModeID displayMode,IOTimingInformation* info);
	IOReturn connectFlags(IOIndex connectIndex, IODisplayModeID displayMode, IOOptionBits* flags);
	IOReturn getPixelInformation(IODisplayModeID displayMode, IOIndex depth, IOPixelAperture aperture, IOPixelInformation* pixelInfo);
	IOReturn setDisplayMode(IODisplayModeID displayMode, IOIndex depth);

	/*
	 * Begin Added
	 */
	IOReturn getDDCBlock(IOIndex connectIndex, UInt32 blockNumber, IOSelect blockType, IOOptionBits options, UInt8* data, IOByteCount* length);
	bool hasDDCConnect(IOIndex connectIndex);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	bool passiveMatch(OSDictionary* matching, bool changesOK = false);
#endif
	/*
	 * End Added
	 */

	/*
	 * Accelerator Support
	 */
	SVGADevice* getDevice() { return &svga; }
	void lockDevice();
	void unlockDevice();
	bool supportsAccel();
	IOReturn configureBackingInterrupt(OSObject*,void (*)(OSObject*));
	void useAccelUpdates(bool state);

#if 0
	IOReturn getStartupDisplayMode(IODisplayModeID* displayMode, IOIndex* depth);
	IOReturn getTimingInfoForDisplayMode(IODisplayModeID displayMode, IOTimingInformation* info);
	IOReturn setDetailedTimings(OSArray* array);
	IOReturn setGammaTable(UInt32 channelCount, UInt32 dataCount, UInt32 dataWidth, void* data);
	IOReturn setStartupDisplayMode(IODisplayModeID displayMode, IOIndex depth);
	IOReturn validateDetailedTiming(void* description, IOByteCount descripSize);
#endif
};

#endif /* __VMSVGA2_H__ */
