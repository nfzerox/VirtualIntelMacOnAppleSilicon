/* Lion 11G63 WindowServer, run on a scratch guest:
 * sudo dtrace -Zqs opengl-lion-scroll-overwrite.d WINDOWSERVER_PID FINDER_PID
 * Start one vertical scroll after READY. No tracing during normal operation. */
BEGIN { printf("READY\n"); }

pid$1:CoreGraphics:CGSBackingStoreCopy:entry
{
    self->scroll=1;
    self->baseout=(uint64_t)0;
    self->base=(uint64_t)0;
    backing_offset=(uint64_t)-1;
    finder_sample=(uint64_t)0;
    printf("scroll dx=%d dy=%d\n",(int)arg2,(int)arg3);
}

pid$1:CoreGraphics:CGXBackingStoreDataLock:entry
/self->scroll/
{ self->baseout=arg3; }

pid$1:CoreGraphics:CGXBackingStoreDataLock:return
/self->baseout && !arg1/
{ self->base=*(uint64_t *)copyin(self->baseout,8); }

pid$1:CoreGraphics:CGBlt_copyBytes:entry
/self->scroll && arg0>512 && arg1>10/
{
    sample=arg3+9*(uint32_t)arg5+64;
    backing_pitch=(uint32_t)arg5;
    backing_offset=self->base && sample>=self->base?sample-self->base:(uint64_t)-1;
    started=timestamp;
    last=*(uint32_t *)copyin(sample,4);
    printf("before blt 0x%x\n",last);
}

pid$1:CoreGraphics:CGBlt_copyBytes:return,
pid$1:CoreGraphics:CGSBackingStoreCopy:return,
pid$1:CoreGraphics:*CGXScrollWindowRegion*:return,
pid$1:CoreGraphics:*CGSShmemSeed*:entry,
pid$1:CoreGraphics:*CGSShmemSeed*:return,
pid$1:CoreGraphics:*CGXInvalidateWindowRegion*:entry,
pid$1:CoreGraphics:*CGXInvalidateWindowRegion*:return,
pid$1:CoreGraphics:*CGXFlushWindow*:entry,
pid$1:CoreGraphics:*CGXFlushWindow*:return,
pid$1:CoreGraphics:*CGXUpdateDisplaySynchronizeBacking*:entry,
pid$1:CoreGraphics:*CGXUpdateDisplaySynchronizeBacking*:return,
pid$1:CoreGraphics:*synchronizeBackingSurface:entry,
pid$1:CoreGraphics:*synchronizeBackingSurface:return,
pid$1:CoreGraphics:*updateBackingSurface:entry,
pid$1:CoreGraphics:*updateBackingSurface:return,
pid$1:CoreGraphics:*CGXBackingStoreActivateFrontBuffer*:entry,
pid$1:CoreGraphics:*CGXBackingStoreActivateFrontBuffer*:return,
pid$1:CoreGraphics:*CGXBackingStoreTextureGet*:entry,
pid$1:CoreGraphics:*CGXBackingStoreTextureGet*:return,
pid$1:IOKit:*IOAccel*Surface*:entry,
pid$1:IOKit:*IOAccel*Surface*:return
/started && timestamp-started<100000000 && pid==$1/
{
    this->current=*(uint32_t *)copyin(sample,4);
    printf("%6d us %-42s %s 0x%x%s\n",
        (int)((timestamp-started)/1000),probefunc,probename,this->current,
        this->current!=last?" CHANGED":"");
    last=this->current;
}

pid$1:CoreGraphics:CGSBackingStoreCopy:return /self->scroll/
{ self->scroll=0; self->baseout=0; }

pid$2:CoreGraphics:CGSDeviceScroll:entry,
pid$2:CoreGraphics:CGSDeviceLock:entry
{ self->device=arg0; }

pid$2:CoreGraphics:CGSDeviceScroll:return,
pid$2:CoreGraphics:CGSDeviceLock:return
/self->device && started && timestamp-started<100000000/
{
    this->base=*(uint64_t *)copyin(self->device+0xe8,8);
    this->pitch=*(uint32_t *)copyin(self->device+0xdc,4);
    this->accel=*(uint64_t *)copyin(self->device+0xc8,8);
    this->comparable=this->base && this->pitch==backing_pitch && backing_offset<32*1024*1024;
    this->pixel=this->comparable?*(uint32_t *)copyin(this->base+backing_offset,4):0;
    finder_sample=this->comparable?this->base+backing_offset:finder_sample;
    printf("%6d us Finder %s:%s accel=%d matching pitch=%d pixel=0x%x\n",
        (int)((timestamp-started)/1000),probefunc,probename,!!this->accel,
        this->comparable,this->pixel);
    self->device=0;
}

pid$2:libVIMPassthroughOpenGL*:*DGReadDrawableRectRows:entry
/finder_sample && started && timestamp-started<100000000/
{
    /* Finder's CG backing and the GLI surface lock may map the same
     * allocation at different virtual addresses. Observe Finder's original
     * mapping before and after rather than requiring pointer equality. */
    self->candidate=finder_sample;
    self->epoch=started;
    printf("%6d us Finder scatter x=%d y=%d width=%d pitch=%d match=%d pixel=0x%x\n",
        (int)((timestamp-started)/1000),(int)arg3,(int)arg4,(int)arg5,(int)arg1,
        arg1==backing_pitch,*(uint32_t *)copyin(self->candidate,4));
}

pid$2:libVIMPassthroughOpenGL*:*DGReadDrawableRectRows:return
/self->candidate && self->epoch==started/
{
    printf("%6d us Finder scatter returned %d pixel=0x%x\n",
        (int)((timestamp-started)/1000),(int)arg1,*(uint32_t *)copyin(self->candidate,4));
    self->candidate=0;
}

pid$2:CoreGraphics:*CGSFlushAccelerator*:entry,
pid$2:CoreGraphics:*CGSFlushAccelerator*:return,
pid$2:GLEngine:*VIMDispatch_begin:entry,
pid$2:GLEngine:*VIMDispatch_draw_arrays:entry,
pid$2:GLEngine:*VIMDispatch_draw_elements:entry,
pid$2:GLEngine:*VIMDispatch_clear:entry,
pid$2:GLEngine:*VIMDispatch_call_list*:entry,
pid$2:GLEngine:*VIMGLIFlushMode*:entry,
pid$2:GLEngine:*VIMPresentDrawableKnown*:entry,
pid$2:GLEngine:*VIMPresentDrawableKnown*:return,
pid$2:GLEngine:*VIMImportAcceleratorBacking*:entry,
pid$2:GLEngine:*VIMImportAcceleratorBacking*:return,
pid$2:GLEngine:*VIMPrepareOrdinaryDrawing*:entry,
pid$2:libVIMPassthroughOpenGL*:DGImportDrawable:entry,
pid$2:libVIMPassthroughOpenGL*:*DGReadDrawable*:entry,
pid$2:libVIMPassthroughOpenGL*:*DGPublishBacking*:entry
/started && timestamp-started<100000000/
{
    printf("%6d us Finder %s:%s\n",(int)((timestamp-started)/1000),probefunc,probename);
}

fbt:*VMsvga*:*materializeGPU*:entry,
fbt:*VMsvga*:*readGPUSurface*:entry,
fbt:*VMsvga*:*surface_write_unlock_options*:entry,
fbt:*VMsvga*:*surface_write_lock_options*:entry
/started && timestamp-started<100000000 && pid==$1/
{
    printf("%6d us %s\n",(int)((timestamp-started)/1000),probefunc);
}

tick-25sec { exit(0); }
