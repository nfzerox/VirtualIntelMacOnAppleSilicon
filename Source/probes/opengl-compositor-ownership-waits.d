/* Finite whole-update ownership attribution on the real 13F1911 compositor.
 * I/O wrappers are UNIONED, not added twice. Biased nonzero depth markers
 * avoid Mavericks DTrace's dirty-TLS initialization ambiguity. Kernel mutex
 * and scheduler spans nest within I/O; none is additive to the update wall.
 * Selector50 identifies OUR retained-writer claim only with the matched VM
 * driver; do not assign that meaning to another vendor's method number.
 * No private object fields, pixels, memory writes or hot-path output.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=32m
BEGIN {began=timestamp;budget=2048;printf("READY compositor ownership waits target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{
    self->root=++roots;self->active=2;self->at=timestamp;self->cpu=vtimestamp;
    self->io_depth=1;self->mirror_depth=1;self->io_us=(uint64_t)0;
    self->io_count=(uint64_t)0;self->mirror_us=(uint64_t)0;
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry /self->active==2/
{self->mirror_depth++;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry /self->active==2 && self->mirror_depth==2/
{self->mirror_at=timestamp;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->active==2 && self->mirror_depth==2/
{self->mirror_us+=(timestamp-self->mirror_at)/1000;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->active==2 && self->mirror_depth>1/
{self->mirror_depth--;}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->active==2/
{
    self->io_depth++;
}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->active==2 && self->io_depth==2/
{
    self->io_at=timestamp;self->io_cpu=vtimestamp;self->selector=(uint32_t)arg1;
    self->kind=probefunc;self->inside_mirror=self->mirror_depth>1;
    self->mutex_depth=1;self->mutex_us=(uint64_t)0;self->sleep_us=(uint64_t)0;
    self->sleep_active=1;self->io_count++;@entered=count();
}
fbt:mach_kernel:lck_mtx_lock:entry /pid==$target && self->active==2 && self->io_depth>1/
{self->mutex_depth++;}
fbt:mach_kernel:lck_mtx_lock:entry /pid==$target && self->active==2 && self->io_depth>1 && self->mutex_depth==2/
{self->mutex_at=timestamp;@mutex_entered=count();}
fbt:mach_kernel:lck_mtx_lock:return /self->active==2 && self->io_depth>1 && self->mutex_depth==2/
{
    self->mutex_us+=(timestamp-self->mutex_at)/1000;@mutex_returned=count();
    @mutex_max=max((timestamp-self->mutex_at)/1000);
}
fbt:mach_kernel:lck_mtx_lock:return
/self->active==2 && self->io_depth>1 && self->mutex_depth==2 && timestamp-self->mutex_at>=1000000/
{@slow_mutex[self->selector,stack(18),ustack(14)]=count();}
fbt:mach_kernel:lck_mtx_lock:return /self->active==2 && self->io_depth>1 && self->mutex_depth>1/
{self->mutex_depth--;}
sched:::off-cpu /pid==$target && self->active==2 && self->io_depth>1/
{
    self->sleep_active=2;self->sleep_at=timestamp;@sleep_entered=count();
}
sched:::off-cpu /pid==$target && self->active==2 && self->io_depth>1 && self->selector==50/
{@claim_sleep_sites[stack(18),ustack(14)]=count();}
sched:::on-cpu /self->active==2 && self->io_depth>1 && self->sleep_active==2/
{
    self->sleep_us+=(timestamp-self->sleep_at)/1000;self->sleep_active=1;
    @sleep_returned=count();@sleep_max=max((timestamp-self->sleep_at)/1000);
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->active==2 && self->io_depth==2/
{
    self->io_us+=(timestamp-self->io_at)/1000;@returned=count();
    @calls[self->kind,self->selector,self->inside_mirror]=count();
    @wall[self->kind,self->selector,self->inside_mirror]=sum((timestamp-self->io_at)/1000);
    @maximum[self->kind,self->selector,self->inside_mirror]=max((timestamp-self->io_at)/1000);
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->active==2 && self->io_depth==2 && timestamp-self->io_at>=1000000 && budget>0/
{
    @slow_io[(self->io_at-began)/1000,self->root,self->kind,self->selector,self->inside_mirror,
        (timestamp-self->io_at)/1000,(vtimestamp-self->io_cpu)/1000,self->mutex_us,self->sleep_us]=count();budget--;
    @callers[self->kind,self->selector,self->inside_mirror,ustack(18)]=count();
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->active==2 && self->io_depth>1/
{self->io_depth--;}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->depth==1 && self->active==2 && timestamp-self->at>=500000 && budget>0/
{
    @updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,
        (vtimestamp-self->cpu)/1000,self->mirror_us,self->io_us,self->io_count]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1/ {self->active=1;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID ownership attribution: probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us cpu_us mirror_us io_union_us io_calls count\n");
    printa("%lu %u %lu %lu %lu %lu %lu %@d\n",@updates);
    printf("SLOW_IO start_us root kind selector mirror wall_us cpu_us mutex_us offcpu_us count\n");
    printa("%lu %u %s %u %u %lu %lu %lu %lu %@d\n",@slow_io);
    printf("I/O kind selector mirror calls union_us max_us\n");printa("%s %u %u %@d %@d %@d\n",@calls,@wall,@maximum);
    printf("BALANCE I/O enter/return, mutex enter/return/max, scheduler off/on/max\n");
    printa(@entered);printa(@returned);printa(@mutex_entered);printa(@mutex_returned);printa(@mutex_max);
    printa(@sleep_entered);printa(@sleep_returned);printa(@sleep_max);
    printf("Slow I/O actual callers\n");printa(@callers);
    printf("Slow mutex actual callers\n");printa(@slow_mutex);
    printf("Retained-writer claim blocking sites\n");printa(@claim_sleep_sites);
    printf("END compositor ownership waits budget=%d roots=%u\n",budget,roots);
}
