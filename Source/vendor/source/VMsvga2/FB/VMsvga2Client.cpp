/*
 *  VMsvga2Client.cpp
 *  VMsvga2
 *
 *  Created by Zenith432 on July 4th 2009.
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

#include "VMsvga2Client.h"
#include "VMsvga2.h"
#include "VLog.h"

#define super IOUserClient
OSDefineMetaClassAndStructors(VMsvga2Client, IOUserClient);

#if LOGGING_LEVEL >= 1
#define LogPrintf(log_level, ...) do { if (log_level <= logLevelFB) VLog("IOFBClient: ", ##__VA_ARGS__); } while (false)
#else
#define LogPrintf(log_level, ...)
#endif

static IOExternalMethod const iofbFuncsCache[1] =
{
	{0, reinterpret_cast<IOMethod>(&VMsvga2::CustomMode), kIOUCStructIStructO, sizeof(CustomModeData), sizeof(CustomModeData)}
};

IOExternalMethod* VMsvga2Client::getTargetAndMethodForIndex(IOService** targetP, UInt32 index)
{
	LogPrintf(2, "%s: index=%u.\n", __FUNCTION__, static_cast<unsigned>(index));
	if (!targetP)
		return 0;
	if (index != 0 && index != 3) {
		LogPrintf(1, "%s: Invalid index %u.\n",
				  __FUNCTION__, static_cast<unsigned>(index));
		return 0;
	}
	*targetP = getProvider();
	return const_cast<IOExternalMethod*>(&iofbFuncsCache[0]);
}

IOReturn VMsvga2Client::clientClose()
{
	LogPrintf(2, "%s()\n", __FUNCTION__);
	if (!terminate())
		LogPrintf(1, "%s: terminate failed.\n", __FUNCTION__);
	return kIOReturnSuccess;
}

bool VMsvga2Client::initWithTask(task_t owningTask, void* securityToken, UInt32 type)
{
	if (!super::initWithTask(owningTask, securityToken, type) ||
		clientHasPrivilege(securityToken, kIOClientPrivilegeAdministrator) != kIOReturnSuccess)
		return false;
	return true;
}
