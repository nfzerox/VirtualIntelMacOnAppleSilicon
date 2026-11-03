/* Finite host-side wall/CPU attribution of real transfer functions. No guest
 * payload/pixel reads, task suspension or per-call output. Require actual
 * symbols in the loaded executor; missing probes are not zero cost. Nested
 * scopes must not be added together. Counts are attribution, not clean FPS.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=4m
BEGIN {printf("READY Snow host transfer costs\n");}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSendDrawableSurface:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGImportDrawableSurface:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceUpload:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMTransfer:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMWait:entry
{self->at[probefunc]=timestamp;self->cpu[probefunc]=vtimestamp;}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSendDrawableSurface:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGImportDrawableSurface:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceUpload:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMTransfer:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMWait:return
/self->at[probefunc]/
{
    this->us=(timestamp-self->at[probefunc])/1000;
    @calls[probefunc]=count();@wall[probefunc]=sum(this->us);
    @cpu[probefunc]=sum((vtimestamp-self->cpu[probefunc])/1000);
    @maximum[probefunc]=max(this->us);@distribution[probefunc]=quantize(this->us);
    self->at[probefunc]=0;self->cpu[probefunc]=0;
}
tick-12s {exit(0);}
ERROR {printf("INVALID: transfer probe error\n");exit(2);}
END {
    printa("HOST %s calls=%@d wall_us=%@d cpu_us=%@d max_us=%@d\n",@calls,@wall,@cpu,@maximum);
    printa(@distribution);
}
