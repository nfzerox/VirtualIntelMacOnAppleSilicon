/*
 *  VMsvga2OCDContext.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 11th 2009.
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

#ifndef __VMSVGA2OCDCONTEXT_H__
#define __VMSVGA2OCDCONTEXT_H__

#include <IOKit/IOUserClient.h>

class VMsvga2OCDContext: public IOUserClient
{
	OSDeclareDefaultStructors(VMsvga2OCDContext);

private:
	task_t m_owning_task;
	class VMsvga2Accel* m_provider;
	int m_log_level;

public:
	/*
	 * Methods overridden from superclass
	 */
	IOExternalMethod* getTargetAndMethodForIndex(IOService** targetP, UInt32 index);
	IOReturn clientClose();
	IOReturn clientMemoryForType(UInt32 type, IOOptionBits* options, IOMemoryDescriptor** memory);
	IOReturn connectClient(IOUserClient* client);
	IOReturn registerNotificationPort(mach_port_t port, UInt32 type, UInt32 refCon);
	bool start(IOService* provider);
	bool initWithTask(task_t owningTask, void* securityToken, UInt32 type);
	static VMsvga2OCDContext* withTask(task_t owningTask, void* securityToken, uint32_t type);

	/*
	 * Methods from IONVOCDContext
	 */
	IOReturn finish();
	IOReturn wait_for_stamp(uintptr_t);

	/*
	 * Methods from NVOCDContext
	 */
	IOReturn check_error_notifier(struct NvNotificationRec volatile*, size_t*);
	IOReturn mark_texture_for_ocd_use(uintptr_t);
	IOReturn FreeEvent();
	IOReturn GetHandleIndex(uint32_t*, uint32_t*);
};

#endif /* __VMSVGA2OCDCONTEXT_H__ */
