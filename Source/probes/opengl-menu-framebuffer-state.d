/* Finite metadata-only menu regeneration comparison on native Intel and the
 * guest. Trigger one wallpaper change after READY, then restore it. Wildcards
 * avoid requiring private symbols absent from one of the paired engines.
 * x86_64 10.8 ONLY: the layer->filter (+0xc8), filter->definition (+8)
 * and definition type (+0xc) reads are verified in actual 12F45 Ghidra output
 * and 12F2560 physical Intel disassembly. Requalify before another OS/build.
 * No floating-point registers or desktop pixels decoded. */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { printf("READY menu framebuffer survey\n"); }
pid$target:CoreGraphics:*CGXGLCompositeLayer:entry
{ self->filter=*(uint64_t *)copyin(arg0+0xc8,8); }
pid$target:CoreGraphics:*CGXGLCompositeLayer:entry
/self->filter/
{ self->definition=*(uint64_t *)copyin(self->filter+8,8); }
pid$target:CoreGraphics:*CGXGLCompositeLayer:entry
/self->filter && self->definition/
{ self->menu=(*(uint32_t *)copyin(self->definition+0xc,4)==1); }
pid$target:CoreGraphics:*CGXGLCompositeLayer:return
{ self->menu=0;self->filter=0;self->definition=0; }
pid$target:GLEngine:*glViewport*Exec*:entry,
pid$target:GLEngine:*glScissor*Exec*:entry,
pid$target:GLEngine:*glBindFramebuffer*Exec*:entry,
pid$target:GLEngine:*glBindTexture*Exec*:entry,
pid$target:GLEngine:*glDrawBuffer*Exec*:entry,
pid$target:GLEngine:*glReadBuffer*Exec*:entry,
pid$target:GLEngine:*glEnable*Exec*:entry,
pid$target:GLEngine:*glDisable*Exec*:entry,
pid$target:GLEngine:*glBegin*Exec*:entry,
pid$target:GLEngine:*VIMDispatch_viewport:entry,
pid$target:GLEngine:*VIMDispatch_scissor:entry,
pid$target:GLEngine:*VIMDispatch_bind_framebuffer_EXT:entry,
pid$target:GLEngine:*VIMDispatch_bind_texture:entry,
pid$target:GLEngine:*VIMDispatch_draw_buffer:entry,
pid$target:GLEngine:*VIMDispatch_read_buffer:entry,
pid$target:GLEngine:*VIMDispatch_enable:entry,
pid$target:GLEngine:*VIMDispatch_disable:entry,
pid$target:GLEngine:*VIMDispatch_begin:entry
/self->menu && events++<3000/
{ printf("%llu %s ctx=%x args=%x,%x,%x,%x\n",(unsigned long long)timestamp,probefunc,
    (uint32_t)arg0,(uint32_t)arg1,(uint32_t)arg2,(uint32_t)arg3,(uint32_t)arg4); }
tick-12sec { exit(0); }
