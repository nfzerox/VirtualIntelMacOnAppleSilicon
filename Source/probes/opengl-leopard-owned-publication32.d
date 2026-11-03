/* Read-only i386 pair72/support73 metadata, NOT a generic Apple ABI decoder.
 * Rechecked against this candidate's actual DWARF: VIMContext drawable0xd24,
 * error0xc88; owned drawable width20, unflipped92, view104, sharedVRAM184,
 * render-scope252. Wrapper addresses escape via the native C dispatch ABI;
 * do not decode internal static helpers' compiler-private register arguments.
 * Never use with another build until its DWARF layout is checked again. */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN {budget=256;printf("READY owned Leopard32 publication\n");}
pid$target:GLEngine:gliSetInteger:entry
/budget>0 && arg0 && arg2 && (arg1==0x3e5 || arg1==0x3e6 || arg1==0x3e7)/
{
    this->d=*(uint32_t *)copyin(arg0+0xd24,4);
    this->size=(int32_t *)copyin(this->d+20,8);
    this->view=(int32_t *)copyin(this->d+104,12);
    this->a=(uint32_t *)copyin(arg2,20);
    budget--;printf("%llu IMPORT key=%x id=%x request=%u,%u source=%d,%d view=%d origin=%d,%d flip=%d\n",
        timestamp,arg1,this->a[0],this->a[3],this->a[4],this->size[0],this->size[1],
        this->view[0],this->view[1],this->view[2],*(int32_t *)copyin(this->d+92,4));
}
pid$target:GLEngine:VIMGLIFlush:entry,
pid$target:GLEngine:VIMGLIFinish:entry
/budget>0 && arg0/
{
    self->context=arg0;this->d=*(uint32_t *)copyin(arg0+0xd24,4);
    this->size=(int32_t *)copyin(this->d+20,8);
    this->view=(int32_t *)copyin(this->d+104,12);
    budget--;printf("%llu %s size=%d,%d view=%d origin=%d,%d flip=%d shared=%d scope=%d error=%x\n",
        timestamp,probefunc,this->size[0],this->size[1],this->view[0],this->view[1],this->view[2],
        *(int32_t *)copyin(this->d+92,4),*(int32_t *)copyin(this->d+184,4),
        *(int32_t *)copyin(this->d+252,4),*(uint32_t *)copyin(arg0+0xc88,4));
}
pid$target:GLEngine:VIMPresentDrawableKnown:return
/budget>0 && self->context/
{budget--;printf("%llu PRESENT_END error=%x\n",timestamp,*(uint32_t *)copyin(self->context+0xc88,4));self->context=0;}
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishVRAMDrawable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawableRectRows:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawableRectMasked:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishSurfaceRegions:entry
/budget>0/
{budget--;printf("%llu ROUTE %s\n",timestamp,probefunc);}
tick-30s {exit(0);}
