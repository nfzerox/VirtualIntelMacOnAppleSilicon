/* Bounded owned-host executor attribution. Verify READY/live tracing before
 * actual input, and balanced entry/return counts afterward. Inclusive wall
 * and thread-running time are nested, not additive frame/GPU measurements.
 * No missing-probe suppression, pixel reads, pointer payloads or hot logging.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY completion wall/thread-CPU target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMWait:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMCopyDrawable:entry
{
    self->depth[probefunc]++;
    self->wall[probefunc,self->depth[probefunc]]=timestamp;
    self->cpu[probefunc,self->depth[probefunc]]=vtimestamp;
    @entered[probefunc]=count();
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMWait:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMCopyDrawable:return
/self->depth[probefunc] && self->wall[probefunc,self->depth[probefunc]]/
{
    this->wall=(timestamp-self->wall[probefunc,self->depth[probefunc]])/1000;
    this->cpu=(vtimestamp-self->cpu[probefunc,self->depth[probefunc]])/1000;
    @returned[probefunc]=count();@wall[probefunc]=sum(this->wall);@cpu[probefunc]=sum(this->cpu);
    @max_wall[probefunc]=max(this->wall);@max_cpu[probefunc]=max(this->cpu);
    @latency[probefunc]=quantize(this->wall);
    self->wall[probefunc,self->depth[probefunc]]=0;self->cpu[probefunc,self->depth[probefunc]]=0;
    self->depth[probefunc]--;
}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {
    printf("FUNCTION entered returned inclusive_wall_us inclusive_thread_cpu_us max_wall_us max_cpu_us\n");
    printa("%s %@d %@d %@d %@d %@d %@d\n",@entered,@returned,@wall,@cpu,@max_wall,@max_cpu);
    printa(@latency);
}
