/* Exact kernel 8926E512-6714-34FC-B27C-6AE90EB51D5E, owned accelerator
 * 25257282-998C-3D48-B62F-1EF4453604BE. Supply ACTUAL loaded text bounds as
 * $1/$2 (kextstat is UNSLID on this guest). Refuse any unmatched binary.
 * XNU thread_call.c + matched kernel disassembly establish static SDT args:
 * arg0=callback, arg5=call descriptor. FBT enter observes that same opaque
 * descriptor. No heap fields, pixel reads, kernel writes or guessed offsets.
 * Raw enqueue/return rows preserve already-pending/dispatch-before-return
 * races; do not treat a pre-existing/coalesced job as a new queue latency.
 * ubc_upl_commit has a proved real RET, not a tail-call 'return' probe.
 * All output is bounded/buffered. This is attribution, NOT clean pacing.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=16m
uint64_t queued_at[uintptr_t];
uint64_t ids[uintptr_t];
BEGIN {began=timestamp;budget=8192;printf("READY backing callout attribution bounds=%p..%p\n",$1,$2);}
fbt:mach_kernel:thread_call_enter:entry /caller>=$1 && caller<$2 && budget>0/
{
    self->enqueue=arg0;
    ids[arg0]=queued_at[arg0]?ids[arg0]:++serial;
    queued_at[arg0]=queued_at[arg0]?queued_at[arg0]:timestamp;
    self->enqueue_id=ids[arg0];
    @rows[(timestamp-began)/1000,ids[arg0],0,tid,caller-$1]=count();budget--;
}
fbt:mach_kernel:thread_call_enter:return /self->enqueue && budget>0/
{
    @rows[(timestamp-began)/1000,self->enqueue_id,1,tid,(uint64_t)arg1]=count();budget--;
    self->enqueue=0;self->enqueue_id=0;
}
sdt:mach_kernel::thread_callout-start /arg0>=$1 && arg0<$2 && budget>0/
{
    self->owned=1;self->at=timestamp;self->func=arg0-$1;
    self->job=ids[arg5]?ids[arg5]:++serial;self->upl_at=(uint64_t)0;
    self->lock_at=(uint64_t)0;self->lock_depth=0;callout_by_tid[tid]=self->job;
    @rows[(timestamp-began)/1000,self->job,10,tid,self->func]=count();
    @queues[self->job,queued_at[arg5]?1:0,queued_at[arg5]?(timestamp-queued_at[arg5])/1000:0]=count();
    @started[self->func]=count();queued_at[arg5]=0;ids[arg5]=0;budget--;
}
fbt:mach_kernel:ubc_upl_commit:entry /self->owned && budget>0/
{
    self->upl_at=timestamp;
    @rows[(timestamp-began)/1000,self->job,20,tid,0]=count();budget--;
}
fbt:mach_kernel:ubc_upl_commit:return /self->owned && self->upl_at && budget>0/
{
    @rows[(timestamp-began)/1000,self->job,21,tid,(timestamp-self->upl_at)/1000]=count();
    self->upl_at=0;budget--;
}
fbt:mach_kernel:IOLockSleep:entry /pid==$target && budget>0/
{
    event_seen[arg1]=1;@sleep_events[(timestamp-began)/1000,tid,(uintptr_t)arg1,stack(14)]=count();budget--;
}
fbt:mach_kernel:IOLockWakeup:entry /self->owned && event_seen[arg1] && budget>0/
{
    @wakes[(timestamp-began)/1000,self->job,tid,(uintptr_t)arg1]=count();budget--;
}
/* This period SDK defines IOLockLock as a macro to lck_mtx_lock; no
 * IOLockLock FBT probe exists. Observe the actual primitive, only while an
 * owned restore is executing, and union any nested acquisition brackets. */
fbt:mach_kernel:lck_mtx_lock:entry /self->owned/ {self->lock_depth++;}
fbt:mach_kernel:lck_mtx_lock:entry /self->owned && self->lock_depth==1/
{self->lock_at=timestamp;self->lock_caller=caller;}
fbt:mach_kernel:lck_mtx_lock:return /self->owned && self->lock_depth==1 && self->lock_at/
{
    @locks[self->job,self->lock_caller,(timestamp-self->lock_at)/1000]=count();self->lock_at=0;
}
fbt:mach_kernel:lck_mtx_lock:return /self->owned && self->lock_depth/ {self->lock_depth--;}
/* Retain the ACTUAL interrupted PC: a frameless stack can omit its callee.
 * A post-OUT PC includes host service/resumption, NOT pure guest CPU work. */
profile-199 /callout_by_tid[tid] && arg0/
{
    @sites[callout_by_tid[tid],sym(arg0)]=count();
    @cpu_stacks[callout_by_tid[tid],stack(18)]=count();
}
sched:::off-cpu /self->owned && budget>0/
{
    self->off_at=timestamp;@off_stacks[self->job,tid,stack(18)]=count();budget--;
}
sched:::on-cpu /self->owned && self->off_at && budget>0/
{
    @off[self->job,tid,(self->off_at-began)/1000,(timestamp-self->off_at)/1000]=count();self->off_at=0;budget--;
}
sdt:mach_kernel::thread_callout-end /self->owned && budget>0/
{
    @rows[(timestamp-began)/1000,self->job,11,tid,(timestamp-self->at)/1000]=count();
    @ended[self->func]=count();@unfinished_locks[self->lock_at!=0]=count();
    self->owned=0;self->at=0;self->func=0;self->job=0;self->off_at=0;self->lock_at=0;callout_by_tid[tid]=0;budget--;
}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
ERROR {printf("INVALID backing callout attribution probe error\n");exit(2);}
END {
    printf("ROWS us job event(0=enter,1=return-pending,10=start,11=end,20=UPL-enter,21=UPL-return) tid value(offset/duration/result) count\n");printa(@rows);
    printf("QUEUES job has-enter entry-to-start-us count (consult pending-return rows)\n");printa(@queues);
    printf("OFF_CPU job tid start_us duration_us count\n");printa(@off);
    printf("OFF_STACKS job tid stack\n");printa(@off_stacks);
    printf("SLEEP_EVENTS us tid event stack\n");printa(@sleep_events);
    printf("WAKES us job tid event count\n");printa(@wakes);
    printf("OWNED_LOCKS job actual caller / lock acquisition us count\n");printa(@locks);
    printf("RUNNING_PC job actual interrupted PC samples (includes host service at OUT)\n");printa(@sites);
    printf("RUNNING_STACKS job kernel stack\n");printa(@cpu_stacks);
    printf("Unfinished owned-lock brackets (must be zero)\n");printa(@unfinished_locks);
    printf("BALANCE function offset / start and end\n");printa(@started);printa(@ended);
    printf("END backing callout attribution budget=%d jobs=%u began_ns=%lu\n",budget,serial,began);
}
