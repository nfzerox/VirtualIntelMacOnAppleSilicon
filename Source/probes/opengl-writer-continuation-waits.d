/* Guest-only attribution for OUR append-only selector 50 (ContinueWriterRAMLoan).
 * It is supposed to claim retained ownership, not wait for GPU completion.
 * The actual caller and loaded driver ABI must be checked before use. This
 * does not claim that time in the IOKit wrapper belongs to the driver body:
 * IPC scheduling and copyin/copyout also lie within that boundary.
 *
 * Time only outer kernel mutex and fault spans while that wrapper is active.
 * Use biased nonzero scalar metadata, avoiding the old DTrace dirty-variable
 * problem. Require balanced counts, no drops and actual workload coverage.
 * Kernel/VM wall times include emulated execution and are not clean FPS.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY retained-writer continuation waits target=%d selector=50\n",$target);}
pid$target:IOKit:IOConnectCallStructMethod:entry /arg1==50/
{
    self->method=2;self->at=timestamp;self->cpu=vtimestamp;
    self->mutex_us=(uint64_t)0;self->fault_us=(uint64_t)0;
    self->lock_depth=1;self->fault_depth=1;@method_entered=count();
}
fbt:mach_kernel:lck_mtx_lock:entry /pid==$target && self->method==2/
{self->lock_depth++;}
fbt:mach_kernel:lck_mtx_lock:entry /pid==$target && self->method==2 && self->lock_depth==2/
{self->lock_at=timestamp;@mutex_entered=count();}
fbt:mach_kernel:lck_mtx_lock:return /self->method==2 && self->lock_depth==2/
{
    self->mutex_us+=(timestamp-self->lock_at)/1000;@mutex_returned=count();
    @mutex_max=max((timestamp-self->lock_at)/1000);
}
fbt:mach_kernel:lck_mtx_lock:return
/self->method==2 && self->lock_depth==2 && timestamp-self->lock_at>=1000000/
{@slow_mutex[stack(16),ustack(12)]=count();}
fbt:mach_kernel:lck_mtx_lock:return /self->method==2 && self->lock_depth>1/
{self->lock_depth--;}
fbt:mach_kernel:vm_fault:entry /pid==$target && self->method==2/
{self->fault_depth++;}
fbt:mach_kernel:vm_fault:entry /pid==$target && self->method==2 && self->fault_depth==2/
{self->fault_at=timestamp;@fault_entered=count();}
fbt:mach_kernel:vm_fault:return /self->method==2 && self->fault_depth==2/
{
    self->fault_us+=(timestamp-self->fault_at)/1000;@fault_returned=count();
    @fault_max=max((timestamp-self->fault_at)/1000);
}
fbt:mach_kernel:vm_fault:return
/self->method==2 && self->fault_depth==2 && timestamp-self->fault_at>=1000000/
{@slow_fault[stack(16),ustack(12)]=count();}
fbt:mach_kernel:vm_fault:return /self->method==2 && self->fault_depth>1/
{self->fault_depth--;}
pid$target:IOKit:IOConnectCallStructMethod:return /self->method==2/
{
    @method_returned=count();@method_max=max((timestamp-self->at)/1000);
    @method_wall=sum((timestamp-self->at)/1000);
}
pid$target:IOKit:IOConnectCallStructMethod:return
/self->method==2 && timestamp-self->at>=1000000/
{
    printf("SLOW_CONTINUATION start_us=%lu wall_us=%lu virtual_cpu_us=%lu mutex_us=%lu fault_us=%lu result=%x\n",
        (unsigned long)((self->at-began)/1000),(unsigned long)((timestamp-self->at)/1000),
        (unsigned long)((vtimestamp-self->cpu)/1000),(unsigned long)self->mutex_us,
        (unsigned long)self->fault_us,(uint32_t)arg1);
}
pid$target:IOKit:IOConnectCallStructMethod:return /self->method==2/
{self->method=1;}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
ERROR {printf("INVALID continuation attribution: probe error\n");exit(2);}
END {
    printf("COUNTERS method entered/returned/wall/max, mutex entered/returned/max, fault entered/returned/max\n");
    printa(@method_entered);printa(@method_returned);printa(@method_wall);printa(@method_max);
    printa(@mutex_entered);printa(@mutex_returned);printa(@mutex_max);
    printa(@fault_entered);printa(@fault_returned);printa(@fault_max);
    printf("Mutex spans >=1ms\n");printa(@slow_mutex);
    printf("Fault spans >=1ms\n");printa(@slow_fault);
}
