/*
 *  BlitHelper.cpp
 *  VMsvga2GA
 *
 *  Created by Zenith432 on August 12th 2009.
 *  Copyright 2009 Zenith432. All rights reserved.
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

#include <IOKit/IOKitLib.h>
#include "BlitHelper.h"
#include "UCMethods.h"

IOReturn useAccelUpdates(io_connect_t context, int state)
{
	uint64_t input;

	input = state ? 1 : 0;
	return IOConnectCallMethod(context,
							   kIOVM2DUseAccelUpdates,
							   &input, 1,
							   0, 0,
							   0, 0,
							   0, 0);
}

IOReturn RectCopy(io_connect_t context, void const* copyRects, size_t copyRectsSize)
{
	return IOConnectCallMethod(context,
							   kIOVM2DRectCopy,
							   0, 0,
							   copyRects, copyRectsSize,
							   0, 0,
							   0, 0);
}

IOReturn RectFill(io_connect_t context, uintptr_t color, void const* rects, size_t rectsSize)
{
	uint64_t input;

	input = color;
	return IOConnectCallMethod(context,
							   kIOVM2DRectFill,
							   &input, 1,
							   rects, rectsSize,
							   0, 0,
							   0, 0);
}

IOReturn UpdateFramebuffer(io_connect_t context, UInt32 const* rect)
{
	return IOConnectCallMethod(context,
							   kIOVM2DUpdateFramebuffer,
							   0, 0,
							   rect, 4U * sizeof(UInt32),
							   0, 0,
							   0, 0);
}

IOReturn CopyRegion(io_connect_t context, uintptr_t source_surface_id, intptr_t destX, intptr_t destY, void const* region, size_t regionSize)
{
	uint64_t input[3];

	input[0] = source_surface_id;
	input[1] = (uint64_t) destX;
	input[2] = (uint64_t) destY;
	return IOConnectCallMethod(context,
							   kIOVM2DCopyRegion,
							   &input[0], 3,
							   region, regionSize,
							   0, 0,
							   0, 0);
}
