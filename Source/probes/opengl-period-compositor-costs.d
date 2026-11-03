/* Finite wall-time attribution; names only, no private structures/arguments,
 * pixels, preferences or production polling. Nested rows are INCLUSIVE and
 * must not be summed as independent costs. -Z permits native references to
 * omit project-only modules; require a real compositor count separately.
 * Use only symbols verified in the actual paired artifacts: the compiler
 * inlines the typed GLI helpers, so observe their exported backend operation.
 * Instrumented wall times are attribution evidence, not clean FPS timings.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=4m
BEGIN {printf("READY period compositor costs pid=%d\n",$target);}
pid$target:CoreGraphics:CGXGLAccelComposite:entry,
pid$target:GLEngine:VIMImportAcceleratorBacking:entry,
pid$target:GLEngine:VIMPresentDrawableKnown:entry,
pid$target:GLEngine:VIMImportCGSTexture:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGUploadSurface:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGReadTypedDrawable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:entry
{self->depth[probefunc]++;self->started[probefunc,self->depth[probefunc]]=timestamp;}
pid$target:CoreGraphics:CGXGLAccelComposite:return,
pid$target:GLEngine:VIMImportAcceleratorBacking:return,
pid$target:GLEngine:VIMPresentDrawableKnown:return,
pid$target:GLEngine:VIMImportCGSTexture:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGUploadSurface:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGReadTypedDrawable:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:return
/self->depth[probefunc]/
{
    this->us=(timestamp-self->started[probefunc,self->depth[probefunc]])/1000;
    @calls[probefunc]=count();@wall_us[probefunc]=sum(this->us);
    @maximum_us[probefunc]=max(this->us);@latency_us[probefunc]=quantize(this->us);
    self->started[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
tick-12s {exit(0);}
ERROR {printf("INVALID compositor attribution error\n");exit(2);}
END {printf("CALLS\n");printa(@calls);printf("INCLUSIVE WALL us\n");printa(@wall_us);
    printf("MAXIMUM us\n");printa(@maximum_us);printf("LATENCY us\n");printa(@latency_us);}
