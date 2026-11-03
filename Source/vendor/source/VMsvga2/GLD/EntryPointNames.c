/*
 *  EntryPointNames.c
 *  VMsvga2GLDriver
 *
 *  Created by Zenith432 on December 6th 2009.
 *  Copyright 2009-2010 Zenith432. All rights reserved.
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

#include "EntryPointNames.h"

#pragma GCC visibility push(hidden)

char const* const entry_point_names[NUM_ENTRIES] =
{
/*
 * From OS 10.6
 */
"gldGetVersion",
"gldGetRendererInfo",
"gldChoosePixelFormat",
"gldDestroyPixelFormat",
"gldCreateShared",
"gldDestroyShared",
"gldCreateContext",
"gldReclaimContext",
"gldDestroyContext",
"gldAttachDrawable",
"gldInitDispatch",
"gldUpdateDispatch",
"gldGetString",
"gldGetError",
"gldSetInteger",
"gldGetInteger",
"gldFlush",
"gldFinish",
"gldTestObject",
"gldFlushObject",
"gldFinishObject",
"gldWaitObject",
"gldCreateTexture",
"gldIsTextureResident",
"gldModifyTexture",
"gldLoadTexture",
"gldUnbindTexture",
"gldReclaimTexture",
"gldDestroyTexture",
"gldCreateTextureLevel",		// Discontinued OS 10.6.3
"gldGetTextureLevelInfo",
"gldGetTextureLevelImage",
"gldModifyTextureLevel",		// Discontinued OS 10.6.3
"gldDestroyTextureLevel",		// Discontinued OS 10.6.3
"gldCreateBuffer",
"gldLoadBuffer",
"gldFlushBuffer",
"gldPageoffBuffer",
"gldUnbindBuffer",
"gldReclaimBuffer",
"gldDestroyBuffer",
"gldGetMemoryPlugin",
"gldSetMemoryPlugin",
"gldTestMemoryPlugin",
"gldFlushMemoryPlugin",
"gldDestroyMemoryPlugin",
"gldCreateFramebuffer",
"gldUnbindFramebuffer",
"gldReclaimFramebuffer",
"gldDestroyFramebuffer",
"gldCreatePipelineProgram",
"gldGetPipelineProgramInfo",
"gldModifyPipelineProgram",
"gldUnbindPipelineProgram",
"gldDestroyPipelineProgram",
"gldCreateProgram",
"gldDestroyProgram",
"gldCreateVertexArray",
"gldModifyVertexArray",
"gldFlushVertexArray",
"gldUnbindVertexArray",
"gldReclaimVertexArray",
"gldDestroyVertexArray",
"gldCreateFence",
"gldDestroyFence",
"gldCreateQuery",
"gldGetQueryInfo",
"gldDestroyQuery",
"gldObjectPurgeable",
"gldObjectUnpurgeable",
"gldCreateComputeContext",
"gldDestroyComputeContext",
"gldLoadHostBuffer",
"gldSyncBufferObject",
"gldSyncTexture",
/*
 * Added OS 10.6.3
 */
"gldGenerateTexMipmaps",
"gldCopyTexSubImage",
"gldModifyTexSubImage",
"gldBufferSubData",
"gldModifyQuery",
"gldDiscardFramebuffer",
/*
 * From OS 10.5.8
 */
"gldGetTextureLevel",
"gldDeleteTextureLevel",
"gldDeleteTexture",
"gldAllocVertexBuffer",
"gldCompleteVertexBuffer",
"gldFreeVertexBuffer",
"gldGetMemoryPluginData",
"gldSetMemoryPluginData",
"gldFinishMemoryPluginData",
"gldTestMemoryPluginData",
"gldDestroyMemoryPluginData"
};

#pragma GCC visibility pop
