/*
 *  VendorTransferBuffer.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on January 24th 2011.
 *  Copyright 2011 Zenith432. All rights reserved.
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

#include <IOKit/IOMemoryDescriptor.h>
#include "VendorTransferBuffer.h"
#include "VMsvga2Accel.h"

#define CLASS VendorTransferBuffer

#define HIDDEN __attribute__((visibility("hidden")))

#pragma mark -
#pragma mark Global Functions
#pragma mark -

static inline
bool isIdValid(uint32_t id)
{
	return static_cast<int>(id) >= 0;
}

#pragma mark -
#pragma mark Public Methods
#pragma mark -

HIDDEN
void CLASS::init(void)
{
	gmr_id = SVGA_ID_INVALID;
}

HIDDEN
IOReturn CLASS::prepare(VMsvga2Accel* provider)
{
	IOReturn rc;

	if (!provider)
		return kIOReturnNotReady;
	if (isIdValid(gmr_id))
		return kIOReturnSuccess;
	if (!md)
		return kIOReturnNotReady;
	gmr_id = provider->AllocGMRID();
	if (!isIdValid(gmr_id)) {
#if 0
		IOLog("%s: Out of GMR IDs\n", __FUNCTION__);
#endif
		return kIOReturnNoResources;
	}
	rc = md->prepare();
	if (rc != kIOReturnSuccess)
		goto clean1;
	rc = provider->createGMR(gmr_id, md);
	if (rc != kIOReturnSuccess)
		goto clean2;
	return kIOReturnSuccess;

clean2:
	md->complete();
clean1:
	provider->FreeGMRID(gmr_id);
	gmr_id = SVGA_ID_INVALID;
	return rc;
}

HIDDEN
void CLASS::sync(VMsvga2Accel* provider)
{
	if (!provider || !fence)
		return;
	provider->SyncToFence(fence);
	fence = 0U;
}

HIDDEN
void CLASS::complete(VMsvga2Accel* provider)
{
	sync(provider);
	if (!provider || !isIdValid(gmr_id))
		return;
	provider->destroyGMR(gmr_id);
	if (md)
		md->complete();
	provider->FreeGMRID(gmr_id);
	gmr_id = SVGA_ID_INVALID;
}

HIDDEN
void CLASS::discard(void)
{
	if (!md)
		return;
	md->release();
	md = 0;
}
