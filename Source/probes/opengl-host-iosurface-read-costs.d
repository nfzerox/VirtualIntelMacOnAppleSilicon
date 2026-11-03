/* Host Intel 26.6.2 IOSurfaceClientLock has a genuine ret at +131, verified
 * in the loaded QEMU image before tracing. IOSurfaceLock tail-jumps into it:
 * do NOT time the public wrapper's premature pid return probe. Options and
 * durations only, plus bounded slow-call stacks. No pixels or pointers logged,
 * no lock flags changed, no SIP/host-driver change. Profiling is not FPS.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY host IOSurface lock cost survey target=%d\n",$target);}
pid$target:IOSurface:IOSurfaceClientLock:entry
{self->lock_begin=timestamp;self->options=(uint32_t)arg1;}
pid$target:IOSurface:IOSurfaceClientLock:return
/self->lock_begin/
{
    this->us=(timestamp-self->lock_begin)/1000;
    @calls[self->options,(uint32_t)arg1]=count();
    @sum[self->options,(uint32_t)arg1]=sum(this->us);
    @max[self->options,(uint32_t)arg1]=max(this->us);
    @hist[self->options]=quantize(this->us);
}
pid$target:IOSurface:IOSurfaceClientLock:return
/self->lock_begin && timestamp-self->lock_begin>=1000000/
{@slow[self->options,ustack(10)]=count();}
pid$target:IOSurface:IOSurfaceClientLock:return
/self->lock_begin/ {self->lock_begin=0;}
tick-30s {exit(0);}
END {
    printf("options native_status calls inclusive_us max_us\n");
    printa("%u %u %@d %@d %@d\n",@calls,@sum,@max);
    printa(@hist);printf("slow lock callers\n");printa(@slow);
}
