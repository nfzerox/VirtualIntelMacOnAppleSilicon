/* Guest-only bounded lock-wait attribution. Run with -p WindowServer-PID.
 * Native GL spans do not cover guest kernel locks around the device doorbell.
 * Require balanced entry/return counts AND no dynamic-variable drops.
 * Instrumented kernel wall spans are
 * not uncontended-lock costs, clean frame pacing, or proof of a removable lock.
 * No lock/object contents, pixels, host ptrace or policy changes are involved.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {
    self->depth=1;self->at=(uint64_t)1;self->cpu=(uint64_t)1;self->mutex=(uint64_t)1;
    began=timestamp;printf("READY guest kernel mutex attribution target=%d\n",$target);
}
fbt:mach_kernel:lck_mtx_lock:entry
/pid==$target/
{
    /* Keep scalar metadata allocated: repeatedly zeroing indexed dynamic
     * variables on this very hot kernel boundary exhausts old DTrace's
     * dirty-list reclamation and fabricates unmatched spans. Depth is biased
     * by one so the quiescent value is nonzero. Only outer calls are timed. */
    self->depth=self->depth?self->depth+1:2;
}
fbt:mach_kernel:lck_mtx_lock:entry
/pid==$target && self->depth==2/
{
    self->at=timestamp;self->cpu=vtimestamp;self->mutex=arg0;
    @entered[arg0]=count();
}
fbt:mach_kernel:lck_mtx_lock:return
/self->depth==2/
{
    this->us=(timestamp-self->at)/1000;
    @returned[self->mutex]=count();@wall[self->mutex]=sum(this->us);
    @cpu[self->mutex]=sum((vtimestamp-self->cpu)/1000);
    @maximum[self->mutex]=max(this->us);
}
fbt:mach_kernel:lck_mtx_lock:return
/self->depth==2 && timestamp-self->at>=1000000/
{
    @slow[self->mutex,stack(12),ustack(12)]=count();
}
fbt:mach_kernel:lck_mtx_lock:return
/self->depth>1/
{
    self->depth--;
}
tick-1s /timestamp-began>=14000000000/ {exit(0);}
ERROR {printf("INVALID mutex attribution: probe error\n");exit(2);}
END {
    printf("MUTEX address entered returned inclusive_wall_us thread_cpu_us maximum_us\n");
    printa("%p %@d %@d %@d %@d %@d\n",@entered,@returned,@wall,@cpu,@maximum);
    printf("Mutex spans >=1ms by actual kernel/user caller (not additive)\n");
    printa(@slow);
}
