/* Finite, read-only attribution of selector-40 backing acquisition,
 * selector-49 initial writer gate and selector-51 writer commit (qualify
 * their actual caller for this driver).
 * IOLocks.cpp (xnu-2422) defines IOLockSleep(lock,event,interruptible) and
 * IOLockWakeup(lock,event,oneThread). Observe public arguments only: no guessed
 * surface/lock fields, no writes, timeouts, ownership bypass or all-lock trace.
 * Only events first seen sleeping inside this target's selected calls are kept.
 * Wakes are notifications, NOT proof of completed ownership transfer. The
 * last matching wake can be unrelated to a particular waiter; retain stacks,
 * event identities and timestamps instead of claiming exact causality.
 * Exact matched kernel disassembly: IOLockSleep TAIL-JUMPS to lck_mtx_sleep.
 * Its FBT 'return' fires BEFORE sleeping, so use the callee's actual RET.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=16m
BEGIN {began=timestamp;budget=1024;printf("READY compositor backing sleeps target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{
    self->root=++roots;self->root_at=timestamp;
    self->acquire_depth=0;self->scope_selector=0;self->acquire_at=(uint64_t)0;
    self->acquire_cpu=(uint64_t)0;self->off_at=(uint64_t)0;
    self->io_depth=0;self->sleep_at=(uint64_t)0;
}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry
/self->root/
{self->io_depth++;selector[tid,self->io_depth]=(uint32_t)arg1;}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry
/self->root && (arg1==40 || arg1==49 || arg1==51)/
{
    self->acquire_depth++;
    self->scope_selector=self->acquire_depth==1?(uint32_t)arg1:self->scope_selector;
    self->acquire_at=self->acquire_depth==1?timestamp:self->acquire_at;
    self->acquire_cpu=self->acquire_depth==1?vtimestamp:self->acquire_cpu;
    @acquire_entered[probefunc]=count();
}
sched:::off-cpu /pid==$target && self->acquire_depth/
{
    self->off_at=timestamp;
    @off_callers[self->root,tid,stack(20),ustack(18)]=count();
}
sched:::on-cpu /pid==$target && self->off_at/
{
    @off_intervals[self->root,tid,(self->off_at-began)/1000,
        (timestamp-self->off_at)/1000]=count();self->off_at=0;
}
fbt:mach_kernel:IOLockSleep:entry /pid==$target && self->acquire_depth && !self->sleep_at/
{
    self->sleep_at=timestamp;self->sleep_event=(uintptr_t)arg1;
    event_seen[self->sleep_event]=1;@sleep_entered=count();
    @sleep_callers[self->root,tid,self->sleep_event,stack(18),ustack(18)]=count();
}
fbt:mach_kernel:IOLockWakeup:entry /event_seen[(uintptr_t)arg1] && budget>0/
{
    this->event=(uintptr_t)arg1;last_wake[this->event]=timestamp;
    @wakes[(timestamp-began)/1000,this->event,pid,tid,stack(20)]=count();budget--;
}
fbt:mach_kernel:lck_mtx_sleep:return /pid==$target && self->sleep_at/
{
    this->event=self->sleep_event;
    this->wake=last_wake[this->event];
    @sleeps[(self->sleep_at-began)/1000,self->root,tid,this->event,
        (timestamp-self->sleep_at)/1000,
        this->wake>=self->sleep_at?(timestamp-this->wake)/1000:0,
        this->wake>=self->sleep_at?1:0,(int)arg1]=count();
    @sleep_returned=count();self->sleep_at=0;self->sleep_event=0;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return
/self->root && self->io_depth && (selector[tid,self->io_depth]==40 || selector[tid,self->io_depth]==49 || selector[tid,self->io_depth]==51)/
{
    @acquire_returned[probefunc]=count();
    @acquires[self->root,self->scope_selector,probefunc,(timestamp-self->acquire_at)/1000,
        (vtimestamp-self->acquire_cpu)/1000]=count();
    self->acquire_depth--;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return
/self->root && self->io_depth/
{selector[tid,self->io_depth]=0;self->io_depth--;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->root/
{
    @updates[self->root,(timestamp-self->root_at)/1000]=count();
    @unfinished[self->acquire_depth,self->sleep_at!=0]=count();self->root=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
ERROR {printf("INVALID backing-sleep attribution: probe error\n");exit(2);}
END {
    printf("ROOTS root wall_us count\n");printa(@updates);
    printf("ACQUISITIONS root selector kind wall_us thread_cpu_us count (wrappers nested, not additive)\n");printa(@acquires);
    printf("OFF_CPU root tid start_us duration_us count (sleep OR runnable delay)\n");printa(@off_intervals);
    printf("OFF_CPU_CALLERS root tid kernel/user stack\n");printa(@off_callers);
    printf("SLEEPS start_us root tid event wall_us last_matching_wake_to_return_us has_wake result count\n");
    printa(@sleeps);
    printf("SLEEP_CALLERS root tid event kernel/user stack\n");printa(@sleep_callers);
    printf("WAKES time_us event pid tid kernel stack\n");printa(@wakes);
    printf("BALANCE acquire entered/returned, sleep entered/returned, unfinished depths\n");
    printa(@acquire_entered);printa(@acquire_returned);
    printa(@sleep_entered);printa(@sleep_returned);printa(@unfinished);
    printf("END compositor backing sleeps budget=%d roots=%u\n",budget,roots);
}
