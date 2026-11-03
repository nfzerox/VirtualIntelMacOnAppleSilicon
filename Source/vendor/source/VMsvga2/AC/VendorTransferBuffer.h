/*
 *  VendorTransferBuffer.h
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

#ifndef __VENDORTRANSFERBUFFER_H__
#define __VENDORTRANSFERBUFFER_H__

struct VendorTransferBuffer {
	uint32_t pad1;			//   0
	uint32_t gart_ptr;		//   4
	class IOMemoryDescriptor* md;
							//   8
	uint16_t offset12;		// offset 0xC - initialized to 4 in VMsvga2TextureBuffer
	uint16_t counter14;		// offset 0xE
							// 0x10 end
	uint32_t gmr_id;
	uint32_t fence;

	void init(void);
	IOReturn prepare(class VMsvga2Accel* provider);
	void sync(class VMsvga2Accel* provider);
	void complete(class VMsvga2Accel* provider);
	void discard(void);
};

#endif /* __VENDORTRANSFERBUFFER_H__ */
