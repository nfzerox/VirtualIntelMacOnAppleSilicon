/*
 *  VMsvga2GLDriver.h
 *  VMsvga2GLDriver
 *
 *  Created by Zenith432 on December 6th 2009.
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

#ifndef __VMSVGA2GLDRIVER_H__
#define __VMSVGA2GLDRIVER_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef int GLDReturn;

typedef GLDReturn (*GLD_GENERIC_FUNC)(void*, void*, void*, void*, void*, void*);

#define GLD_DECLARE_GENERIC(name) GLDReturn name(void*, void*, void*, void*, void*, void*)

void gldInitializeLibrary(int* psvc, void*, int GLDisplayMask, void*, void*);
void gldTerminateLibrary(void);

_Bool gldGetVersion(int*, int*, int*, int*);
GLDReturn gldGetRendererInfo(void* struct_out, int GLDisplayMask);
GLDReturn gldChoosePixelFormat(void** struct_out, int* attributes);
GLDReturn gldDestroyPixelFormat(void* struct_in);	// calls free(struct_in), returns 0
GLDReturn gldCreateShared(void* arg0, int GLDisplayMask, long arg2);
GLDReturn gldDestroyShared(void* struct_in);
GLDReturn gldCreateContext(void* arg0, void* arg1, void* arg2, void* arg3, void* arg4, void* arg5);
GLDReturn gldReclaimContext(void* arg0);
GLDReturn gldDestroyContext(void* arg0);
GLDReturn gldAttachDrawable(void* context, int surface_type, void* arg2, void* arg3);	// note: arg2+arg3 may be a 64-bit param
GLDReturn gldInitDispatch(void* arg0, void* arg1, void* arg2);
GLDReturn gldUpdateDispatch(void* arg0, void* arg1, void* arg2);
char const* gldGetString(int GLDisplayMask, int string_code);
void gldGetError(void* arg0);
GLDReturn gldSetInteger(void* arg0, int arg1, void* arg2);
GLDReturn gldGetInteger(void* arg0, int arg1, void* arg2);
GLD_DECLARE_GENERIC(gldFlush);
GLD_DECLARE_GENERIC(gldFinish);
GLD_DECLARE_GENERIC(gldTestObject);
GLD_DECLARE_GENERIC(gldFlushObject);
GLD_DECLARE_GENERIC(gldFinishObject);
GLD_DECLARE_GENERIC(gldWaitObject);
GLDReturn gldCreateTexture(void* arg0, void* arg1, void* arg2);
GLD_DECLARE_GENERIC(gldIsTextureResident);
GLD_DECLARE_GENERIC(gldModifyTexture);
GLD_DECLARE_GENERIC(gldLoadTexture);
void gldUnbindTexture(void* arg0, void* arg1);
GLD_DECLARE_GENERIC(gldReclaimTexture);
void gldDestroyTexture(void* arg0, void* arg1);
GLD_DECLARE_GENERIC(gldCreateTextureLevel);
GLD_DECLARE_GENERIC(gldGetTextureLevelInfo);
GLD_DECLARE_GENERIC(gldGetTextureLevelImage);
GLD_DECLARE_GENERIC(gldModifyTextureLevel);
GLD_DECLARE_GENERIC(gldDestroyTextureLevel);
GLD_DECLARE_GENERIC(gldCreateBuffer);
GLD_DECLARE_GENERIC(gldLoadBuffer);
GLD_DECLARE_GENERIC(gldFlushBuffer);
GLD_DECLARE_GENERIC(gldPageoffBuffer);
GLD_DECLARE_GENERIC(gldUnbindBuffer);
GLD_DECLARE_GENERIC(gldReclaimBuffer);
GLD_DECLARE_GENERIC(gldDestroyBuffer);
GLD_DECLARE_GENERIC(gldGetMemoryPlugin);
GLD_DECLARE_GENERIC(gldSetMemoryPlugin);
GLD_DECLARE_GENERIC(gldTestMemoryPlugin);
GLD_DECLARE_GENERIC(gldFlushMemoryPlugin);
GLD_DECLARE_GENERIC(gldDestroyMemoryPlugin);
GLD_DECLARE_GENERIC(gldCreateFramebuffer);
GLD_DECLARE_GENERIC(gldUnbindFramebuffer);
GLD_DECLARE_GENERIC(gldReclaimFramebuffer);
GLD_DECLARE_GENERIC(gldDestroyFramebuffer);
GLDReturn gldCreatePipelineProgram(void* arg0, void* arg1, void* arg2);
GLD_DECLARE_GENERIC(gldGetPipelineProgramInfo);
GLD_DECLARE_GENERIC(gldModifyPipelineProgram);
GLDReturn gldUnbindPipelineProgram(void* arg0, void* arg1);
GLDReturn gldDestroyPipelineProgram(void* arg0, void* arg1);
GLD_DECLARE_GENERIC(gldCreateProgram);
GLD_DECLARE_GENERIC(gldDestroyProgram);
GLDReturn gldCreateVertexArray(void);
GLD_DECLARE_GENERIC(gldModifyVertexArray);
GLD_DECLARE_GENERIC(gldFlushVertexArray);
GLD_DECLARE_GENERIC(gldUnbindVertexArray);
GLD_DECLARE_GENERIC(gldReclaimVertexArray);
GLDReturn gldDestroyVertexArray(void);
GLD_DECLARE_GENERIC(gldCreateFence);
GLD_DECLARE_GENERIC(gldDestroyFence);
GLD_DECLARE_GENERIC(gldCreateQuery);
GLD_DECLARE_GENERIC(gldGetQueryInfo);
GLD_DECLARE_GENERIC(gldDestroyQuery);
GLD_DECLARE_GENERIC(gldObjectPurgeable);
GLD_DECLARE_GENERIC(gldObjectUnpurgeable);
//GLDReturn gldObjectUnpurgeable(arg0, arg1, arg2, arg3, arg4);
GLDReturn gldCreateComputeContext(void);
GLDReturn gldDestroyComputeContext(void);
GLDReturn gldLoadHostBuffer(void);
GLDReturn gldSyncBufferObject(void);
GLDReturn gldSyncTexture(void);

#ifdef __cplusplus
}
#endif

#endif /* __VMSVGA2GLDRIVER_H__ */
