/* Low-rate selected owned-driver call + scheduler attribution. Unlike the
 * broader backing-sleep survey, no IOLockSleep/Wakeup/all-mutex or fault probe
 * is enabled. Kernel instrumentation can perturb the rare tail: a negative
 * reproduction is not a fix. Selectors49/51 require this exact VMsvga2 ABI and
 * actual user stacks; numbers alone do not identify another driver's calls.
 * Off-CPU can mean blocking OR runnable/preemption delay; keep kernel stacks.
 * No private fields, memory writes, pixels, priority or security changes.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=16m
/* Exact kernel8926E512: dtrace_gethrtime+25 is SUBQ disp32(%rip),R14;
 * +28 holds its signed displacement, +2c is next PC. It reads the static
 * first-use Mach epoch, NOT a boot epoch. No fixed slid address or text write.
 * This Intel kernel's public mach_timebase_info is1/1. The coordinator checks
 * the kernel UUID before enabling this exact-layout read. */
BEGIN {
    clock_function=(uintptr_t)&`dtrace_gethrtime;
    clock_qualified=*(uint8_t*)(clock_function+0x25)==0x4c &&
        *(uint8_t*)(clock_function+0x26)==0x2b && *(uint8_t*)(clock_function+0x27)==0x35;
}
BEGIN /!clock_qualified/ {printf("INVALID DTrace clock layout\n");exit(2);}
BEGIN /clock_qualified/ {
    this->epoch_address=clock_function+0x2c+*(int32_t*)(clock_function+0x28);
    epoch=*(uint64_t*)this->epoch_address;
    began=timestamp;budget=2048;
    printf("READY selected writer scheduling target=%d began_seconds=%u began_nanoseconds=%u epoch_seconds=%u epoch_nanoseconds=%u\n",
        $target,(uint32_t)(began/1000000000),(uint32_t)(began%1000000000),
        (uint32_t)(epoch/1000000000),(uint32_t)(epoch%1000000000));
}
pid$target:IOKit:IOConnectCallStructMethod:entry
/(arg1==49 || arg1==51) && budget>0/
{
    self->active=2;self->id=++serial;self->selector=(uint32_t)arg1;
    self->at=timestamp;self->cpu=vtimestamp;self->off_at=(uint64_t)0;
    self->off_us=(uint64_t)0;self->off_count=0;budget--;
}
sched:::off-cpu /pid==$target && self->active==2/
{
    self->off_at=timestamp;self->off_count++;
    @sites[self->id,self->selector,stack(24),ustack(16)]=count();
}
sched:::on-cpu /self->active==2 && self->off_at/
{
    @intervals[self->id,(self->off_at-began)/1000,(timestamp-self->off_at)/1000]=count();
    self->off_us+=(timestamp-self->off_at)/1000;self->off_at=0;
}
/* Sparse interrupted PCs are useful only for calls whose real returned span
 * is long. Keep call IDs and actual kernel stacks; no guessed Apple fields. */
profile-197 /pid==$target && self->active==2 && arg0/
{@kernel_sites[self->id,arg0,stack(22),ustack(16)]=count();}
pid$target:IOKit:IOConnectCallStructMethod:return /self->active==2/
{
    @calls[self->id,(self->at-began)/1000,self->selector,(timestamp-self->at)/1000,
        (vtimestamp-self->cpu)/1000,self->off_us,self->off_count]=count();
    self->active=1;
}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
ERROR {printf("INVALID selected writer scheduling\n");exit(2);}
END {
    printf("CALL id start_us selector wall_us thread_cpu_us offcpu_us offcpu_count records\n");
    printa("%u %lu %u %lu %lu %lu %u %@d\n",@calls);
    printf("OFFCPU id start_us duration_us records\n");printa("%u %lu %lu %@d\n",@intervals);
    printf("SITES id selector kernel/user stack\n");printa(@sites);
    printf("KERNEL_SAMPLES id interrupted_pc kernel/user stack (not event durations)\n");printa(@kernel_sites);
    printf("END selected writer scheduling budget=%d calls=%u\n",budget,serial);
}
