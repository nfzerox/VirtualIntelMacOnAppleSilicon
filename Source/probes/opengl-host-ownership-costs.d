/* Low-rate background ownership attribution, not per-GL-command tracing.
 * Metadata/function spans only: no packet/Apple-object memory reads, host
 * ptrace or security changes. Native GPU waits, CPU transfer and notification
 * spans overlap; never add their totals or treat them as frame/GPU times.
 * Require positive balanced entries/returns for EACH reported function.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY host ownership attribution target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGBackingTicketNotify:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGScatterSurfaceTransfer:entry
{
    self->depth[probefunc]++;
    self->wall[probefunc,self->depth[probefunc]]=timestamp;
    self->cpu[probefunc,self->depth[probefunc]]=vtimestamp;
    @entered[probefunc]=count();
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGBackingTicketNotify:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGScatterSurfaceTransfer:return
/self->depth[probefunc]/
{
    this->us=(timestamp-self->wall[probefunc,self->depth[probefunc]])/1000;
    @returned[probefunc]=count();@wall[probefunc]=sum(this->us);
    @cpu[probefunc]=sum((vtimestamp-self->cpu[probefunc,self->depth[probefunc]])/1000);
    @maximum[probefunc]=max(this->us);@latency[probefunc]=quantize(this->us);
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGBackingTicketNotify:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGScatterSurfaceTransfer:return
/self->depth[probefunc] && timestamp-self->wall[probefunc,self->depth[probefunc]]>=20000000/
{@slow[probefunc,ustack(12)]=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGBackingTicketNotify:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGScatterSurfaceTransfer:return
/self->depth[probefunc]/
{self->wall[probefunc,self->depth[probefunc]]=0;self->cpu[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
ERROR {printf("INVALID attribution; probe error\n");exit(2);}
END {
    printf("PHASE function entered returned inclusive_wall_us host_thread_cpu_us maximum_us (NESTED)\n");
    printa("%s %@d %@d %@d %@d %@d\n",@entered,@returned,@wall,@cpu,@maximum);
    printa(@latency);printf("Ownership spans >=20ms by caller\n");printa(@slow);
}
