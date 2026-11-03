/* Finite real-window drag survey. Native SkyLight and our GLI use the same
 * compositor boundaries; native driver calls are not replaced or injected.
 * Inclusive wall/thread-CPU scopes overlap and are not additive frame time.
 * Start the real Finder workload after READY. Missing return counts invalidate
 * that scope. This is diagnostic-only; nothing runs in the production hot path.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN { began=timestamp;self->last=0;printf("READY Sierra compositor costs\n"); }
pid$target::CGXGLAccelFinish:entry,
pid$target::VIMGLIFlushMode:entry,
pid$target::VIMPresentDrawableKnown:entry,
pid$target::VIMImportAcceleratorBacking:entry,
pid$target::VIMPublishIOSurfaceWritesLocked:entry
{
    self->depth[probefunc]++;
    self->wall[probefunc,self->depth[probefunc]]=timestamp;
    self->cpu[probefunc,self->depth[probefunc]]=vtimestamp;
    @entered[probefunc]=count();
}
pid$target::CGXUpdateDisplay:entry
{
    @updates=count();
    if(self->last)@gaps_us=quantize((timestamp-self->last)/1000);
    self->last=timestamp;
}
/* Mode's render_only argument may be optimized away on newer-target builds.
 * Count the real wrappers instead of interpreting a dead argument register. */
pid$target::VIMGLIFlush:entry,
pid$target::VIMGLIFlushRender:entry,
pid$target::VIMGLIFinish:entry,
pid$target::VIMGLIFinishRender:entry
{ @flush_modes[probefunc]=count(); }
pid$target::VIMPresentDrawableKnown:entry
{ @present_bindings[arg1,arg2]=count(); }
pid$target:libVIMPassthroughOpenGL.dylib:glGetTexParameteriv:entry
{ @texture_query[arg0,arg1]=count(); }
/* Own packet's first fixed-width field only; no Apple object offsets. */
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/arg2/
{ @sync_ops[*(uint32_t *)copyin(arg1,4)]=count(); }
pid$target::CGXGLAccelFinish:return,
pid$target::VIMGLIFlushMode:return,
pid$target::VIMPresentDrawableKnown:return,
pid$target::VIMImportAcceleratorBacking:return,
pid$target::VIMPublishIOSurfaceWritesLocked:return
/self->depth[probefunc]/
{
    this->wall=(timestamp-self->wall[probefunc,self->depth[probefunc]])/1000;
    this->cpu=(vtimestamp-self->cpu[probefunc,self->depth[probefunc]])/1000;
    @returned[probefunc]=count();@wall[probefunc]=sum(this->wall);
    @cpu[probefunc]=sum(this->cpu);@latency_us[probefunc]=quantize(this->wall);
    self->wall[probefunc,self->depth[probefunc]]=0;
    self->depth[probefunc]--;
}
tick-1s /timestamp-began>=10000000000/ {exit(0);}
ERROR {printf("INVALID compositor trace\n");exit(2);}
END {
    printf("FUNCTION entered returned inclusive_wall_us inclusive_thread_cpu_us\n");
    printa("%s %@d %@d %@d %@d\n",@entered,@returned,@wall,@cpu);
    /* Sierra's optimized update routine has no usable pid return probe.
     * Entry cadence is real; do not invent zero-cost completed durations. */
    printf("COMPOSITOR ENTRIES\n");printa(@updates);
    printf("COMPOSITOR ENTRY GAPS us\n");printa(@gaps_us);
    printf("INCLUSIVE WALL DISTRIBUTIONS us\n");printa(@latency_us);
    printf("FLUSH WRAPPERS calls\n");printa(@flush_modes);
    printf("PRESENT BINDINGS known draw_framebuffer calls\n");printa(@present_bindings);
    printf("TEXTURE QUERIES target pname calls\n");printa(@texture_query);
    printf("SYNCHRONOUS WIRE OPCODES calls\n");printa("%x %@d\n",@sync_ops);
}
