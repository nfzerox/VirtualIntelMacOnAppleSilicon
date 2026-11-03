/*
 *  VMsvga2GLContext.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 21st 2009.
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

#ifndef __VMSVGA2GLCONTEXT_H__
#define __VMSVGA2GLCONTEXT_H__

#include <IOKit/IOUserClient.h>

struct VendorCommandBufferHeader;
class IOMemoryDescriptor;

struct VMsvga2CommandBuffer
{
	uint32_t pad1;
	uint8_t* gart_ptr;
	IOMemoryDescriptor* md;
	uint32_t pad2[8];
	VendorCommandBufferHeader* kernel_ptr;
	size_t size;
};

class VMsvga2GLContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2GLContext);

private:
	task_t m_owning_task;					// offset 0x78
											// offset 0x7C - 0x80: unknown
	class VMsvga2Accel* m_provider;			// offset 0x80
											// offset 0x84 - 0x8C: unknown
											// offset 0x8C
											// offset 0x90 - 0xA8: unknown 
	uint16_t m_drawbuf_params[2];			// offset 0xA8 - 0xAC
											// offset 0xAC - 0xB4: unknown
	IOMemoryDescriptor* m_fences;			// offset 0xB4
	size_t m_fences_len;					// offset 0xB8
	struct GLDFence* m_fences_ptr;			// offset 0xBC
											// offset 0xC0 - 0xC4: unknown
	class VMsvga2Surface* m_surface_client;	// offset 0xC4
	VMsvga2CommandBuffer m_command_buffer;	// offset 0xC8 - 0xFC
	class OSSet* m_gc;						// offset 0xFC
	VMsvga2CommandBuffer m_context_buffer0; // offset 0x100 - 0x134
	VMsvga2CommandBuffer m_context_buffer1; // offset 0x130 - 0x164
											// offset 0x160 - 0x194: unknown
	int m_stream_error;						// offset 0x194
											// offset 0x198 - 0x19C: unknown
	uint32_t m_mem_type;					// offset 0x19C

	/*
	 * VMsvga2 Specific
	 */
	int m_log_level;

	/*
	 * Private Methods
	 */
	void Init();
	void Cleanup();
	static bool allocCommandBuffer(VMsvga2CommandBuffer*, size_t);
	static void initCommandBufferHeader(VendorCommandBufferHeader*, size_t);
	bool allocAllContextBuffers();

public:
	/*
	 * Methods overridden from superclass
	 */
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
	IOReturn connectClient(IOUserClient* client);
#if 0
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments* arguments, IOExternalMethodDispatch* dispatch = 0, OSObject* target = 0, void* reference = 0);
#endif
	bool start(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga2GLContext* withTask(task_t owningTask, void* securityToken, uint32_t type);

	/*
	 * Methods corresponding to Apple's GeForce.kext GL Context User Client
	 */
	/*
	 * IONVGLContext
	 */
	IOReturn set_surface(uintptr_t, uintptr_t /* eIOGLContextModeBits */, uintptr_t, uintptr_t);
	IOReturn set_swap_rect(intptr_t, intptr_t, intptr_t, intptr_t);
	IOReturn set_swap_interval(intptr_t, intptr_t);
	IOReturn get_config(uint32_t*, uint32_t*, uint32_t*);
	IOReturn get_surface_size(uint32_t*, uint32_t*, uint32_t*, uint32_t*);
	IOReturn get_surface_info(uintptr_t, uint32_t*, uint32_t*, uint32_t*);
	IOReturn read_buffer(struct sIOGLContextReadBufferData const*, size_t);
	IOReturn finish();
	IOReturn wait_for_stamp(uintptr_t);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	IOReturn new_texture(struct VendorNewTextureDataStruc const*,
						 struct sIONewTextureReturnData*,
						 size_t,
						 size_t*);
	IOReturn delete_texture(uintptr_t);
#endif
	IOReturn become_global_shared(uintptr_t);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	IOReturn page_off_texture(struct sIODevicePageoffTexture const*, size_t);
#endif
	IOReturn purge_texture(uintptr_t);
	IOReturn set_surface_volatile_state(uintptr_t);
	IOReturn set_surface_get_config_status(struct sIOGLContextSetSurfaceData const*,
										   struct sIOGLContextGetConfigStatus*,
										   size_t,
										   size_t*);
	IOReturn reclaim_resources();
	IOReturn get_data_buffer(struct sIOGLContextGetDataBuffer*, size_t*);
	IOReturn set_stereo(uintptr_t, uintptr_t);
	IOReturn purge_accelerator(uintptr_t);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	IOReturn get_channel_memory(struct sIODeviceChannelMemoryData*, size_t*);
#else
	IOReturn submit_command_buffer(uintptr_t do_get_data,
								   struct sIOGLGetCommandBuffer*,
								   size_t*);
	IOReturn filter_control(struct sIOGLFilterControl const*,		// OS 10.6.8 Only
							struct sIOGLFilterControl*,
							size_t,
							size_t*);
#endif

	/*
	 * NVGLContext
	 */
	IOReturn get_query_buffer(uintptr_t c1, struct sIOGLGetQueryBuffer*, size_t*);
	IOReturn get_notifiers(uint32_t*, uint32_t*);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
	IOReturn new_heap_object(struct sNVGLNewHeapObjectData const*,
							 struct sIONewTextureReturnData*,
							 size_t,
							 size_t*);
#endif
	IOReturn kernel_printf(char const*, size_t);
	IOReturn nv_rm_config_get(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn nv_rm_config_get_ex(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn nv_client_request(void const*, void*, size_t, size_t*);
	IOReturn pageoff_surface_texture(struct sNVGLContextPageoffSurfaceTextureData const*, size_t);
	IOReturn get_data_buffer_with_offset(struct sIOGLContextGetDataBuffer*, size_t*);
	IOReturn nv_rm_control(uint32_t const*, uint32_t*, size_t, size_t*);
	IOReturn get_power_state(uint32_t*, uint32_t*);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
	IOReturn set_watchdog_timer(uintptr_t);
	IOReturn GetHandleIndex(uint32_t*, uint32_t*);
	IOReturn ForceTextureLargePages(uintptr_t);
#endif
};

#endif /* __VMSVGA2GLCONTEXT_H__ */
