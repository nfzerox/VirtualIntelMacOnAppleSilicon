/*
 *  SVGA3D.h
 *  VMsvga2Accel
 *
 *  Created by Zenith432 on August 11th 2009.
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

#ifndef __SVGA3D_H__
#define __SVGA3D_H__

#include <libkern/OSTypes.h>
#include "svga_apple_header.h"
#include "svga3d_reg.h"
#include "svga_apple_footer.h"

class SVGADevice;

class SVGA3D
{
private:
	SVGADevice* m_svga;
	uint32_t HWVersion;

	void* FIFOReserve(uint32_t cmd, size_t cmdSize);

public:
	/*
	 * SVGA Device Interoperability
	 */

	bool Init(SVGADevice*);
	uint32_t getHWVersion() const { return HWVersion; }
	void FIFOCommitAll();			// passthrough
	uint32_t InsertFence();			// passthrough
	bool BeginPresent(uint32_t sid, SVGA3dCopyRect **rects, size_t numRects);
	bool BeginPresentReadback(SVGA3dRect **rects, size_t numRects);
	bool BeginBlitSurfaceToScreen(SVGA3dSurfaceImageId const* srcImage,
								  SVGASignedRect const* srcRect,
								  uint32_t destScreenId,
								  SVGASignedRect const* destRect,
								  SVGASignedRect** clipRects,
								  uint32_t numClipRects);

	/*
	 * Surface Management
	 */

	bool BeginDefineSurface(uint32_t sid,
							SVGA3dSurfaceFlags flags,
							SVGA3dSurfaceFormat format,
							SVGA3dSurfaceFace **faces,
							SVGA3dSize **mipSizes,
							size_t numMipSizes);
	bool DestroySurface(uint32_t sid);
	bool BeginSurfaceDMA(SVGA3dGuestImage const *guestImage,
						 SVGA3dSurfaceImageId const *hostImage,
						 SVGA3dTransferType transfer,
						 SVGA3dCopyBox **boxes,
						 size_t numBoxes);
	bool BeginSurfaceDMAwithSuffix(SVGA3dGuestImage const *guestImage,
								   SVGA3dSurfaceImageId const *hostImage,
								   SVGA3dTransferType transfer,
								   SVGA3dCopyBox **boxes,
								   size_t numBoxes,
								   uint32_t maximumOffset,
								   SVGA3dSurfaceDMAFlags flags);

	/*
	 * Context Management
	 */

	bool DefineContext(uint32_t cid);
	bool DestroyContext(uint32_t cid);

	/*
	 * Drawing Operations
	 */

	bool BeginClear(uint32_t cid, SVGA3dClearFlag flags,
					uint32_t color, float depth, uint32_t stencil,
					SVGA3dRect **rects, size_t numRects);
	bool BeginDrawPrimitives(uint32_t cid,
							 SVGA3dVertexDecl **decls,
							 size_t numVertexDecls,
							 SVGA3dPrimitiveRange **ranges,
							 size_t numRanges);

	/*
	 * Blits
	 */

	bool BeginSurfaceCopy(SVGA3dSurfaceImageId const* src,
						  SVGA3dSurfaceImageId const* dest,
						  SVGA3dCopyBox **boxes, size_t numBoxes);

	bool SurfaceStretchBlt(SVGA3dSurfaceImageId const* src,
						   SVGA3dSurfaceImageId const* dest,
						   SVGA3dBox const* boxSrc, SVGA3dBox const* boxDest,
						   SVGA3dStretchBltMode mode);

	/*
	 * Shared FFP/Shader Render State
	 */

	bool SetRenderTarget(uint32_t cid, SVGA3dRenderTargetType type, SVGA3dSurfaceImageId const* target);
	bool SetZRange(uint32_t cid, float zMin, float zMax);
	bool SetViewport(uint32_t cid, SVGA3dRect const* rect);
	bool SetScissorRect(uint32_t cid, SVGA3dRect const* rect);
	bool SetClipPlane(uint32_t cid, uint32_t index, float const* plane);
	bool BeginSetTextureState(uint32_t cid, SVGA3dTextureState **states, size_t numStates);
	bool BeginSetRenderState(uint32_t cid, SVGA3dRenderState **states, size_t numStates);

	/*
	 * Fixed-function Render State
	 */

	bool SetTransform(uint32_t cid, SVGA3dTransformType type, float const* matrix);
	bool SetMaterial(uint32_t cid, SVGA3dFace face, SVGA3dMaterial const* material);
	bool SetLightData(uint32_t cid, uint32_t index, SVGA3dLightData const* data);
	bool SetLightEnabled(uint32_t cid, uint32_t index, bool enabled);

	/*
	 * Shaders
	 */

	IOReturn DefineShader(uint32_t cid, uint32_t shid, SVGA3dShaderType type, uint32_t const* bytecode, size_t bytecodeLen);
	bool DestroyShader(uint32_t cid, uint32_t shid, SVGA3dShaderType type);
	IOReturn SetShaderConst(uint32_t cid, uint32_t reg, SVGA3dShaderType type, SVGA3dShaderConstType ctype, void const* value);
	bool SetShader(uint32_t cid, SVGA3dShaderType type, uint32_t shid);

	/*
	 * Query
	 */
	bool BeginQuery(uint32_t cid, SVGA3dQueryType qtype);
	bool EndQuery(uint32_t cid, SVGA3dQueryType qtype, uint32_t gmrid, uint32_t offset_in_gmr);
	bool WaitForQuery(uint32_t cid, SVGA3dQueryType qtype, uint32_t gmrid, uint32_t offset_in_gmr);
};

#endif /* __SVGA3D_H__ */
