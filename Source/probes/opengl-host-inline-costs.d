/* Bounded owned-process attribution for the real inline GL executor.
 * Compare entry/return counts before interpreting inclusive wall/CPU totals;
 * nested totals are not additive and this is not a presented-frame benchmark.
 * No pixel/pointer payloads, code changes, persistent polling or per-call logs.
 * Native submission stacks distinguish implicit context-release work from
 * explicit readback/completion. Start the actual Finder drag after READY.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {printf("READY inline/context/completion costs target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGInlineWritev:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGImportDrawableSurface:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceUpload:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMCopyDrawable:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMWait:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSendDrawableSurface:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGFlushDrawableState:entry,
pid$target:libGL.dylib:CGLSetCurrentContext:entry
{
    self->depth[probefunc]++;
    self->wall[probefunc,self->depth[probefunc]]=timestamp;
    self->cpu[probefunc,self->depth[probefunc]]=vtimestamp;
    @entered[probefunc]=count();
}
pid$target:libGL.dylib:CGLSetCurrentContext:entry
{ @switch_null[arg0==0]=count(); }
/* Public GL entry points are dispatch tail jumps on this host. Count them,
 * but never pretend their absent/premature return probe times GPU work. */
pid$target:libGL.dylib:glFinish:entry,
pid$target:libGL.dylib:glFlush:entry,
pid$target:libGL.dylib:glClientWaitSync:entry
{ @gl_entries[probefunc]=count(); }
pid$target:libVIMPassthroughOpenGLHost.dylib:DGInlineWritev:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGImportDrawableSurface:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceUpload:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMCopyDrawable:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMWait:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSendDrawableSurface:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGFlushDrawableState:return,
pid$target:libGL.dylib:CGLSetCurrentContext:return
/self->depth[probefunc]/
{
    this->wall=(timestamp-self->wall[probefunc,self->depth[probefunc]])/1000;
    this->cpu=(vtimestamp-self->cpu[probefunc,self->depth[probefunc]])/1000;
    @returned[probefunc]=count();
    @wall[probefunc]=sum(this->wall);@cpu[probefunc]=sum(this->cpu);
    @maximum[probefunc]=max(this->wall);
    @latency[probefunc]=quantize(this->wall);
    self->wall[probefunc,self->depth[probefunc]]=0;
    self->cpu[probefunc,self->depth[probefunc]]=0;
    self->depth[probefunc]--;
}
pid$target:AppleIntelICLGraphicsGLDriver:intelSubmitCommands:entry
{ @submissions[ustack(12)]=count(); }
tick-16s {exit(0);}
ERROR {printf("INVALID: inline-cost probe error\n");exit(2);}
END {
    printf("FUNCTION entered returned inclusive_wall_us inclusive_thread_cpu_us max_wall_us\n");
    printa("%s %@d %@d %@d %@d %@d\n",@entered,@returned,@wall,@cpu,@maximum);
    printf("CONTEXT switch_is_null calls\n");printa("%u %@d\n",@switch_null);
    printf("Public GL entry counts only\n");printa("%s %@d\n",@gl_entries);
    printa(@latency);
    printf("Native submission callers (not additive CPU time)\n");printa(@submissions);
}
