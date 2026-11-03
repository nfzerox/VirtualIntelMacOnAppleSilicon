/*
 *  IOSurfaceRoot.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on October 16th 2010.
 *  Copyright 2010 Zenith432. All rights reserved.
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

#ifndef __IOSURFACEROOT_H__
#define __IOSURFACEROOT_H__

#include <IOKit/IOService.h>

class IOSurfaceRoot : public IOService
{
	OSDeclareDefaultStructors(IOSurfaceRoot);

public:
	virtual void* find_surface(unsigned, task_t, class IOSurfaceRootUserClient*);
	virtual void add_surface_buffer(class IOSurface*);
	virtual void remove_surface_buffer(class IOSurface*);
	virtual void* createSurface(task_t, OSDictionary*);
	virtual void* lookupSurface(unsigned, task_t);
	virtual unsigned generateUniqueAcceleratorID(void*);
	virtual void updateLimits(unsigned, unsigned, unsigned, unsigned, unsigned);
};

#endif /* __IOSURFACEROOT_H__ */
