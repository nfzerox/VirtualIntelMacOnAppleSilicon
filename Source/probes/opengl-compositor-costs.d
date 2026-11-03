/* Bounded inclusive wall-time survey of the real active-window workload.
 * Run against WindowServer, wait for READY before starting input, and do
 * not add nested totals or interpret an instrumented capture as FPS.
 * Physical reference must report actual Intel-driver hits. The matched
 * 13F1911 x64 CGXUpdateMagicMenuBar has a real epilogue/RET at 0x28dcdd;
 * include it separately from MagicMirror, without summing nested costs.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN { began=timestamp;printf("READY compositor inclusive costs\n"); }
pid$target:CoreGraphics:CGXUpdateDisplay:entry,
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry,
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:entry,
pid$target:CoreGraphics:CGXGLAccelComposite:entry,
pid$target:CoreGraphics:gl_composite_core_animation:entry,
pid$target:CoreGraphics:CGXGLAccelFinish:entry,
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:entry,
pid$target:GLEngine:*VIMImportAcceleratorBacking*:entry,
pid$target:GLEngine:*VIMPresentDrawable*:entry
{
    self->depth[probefunc]++;
    @observed_entries[probefunc]=count();
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry,
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry,
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:entry,
pid$target:CoreGraphics:CGXGLAccelComposite:entry,
pid$target:CoreGraphics:gl_composite_core_animation:entry,
pid$target:CoreGraphics:CGXGLAccelFinish:entry,
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:entry,
pid$target:GLEngine:*VIMImportAcceleratorBacking*:entry,
pid$target:GLEngine:*VIMPresentDrawable*:entry
/self->depth[probefunc]==1/
{ self->start[probefunc]=timestamp; }
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXUpdateMagicMirror:return,
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:return,
pid$target:CoreGraphics:CGXGLAccelComposite:return,
pid$target:CoreGraphics:gl_composite_core_animation:return,
pid$target:CoreGraphics:CGXGLAccelFinish:return,
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:return,
pid$target:GLEngine:*VIMImportAcceleratorBacking*:return,
pid$target:GLEngine:*VIMPresentDrawable*:return
/self->depth[probefunc]==1 && self->start[probefunc]/
{
    this->us=(timestamp-self->start[probefunc])/1000;
    @calls[probefunc]=count();@us[probefunc]=sum(this->us);
    @max_us[probefunc]=max(this->us);@distribution[probefunc]=quantize(this->us);
    self->start[probefunc]=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXUpdateMagicMirror:return,
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:return,
pid$target:CoreGraphics:CGXGLAccelComposite:return,
pid$target:CoreGraphics:gl_composite_core_animation:return,
pid$target:CoreGraphics:CGXGLAccelFinish:return,
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:return,
pid$target:GLEngine:*VIMImportAcceleratorBacking*:return,
pid$target:GLEngine:*VIMPresentDrawable*:return
/self->depth[probefunc]/
{ self->depth[probefunc]--; }
pid$target:*Intel*:glrFlushContextToken:entry
{ @intel[probemod]=count(); }
tick-1s /timestamp-began>=16000000000/ { exit(0); }
END {
    printf("OBSERVED ENTRIES (absent/entry-only probes are NOT zero cost)\n");
    printa("%s entries=%@d\n",@observed_entries);
    printf("INCLUSIVE COSTS name calls wall-us max-us\n");
    printa("%s calls=%@d wall_us=%@d max_us=%@d\n",@calls,@us,@max_us);
    printa(@distribution);
    printf("ACTUAL INTEL DRIVER HITS (required for physical reference)\n");printa(@intel);
}
