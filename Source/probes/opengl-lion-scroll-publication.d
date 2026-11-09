/* Bounded Lion 11G63 WindowServer scroll/present trace:
 * sudo dtrace -Zqs opengl-lion-scroll-publication.d WINDOWSERVER_PID
 * Scroll an already open Finder list after READY. Optional symbols are
 * deliberate: the same probe can run with the physical graphics driver.
 */
BEGIN { printf("READY\n"); }

pid$1:CoreGraphics:CGSBackingStoreCopy:entry
{
    start = timestamp;
    printf("copy dx=%d dy=%d\n",(int)arg2,(int)arg3);
}

pid$1:CoreGraphics:*CGXInvalidateWindowRegion:entry,
pid$1:CoreGraphics:CGXBeginMoveShape:entry,
pid$1:CoreGraphics:CGXAddMoveShape:entry,
pid$1:CoreGraphics:CGXEndMoveShape:entry,
pid$1:CoreGraphics:CGXHWMoveRegion:entry,
pid$1:CoreGraphics:MPHWMoveRegion:entry,
pid$1:CoreGraphics:MPHWCopyPixels:entry,
pid$1:CoreGraphics:MPHWCopyRegion:entry,
pid$1:CoreGraphics:MPHWCopyForwardRegion:entry,
pid$1:CoreGraphics:IOFBBlitSurfaceMove:entry,
pid$1:CoreGraphics:*copy_forward_region:entry,
pid$1:CoreGraphics:CGXBackingStoreTextureGet:entry,
pid$1:CoreGraphics:CGXGLBackingStoreGetSurfaceTextureAccelerator:entry,
pid$1:CoreGraphics:CGXGLSetTextureSurface:entry,
pid$1:CoreGraphics:*CGXRedrawWindow:entry,
pid$1:CoreGraphics:*synchronizeBackingSurface:entry,
pid$1:IOKit:IOFBBlitVRAMCopy:entry,
pid$1:IOKit:IOFBBlitSurfaceCopy:entry,
pid$1:IOKit:IOFBBlitSurfaceSurfaceCopy:entry,
pid$1:IOKit:IOAccelSetSurfaceFramebufferShape:entry,
pid$1:*:*vmCopy*:entry,
pid$1:GLEngine:*VIMImportCGSTexture*:entry,
pid$1:GLEngine:*VIMUpdateCGSLocked*:entry,
pid$1:GLEngine:*VIMRefreshCGSTexture*:entry,
pid$1:libVIMPassthroughOpenGL*:*DGUploadSurface*:entry,
pid$1:libVIMPassthroughOpenGL*:*DGPublishSurfaceEpoch*:entry
/start && timestamp-start<1500000000/
{
    @[probemod,probefunc] = count();
}

pid$1:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/start && timestamp-start<1500000000/
{
    @shape_options[probefunc,arg2] = count();
}

pid$1:CoreGraphics:MPHWCopyRegion:entry
/start && timestamp-start<1500000000/
{
    self->move = 1;
    printf("display copy destination=%d,%d sourceSurface=%u\n",
        (int)arg3,(int)arg4,(uint32_t)arg5);
    ustack(9);
}

pid$1:CoreGraphics:CGSGetRegionRectCount:return
/self->move/
{
    printf("display copy rectangles=%d\n",(int)arg1);
}

pid$1:CoreGraphics:CGBlt_copyBytes:entry
/self->move/
{
    printf("display CPU copy widthBytes=%u rows=%u dst-src=%d rowbytes=%d/%d\n",
        (uint32_t)arg0,(uint32_t)arg1,(int)(arg3-arg2),(int)arg4,(int)arg5);
}

pid$1:CoreGraphics:MPHWCopyRegion:return
/self->move/
{
    printf("display copy success=%d\n",(int)arg1);
    self->move = 0;
}

tick-25sec { exit(0); }
