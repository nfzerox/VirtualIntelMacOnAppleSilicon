/* Finite compositor-side video handoff observation. Enumerate the actual
 * process's symbols first; missing probes or zero counts are not proof of
 * absent frames. Run as guest root, then seek/play in the desktop session.
 * No injected code, object-layout guesses, pixel reads or state changes.
 */
#pragma D option quiet
#pragma D option bufsize=1m
#pragma D option aggsize=1m

BEGIN { printf("READY 12-second video consumer trace pid=%d\n",$target); }

pid$target:IOKit:IOAccelCreateSurface:entry
/created_surfaces<8/
{
    created_surfaces++;
    printf("CREATE SURFACE id=%u mode=%x\n",(unsigned)arg1,(unsigned)arg2);
}

pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/arg1 && shape_records<16/
{
    shape_records++;
    printf("SHAPE surface=%p options=%x framebuffer=%u\n",arg0,(unsigned)arg2,(unsigned)arg3);
    tracemem(copyin(arg1,20),20);
}

/* Preserve the nesting on the calling thread so the error can be attributed
 * to bind, geometry, or backing acquisition rather than a combined result. */
pid$target:GLEngine:gliSetInteger:entry
/arg1==0x3e6 && surface_calls<16/
{ self->video_surface=1; surface_calls++; }

pid$target:GLEngine:gliSetInteger:return
{ self->video_surface=0; }

pid$target:*:IOConnectCallMethod:entry,
pid$target:*:IOConnectCallStructMethod:entry
/self->video_surface/
{ self->surface_selector=arg1; self->surface_io=1; }

pid$target:*:IOConnectCallMethod:return,
pid$target:*:IOConnectCallStructMethod:return
/self->video_surface && self->surface_io/
{
    printf("SURFACE IO %s selector=%u result=%x\n",probefunc,
        (unsigned)self->surface_selector,(unsigned)arg1);
    self->surface_io=0;
}

/* This probe targets the x86_64 guest. The two trailing varargs follow the
 * six register arguments on its System V stack; these are renderer-owned
 * diagnostic arguments, not inferred system-framework layouts. */
pid$target:GLEngine:VIMGLITrace:entry
/arg0 && copyinstr(arg0)=="texture op=%x surface=%x target=%x size=%u,%u flags=%x lock=%x" && cgs_locks<8/
{
    cgs_locks++;
    this->stack=(uint64_t *)copyin(uregs[R_RSP]+8,16);
    printf("CGS LOCK key=%x id=%u target=%x size=%ux%u flags=%x result=%x\n",
        (unsigned)arg1,(unsigned)arg2,(unsigned)arg3,(unsigned)arg4,(unsigned)arg5,
        (unsigned)this->stack[0],(unsigned)this->stack[1]);
}

/* Observe the existing failure-only diagnostic call before its logging gate.
 * These arguments are from this renderer's source, not private object fields;
 * no tracing environment or compositor restart is required. */
pid$target:GLEngine:VIMGLITrace:entry
/arg0 && copyinstr(arg0)=="surface texture failed key=%x error=%d id=%x target=%x internal=%x size=%u,%u" && failed_views<8/
{
    failed_views++;
    printf("FAILED VIEW key=%x error=%d id=%u target=%x internal=%x\n",
        (unsigned)arg1,(int)arg2,(unsigned)arg3,(unsigned)arg4,(unsigned)arg5);
}

pid$target:QuartzCore:CAImageQueueCopyTexture:entry,
pid$target:QuartzCore:CAShmemImageQueueCopyImageInfo:entry,
pid$target:QuartzCore:CAShmemImageQueueUpdate:entry,
pid$target:QuartzCore:*ImageQueue*update*:entry,
pid$target:QuartzCore:*create_surface_from_iosurface*:entry,
pid$target:*:CGLTexImageIOSurface2D:entry,
pid$target:*:gliSetInteger:entry,
pid$target:*:cglsSetInteger:entry
{ @calls[probemod,probefunc]=count(); }

pid$target:GLEngine:gliAttachDrawableWithOptions:entry
{
    @attachments[(unsigned)arg1]=count();
    self->attach_type=arg1;self->attach_context=arg0;
    self->attach_surface=0;
}

pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/arg1==0x5a && arg2/
{ self->attach_surface=*(uint32_t *)copyin(arg2+8,4); }

pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/arg1==0x5a && arg2 && attach_records<8/
{
    attach_records++;
    printf("ATTACH RECORD ctx=%p data=%p\n",arg0,arg2);
    tracemem(copyin(arg2,48),48);
}

pid$target:CoreGraphics:cglsAttachPBuffer:entry
/arg1 && pbuffer_records<8/
{
    pbuffer_records++;
    printf("CGLS PBUFFER ctx=%p record=%p\n",arg0,arg1);
    tracemem(copyin(arg1,48),48);
}

pid$target:GLEngine:gliAttachDrawableWithOptions:return
/self->attach_context && arg1 && attach_failures<12/
{
    attach_failures++;
    printf("ATTACH FAILED ctx=%p type=%x surface=%u result=%d\n",
        self->attach_context,(unsigned)self->attach_type,
        (unsigned)self->attach_surface,(int)arg1);
}

pid$target:GLEngine:gliAttachDrawableWithOptions:return
/self->attach_context/
{
    @attach_results[self->attach_type,(unsigned)arg1]=count();
    self->attach_context=0;
}

pid$target:GLEngine:gliSetInteger:entry
/arg2 && arg1==0x3e5 && surface_views<24/
{
    surface_views++;
    this->a=(uint32_t *)copyin(arg2,32);
    printf("VIEW key=%x ctx=%p id=%u target=%x internal=%x size=%ux%u flags=%x mipmaps=%u\n",
        (unsigned)arg1,arg0,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4],this->a[6],this->a[7]);
}

pid$target:GLEngine:gliSetInteger:entry
/arg2 && (arg1==0x3e6 || arg1==0x3e7) && surface_views<24/
{
    surface_views++;
    this->a=(uint32_t *)copyin(arg2,20);
    printf("VIEW key=%x ctx=%p id=%u target=%x internal=%x size=%ux%u\n",
        (unsigned)arg1,arg0,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4]);
}

pid$target:QuartzCore:*create_surface_from_iosurface*:entry
/surfaces<8/
{
    surfaces++;
    printf("SURFACE %s this=%p width=%d height=%d format=%x surface=%p\n",
        probefunc,arg0,(int)arg1,(int)arg2,(unsigned)arg3,arg4);
    ustack(10);
}

pid$target:*:gliSetInteger:entry,
pid$target:*:cglsSetInteger:entry
/(uint32_t)arg1==0x38e && arg2 && imports<8/
{
    imports++;
    this->words=(uint32_t *)copyin(arg2,32);
    printf("IMPORT %s ctx=%p id=%u target=%x internal=%x size=%ux%u format=%x type=%x plane=%u\n",
        probefunc,arg0,this->words[0],this->words[1],this->words[2],
        this->words[3],this->words[4],this->words[5],this->words[6],this->words[7]);
}

pid$target:GLEngine:CGLTexImageIOSurface2D:entry
{ self->in_import=1; }

pid$target:GLEngine:CGLTexImageIOSurface2D:return
/self->in_import/
{
    @import_results[(int)arg1]=count();
    self->in_import=0;
}

tick-12sec { exit(0); }
END {
    printa("%-18s %-100s %@d\n",@calls);
    printa("GLEngine import return=%d count=%@d\n",@import_results);
    printa("GLEngine drawable type=%x count=%@d\n",@attachments);
    printa("GLEngine attach type=%x result=%d count=%@d\n",@attach_results);
}
