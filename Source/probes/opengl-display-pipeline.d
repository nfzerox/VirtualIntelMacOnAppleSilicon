/* Actual host presentation admission during a real guest workload. Count
 * READY true/false and submit status, not advertised capabilities or frames.
 * All probes must match (no -Z). Wait for READY/live tracing before input.
 * Inclusive completion wall/thread CPU time is not GPU time or displayed FPS.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=4m
BEGIN {began=timestamp;printf("READY display admission target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGDisplaySenderReady:entry
{@ready_entries=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGDisplaySenderReady:return
{@ready[(uint32_t)arg1]=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGDisplaySubmitSealed:entry
{@submit_entries=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGDisplaySubmitSealed:return
{@submit_status[(uint32_t)arg1]=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:entry
{self->wall=timestamp;self->cpu=vtimestamp;@wait_entries=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:return
/self->wall/
{
    @wait_returns=count();@wait_wall=sum((timestamp-self->wall)/1000);
    @wait_cpu=sum((vtimestamp-self->cpu)/1000);
    @wait_result[(uint32_t)arg1]=count();self->wall=0;self->cpu=0;
}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {
    printf("READY entries, boolean return counts\n");printa(@ready_entries);printa(@ready);
    printf("SUBMIT entries, GLenum return counts (0 is success)\n");printa(@submit_entries);printa(@submit_status);
    printf("WAIT entered returned inclusive_wall_us inclusive_thread_cpu_us\n");
    printa("%@d %@d %@d %@d\n",@wait_entries,@wait_returns,@wait_wall,@wait_cpu);
    printa(@wait_result);
}
