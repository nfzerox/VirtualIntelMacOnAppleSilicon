/*
 *  SVGAScreen.cpp
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on November 3rd 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
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

#include <IOKit/IOLib.h>
#include "SVGAScreen.h"
#include "SVGADevice.h"

#if LOGGING_LEVEL >= 1
#define SLog(fmt, ...) IOLog(fmt, ##__VA_ARGS__)
#else
#define SLog(fmt, ...)
#endif

#define CLASS SVGAScreen

bool CLASS::Init(SVGADevice* device)
{
	if (!device) {
		m_svga = 0;
		return false;
	}
	m_svga = device;
	if (!device->HasFIFOCap(SVGA_FIFO_CAP_SCREEN_OBJECT | SVGA_FIFO_CAP_SCREEN_OBJECT_2)) {
		SLog("%s: Virtual device does not have Screen Object support.\n", __FUNCTION__);
		return false;
	}
	return true;
}

bool CLASS::DefineScreen(SVGAScreenObject const* screen)
{
	SVGAFifoCmdDefineScreen* cmd = static_cast<SVGAFifoCmdDefineScreen*>(m_svga->FIFOReserveCmd(SVGA_CMD_DEFINE_SCREEN, screen->structSize));
	if (!cmd)
		return false;
	memcpy(cmd, screen, screen->structSize);
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::DestroyScreen(UInt32 screenId)
{
	SVGAFifoCmdDestroyScreen* cmd = static_cast<SVGAFifoCmdDestroyScreen*>(m_svga->FIFOReserveCmd(SVGA_CMD_DESTROY_SCREEN, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->screenId = screenId;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::DefineGMRFB(SVGAGuestPtr ptr,
						UInt32 bytesPerLine,
						SVGAGMRImageFormat format)
{
	SVGAFifoCmdDefineGMRFB* cmd = static_cast<SVGAFifoCmdDefineGMRFB*>(m_svga->FIFOReserveCmd(SVGA_CMD_DEFINE_GMRFB, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->ptr = ptr;
	cmd->bytesPerLine = bytesPerLine;
	cmd->format = format;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::BlitFromGMRFB(SVGASignedPoint const* srcOrigin,
						  SVGASignedRect const* destRect,
						  UInt32 destScreen)
{
	SVGAFifoCmdBlitGMRFBToScreen* cmd = static_cast<SVGAFifoCmdBlitGMRFBToScreen*>(m_svga->FIFOReserveCmd(SVGA_CMD_BLIT_GMRFB_TO_SCREEN, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->srcOrigin = *srcOrigin;
	cmd->destRect = *destRect;
	cmd->destScreenId = destScreen;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::BlitToGMRFB(SVGASignedPoint const* destOrigin,
						SVGASignedRect const* srcRect,
						UInt32 srcScreen)
{
	SVGAFifoCmdBlitScreenToGMRFB* cmd = static_cast<SVGAFifoCmdBlitScreenToGMRFB*>(m_svga->FIFOReserveCmd(SVGA_CMD_BLIT_SCREEN_TO_GMRFB, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->destOrigin = *destOrigin;
	cmd->srcRect = *srcRect;
	cmd->srcScreenId = srcScreen;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::AnnotateFill(SVGAColorBGRX color)
{
	SVGAFifoCmdAnnotationFill* cmd = static_cast<SVGAFifoCmdAnnotationFill*>(m_svga->FIFOReserveCmd(SVGA_CMD_ANNOTATION_FILL, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->color = color;
	m_svga->FIFOCommitAll();
	return true;
}

bool CLASS::AnnotateCopy(SVGASignedPoint const* srcOrigin, UInt32 srcScreen)
{
	SVGAFifoCmdAnnotationCopy* cmd = static_cast<SVGAFifoCmdAnnotationCopy*>(m_svga->FIFOReserveCmd(SVGA_CMD_ANNOTATION_COPY, sizeof *cmd));
	if (!cmd)
		return false;
	cmd->srcOrigin = *srcOrigin;
	cmd->srcScreenId = srcScreen;
	m_svga->FIFOCommitAll();
	return true;
}
