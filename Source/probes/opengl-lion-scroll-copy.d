/* Lion CoreGraphics x86_64, confirmed on build 11G63.
 * sudo dtrace -Zqs opengl-lion-scroll-copy.d WINDOWSERVER_PID
 * Drag the list scrollbar after READY. This is a finite diagnostic, not
 * an input injection or a renderer policy. It samples a few backing pixels
 * from the active scroll only; use a non-sensitive test folder.
 */
BEGIN { printf("READY\n"); }

pid$1:CoreGraphics:CGSBackingStoreCopy:entry
{
    self->scroll = 1;
    self->base = 0;
    self->pitch = 0;
    printf("scroll dx=%d dy=%d\n",(int)arg2,(int)arg3);
}

pid$1:CoreGraphics:CGBlt_copyBytes:entry
/self->scroll && arg0>512 && arg1>10/
{
    /* Sample a row inside the text, not the uniform top edge of the clip. */
    self->sample = 9*(uint32_t)arg4+64;
    self->dst = arg3+9*(uint32_t)arg5+64;
    printf("blt widthBytes=%u rows=%u dst-src=%d rowbytes=%d/%d\n",
        (uint32_t)arg0,(uint32_t)arg1,(int)(arg3-arg2),(int)arg4,(int)arg5);
    printf("source before\n"); tracemem(copyin(arg2+self->sample,128),128);
    printf("destination before\n"); tracemem(copyin(self->dst,128),128);
}

pid$1:CoreGraphics:CGBlt_copyBytes:return
/self->dst/
{
    printf("destination after\n"); tracemem(copyin(self->dst,128),128);
    sample_offset = self->base && self->dst>=self->base ? self->dst-self->base : (uint64_t)-1;
    sample_address = self->dst;
    sample_size = self->pitch;
    sample_time = timestamp;
    self->dst = 0;
}

pid$1:CoreGraphics:CGXBackingStoreDataLock:entry
/self->scroll/
{
    self->baseout = arg3;
    self->pitchout = arg4;
}

pid$1:CoreGraphics:CGXBackingStoreDataLock:return
/self->baseout && !arg1/
{
    self->base = *(uint64_t *)copyin(self->baseout,8);
    self->pitch = *(uint32_t *)copyin(self->pitchout,4);
}

pid$1:libVIMPassthroughOpenGL*:*DGUploadSurface:entry
/sample_time && timestamp-sample_time<1000000000 && arg4==sample_size &&
    sample_offset<arg4*arg3 && arg4*arg3-sample_offset>=128/
{
    printf("candidate upload %dx%d stride=%u epoch=%llu sampleOffset=%llu\n",
        (int)arg2,(int)arg3,(uint32_t)arg4,
        (unsigned long long)arg5,(unsigned long long)sample_offset);
    printf("upload mapping\n"); tracemem(copyin(arg1+sample_offset,128),128);
    printf("original CPU mapping at upload\n"); tracemem(copyin(sample_address,128),128);
}

pid$1:CoreGraphics:CGXHWMoveRegion:entry,
pid$1:CoreGraphics:CGXBackingStoreDataLock:entry,
pid$1:CoreGraphics:CGXBackingStoreDataUnlock:entry,
pid$1:IOKit:IOAccelWriteLockSurfaceWithOptions:entry,
pid$1:IOKit:IOAccelWriteUnlockSurfaceWithOptions:entry
/self->scroll/
{
    @[probefunc] = count();
}

pid$1:CoreGraphics:CGSBackingStoreCopy:return
/self->scroll/
{
    printf("backing-copy return=%d\n",(int)arg1);
    self->scroll = 0;
    self->base = 0;
    self->baseout = 0;
    self->pitch = 0;
    self->pitchout = 0;
}

pid$1:CoreGraphics:CGSBackingStoreCopy:return,
pid$1:CoreGraphics:*synchronizeBackingSurface:entry,
pid$1:CoreGraphics:*synchronizeBackingSurface:return,
pid$1:CoreGraphics:CGXBackingStoreActivateFrontBuffer:entry,
pid$1:CoreGraphics:CGXBackingStoreActivateFrontBuffer:return,
pid$1:CoreGraphics:CGXBackingStoreTextureGet:entry,
pid$1:CoreGraphics:CGXGLSetTextureSurface:entry
/sample_time && timestamp-sample_time<500000000/
{
    printf("original CPU at %s:%s = 0x%x\n",probefunc,probename,
        *(uint32_t *)copyin(sample_address,4));
}

tick-25sec { exit(0); }
