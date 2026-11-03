/*
 *  SVGAScreen.h
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

#ifndef __SVGASCREEN_H__
#define __SVGASCREEN_H__

#include <libkern/OSTypes.h>
#include "svga_apple_header.h"
#include "svga_reg.h"
#include "svga_apple_footer.h"

class SVGADevice;

class SVGAScreen
{
private:
	SVGADevice* m_svga;

public:
	bool Init(SVGADevice*);

	bool DefineScreen(SVGAScreenObject const* screen);
	bool DestroyScreen(UInt32 screenId);
	bool DefineGMRFB(SVGAGuestPtr ptr,
					 UInt32 bytesPerLine,
					 SVGAGMRImageFormat format);
	bool BlitFromGMRFB(SVGASignedPoint const* srcOrigin,
					   SVGASignedRect const* destRect,
					   UInt32 destScreen);
	bool BlitToGMRFB(SVGASignedPoint const* destOrigin,
					 SVGASignedRect const* srcRect,
					 UInt32 srcScreen);
	bool AnnotateFill(SVGAColorBGRX color);
	bool AnnotateCopy(SVGASignedPoint const* srcOrigin,
					  UInt32 srcScreen);
};

#endif /* __SVGASCREEN_H__ */
