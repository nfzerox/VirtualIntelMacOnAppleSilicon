/* Measure the remaining CPU materialization class, not guessed bandwidth.
 * Own source declares DGScatterSurfaceTransfer(plan, surface, gathering).
 * Lock/copy-window/unlock spans partition that invocation (GetBaseAddress and
 * loop/control overhead remain in the copy window). Completion wait is a
 * SEPARATE inclusive boundary; never add it to an enclosing ticket duration.
 * Exact executor x64 FFFA8250-BA1F-356A-9BE7-C7F7C524EFBD: Wait+60 is
 * immediately AFTER pthread_mutex_lock, before CGLGetCurrentContext. Its
 * entry-to-marker span bounds queue-lock contention plus tiny entry overhead.
 * Metadata only, finite and buffered; no pointers/pixels or Apple layouts.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=4m
BEGIN {began=timestamp;printf("READY host GL residency target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGScatterSurfaceTransfer:entry
{
    self->scatter_at=timestamp;self->gathering=(uint32_t)arg2;
    self->lock_us=(uint64_t)0;self->copy_us=(uint64_t)0;self->unlock_us=(uint64_t)0;
    self->copy_at=0;@entered[self->gathering]=count();
}
pid$target:IOSurface:IOSurfaceLock:entry /self->scatter_at/
{self->lock_at=timestamp;@lock_entered[self->gathering]=count();}
pid$target:IOSurface:IOSurfaceLock:return /self->scatter_at && self->lock_at/
{
    self->lock_us+=(timestamp-self->lock_at)/1000;self->lock_at=0;
    self->copy_at=timestamp;@lock_returned[self->gathering]=count();
}
pid$target:IOSurface:IOSurfaceUnlock:entry /self->scatter_at/
{
    if(self->copy_at)self->copy_us+=(timestamp-self->copy_at)/1000;
    self->copy_at=0;self->unlock_at=timestamp;@unlock_entered[self->gathering]=count();
}
pid$target:IOSurface:IOSurfaceUnlock:return /self->scatter_at && self->unlock_at/
{
    self->unlock_us+=(timestamp-self->unlock_at)/1000;self->unlock_at=0;
    @unlock_returned[self->gathering]=count();
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGScatterSurfaceTransfer:return /self->scatter_at/
{
    this->us=(timestamp-self->scatter_at)/1000;
    @returned[self->gathering]=count();@total[self->gathering]=sum(this->us);
    @maximum[self->gathering]=max(this->us);@distribution[self->gathering]=quantize(this->us);
    @lock[self->gathering]=sum(self->lock_us);@copy[self->gathering]=sum(self->copy_us);
    @unlock[self->gathering]=sum(self->unlock_us);self->scatter_at=0;
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:entry
{self->wait_at=timestamp;@wait_entered=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:60 /self->wait_at/
{
    @wait_locked=count();
    @lock_callers[ustack(20)]=count();
    @lock_caller_us[ustack(20)]=sum((timestamp-self->wait_at)/1000);
    @lock_caller_max[ustack(20)]=max((timestamp-self->wait_at)/1000);
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCompletionQueueWait:return /self->wait_at/
{
    this->us=(timestamp-self->wait_at)/1000;
    @wait_returned=count();@wait_total=sum(this->us);@wait_max=max(this->us);
    @wait_distribution=quantize(this->us);
    @wait_callers[ustack(20)]=count();@wait_caller_us[ustack(20)]=sum(this->us);
    self->wait_at=0;
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID host GL residency probe error\n");exit(2);}
END {
    printf("TRANSFER gathering entered returned total_us max_us lock_us copy_window_us unlock_us\n");
    printa("%u %@d %@d %@d %@d %@d %@d %@d\n",@entered,@returned,@total,@maximum,@lock,@copy,@unlock);
    printf("LOCK_BALANCE gathering lock_entered lock_returned unlock_entered unlock_returned\n");
    printa("%u %@d %@d %@d %@d\n",@lock_entered,@lock_returned,@unlock_entered,@unlock_returned);
    printa(@distribution);
    printf("SEPARATE completion-wait entered returned inclusive_us max_us\n");
    printa(@wait_entered);printa(@wait_returned);printa(@wait_total);printa(@wait_max);printa(@wait_distribution);
    printf("Completion wait actual callers count inclusive_us (not per-frame costs)\n");
    printa(@wait_callers,@wait_caller_us);
    printf("Exact post-lock marker count (require balanced entry/marker/return)\n");printa(@wait_locked);
    printf("Wait queue acquisition actual callers count inclusive_us max_us\n");
    printa(@lock_callers,@lock_caller_us,@lock_caller_max);
    printf("END host GL residency\n");
}
