/* Physical Mavericks Intel ownership survey. Run separately from FPS tests:
 * sudo dtrace -Z -q -p WINDOWSERVER_PID -s opengl-intel-cache-ownership.d
 * Wait for READY, then move a real Finder window. Reject a run without the
 * actual Intel driver hit. No pixels, memory contents or addresses are read.
 * GPUSupport's two cache helpers forward (address,length) to sys_dcache_flush;
 * those argument meanings are established from the physical binary. */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN { began=timestamp;printf("READY Intel cache ownership survey\n"); }
pid$target:*Intel*:glrFlushContextToken:entry
{
    @intel[probemod]=count();
    intel_seen=1;
}
pid$target:*Mercury*:gpusFlushMemoryForIn:entry,
pid$target:*Mercury*:gpusFlushMemoryForOut:entry
{
    @calls[probefunc,ustack(8)]=count();
    @bytes[probefunc]=sum(arg1);
    @sizes[probefunc]=quantize(arg1);
    self->start[probefunc]=timestamp;
}
pid$target:*Mercury*:gpusFlushMemoryForIn:return,
pid$target:*Mercury*:gpusFlushMemoryForOut:return
/self->start[probefunc]/
{
    @us[probefunc]=sum((timestamp-self->start[probefunc])/1000);
    self->start[probefunc]=0;
}
pid$target:IOAccelerator:IOAccelResourceFinishSysMem:entry,
pid$target:IOAccelerator:IOAccelGLContextFinishResourceSysMem:entry,
pid$target:IOAccelerator:IOAccelGLContextFlushResourceSysMem:entry
{
    @resources[probefunc,ustack(8)]=count();
}
/* These are exported by the actual physical Intel driver. Submission and
 * queue finish must be distinguished from per-resource CPU coherence. */
pid$target:*Intel*:gldFlushQueue:entry,
pid$target:*Intel*:gldFinishQueue:entry,
pid$target:*Intel*:gldFinishFenceOnQueue:entry
{@queues[probefunc,ustack(8)]=count();}
tick-1s /timestamp-began>=12000000000/ { exit(0); }
END {
    printf("END actual-Intel-driver-observed=%d\n",(int)intel_seen);
    printa(@intel);printa(@calls);printa(@bytes);printa(@sizes);printa(@us);printa(@resources);printa(@queues);
}
