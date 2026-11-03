/* Finite real MagicMirror attribution, not a blanket function-name guess.
 * Source-defined bridge probes are absent on physical Intel; -Z is allowed
 * ONLY for those optional probes. Require positive MagicMirror and actual
 * Intel-driver hits on the physical reference. No pixels/vertex data/pointers
 * are printed: the own packet's fixed opcode is the only copied metadata.
 * Inclusive nested times must not be added or called displayed FPS.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY nested MagicMirror costs target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry
{self->mirror++;self->mirror_at[self->mirror]=timestamp;@mirror_entered=count();}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return
/self->mirror/
{
    @mirror_returned=count();@mirror_us=sum((timestamp-self->mirror_at[self->mirror])/1000);
    self->mirror_at[self->mirror]=0;self->mirror--;
}
pid$target:CoreGraphics:WSRedrawSpecifiedWindowsToDisplayAndRegion:entry,
pid$target:CoreGraphics:CGXGLAccelComposite:entry,
pid$target:CoreGraphics:CGXGLAccelFinish:entry,
pid$target:CoreGraphics:gl_composite_core_animation:entry,
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:entry,
pid$target:GLEngine:*VIMImportAcceleratorBacking*:entry,
pid$target:GLEngine:*VIMPresentDrawableKnown*:entry,
pid$target:GLEngine:*VIMRetainCPUPublishedDrawable*:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGEndBackingRender:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGFlushDrawableState:entry
/self->mirror/
{
    self->depth[probefunc]++;self->at[probefunc,self->depth[probefunc]]=timestamp;
    @entered[probefunc]=count();
}
pid$target:CoreGraphics:WSRedrawSpecifiedWindowsToDisplayAndRegion:return,
pid$target:CoreGraphics:CGXGLAccelComposite:return,
pid$target:CoreGraphics:CGXGLAccelFinish:return,
pid$target:CoreGraphics:gl_composite_core_animation:return,
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:return,
pid$target:GLEngine:*VIMImportAcceleratorBacking*:return,
pid$target:GLEngine:*VIMPresentDrawableKnown*:return,
pid$target:GLEngine:*VIMRetainCPUPublishedDrawable*:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGEndBackingRender:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGFlushDrawableState:return
/self->depth[probefunc]/
{
    this->us=(timestamp-self->at[probefunc,self->depth[probefunc]])/1000;
    @returned[probefunc]=count();@us[probefunc]=sum(this->us);@max_us[probefunc]=max(this->us);
    self->at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/self->mirror/
{
    self->submit++;self->op[self->submit]=*(uint32_t *)copyin(arg1,4);
    self->submitted[self->submit]=timestamp;@ops[self->op[self->submit]]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return
/self->submit/
{
    @op_us[self->op[self->submit]]=sum((timestamp-self->submitted[self->submit])/1000);
    self->op[self->submit]=0;self->submitted[self->submit]=0;self->submit--;
}
pid$target:*Intel*:glrFlushContextToken:entry
/self->mirror/
{@intel[probemod]=count();}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {
    printf("MIRROR entered returned inclusive_us\n");printa("%@d %@d %@d\n",@mirror_entered,@mirror_returned,@mirror_us);
    printf("NESTED function entered returned inclusive_us max_us\n");
    printa("%s %@d %@d %@d %@d\n",@entered,@returned,@us,@max_us);
    printf("OWN PACKET opcode calls inclusive_us (no payloads)\n");printa("%x %@d %@d\n",@ops,@op_us);
    printf("ACTUAL INTEL DRIVER inside MagicMirror (required on physical)\n");printa(@intel);
}
