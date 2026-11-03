/* Classify WHOLE-update tails, not median FPS. Union nested IOKit wrappers
 * including the real trap. Actual vm_fault elapsed time distinguishes raw
 * guest-memory stalls from time spent inside the host rendering exchange.
 * Metadata only; no pixels or Apple object layouts. Probe overhead is real:
 * run separately from uninstrumented pacing. Require READY + actual input. */
#pragma D option quiet
#pragma D option dynvarsize=64m
#pragma D option aggsize=8m
BEGIN {
    self->root=0;self->root_at=(uint64_t)0;self->root_cpu=(uint64_t)0;
    self->io_at=(uint64_t)0;self->io_depth=0;self->io_kind="";self->io_selector=0;
    self->io_us=(uint64_t)0;self->fault_us=(uint64_t)0;self->fault_depth=0;
    began=timestamp;printf("READY whole-compositor long stalls target=%d\n",$target);
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{
    self->root++;
    self->root_at=self->root==1?timestamp:self->root_at;
    self->root_cpu=self->root==1?vtimestamp:self->root_cpu;
    self->io_us=self->root==1?0:self->io_us;
    self->fault_us=self->root==1?0:self->fault_us;
}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->root/
{
    self->io_at=self->io_depth?self->io_at:timestamp;
    self->io_kind=self->io_depth?self->io_kind:probefunc;
    self->io_selector=self->io_depth?self->io_selector:(uint32_t)arg1;
    self->io_depth++;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->io_depth==1/
{
    this->us=(timestamp-self->io_at)/1000;self->io_us+=this->us;
    @io_calls[self->io_kind,self->io_selector]=count();
    @io_total[self->io_kind,self->io_selector]=sum(this->us);
    @io_max[self->io_kind,self->io_selector]=max(this->us);
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->io_depth==1 && timestamp-self->io_at>=20000000/
{@slow_io[self->io_kind,self->io_selector,ustack(16)]=count();}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->io_depth/ {
    self->io_depth--;
    self->io_at=self->io_depth?self->io_at:0;
    self->io_kind=self->io_depth?self->io_kind:"";
    self->io_selector=self->io_depth?self->io_selector:0;
}
fbt:mach_kernel:vm_fault:entry /pid==$target/
{
    self->fault_depth++;self->fault_at[self->fault_depth]=timestamp;
    self->fault_root[self->fault_depth]=self->root!=0;
}
fbt:mach_kernel:vm_fault:return /self->fault_depth/
{
    this->us=(timestamp-self->fault_at[self->fault_depth])/1000;
    this->root=self->fault_root[self->fault_depth];
    @fault_calls[this->root]=count();@fault_total[this->root]=sum(this->us);@fault_max[this->root]=max(this->us);
    self->fault_us+=this->root?this->us:0;
}
fbt:mach_kernel:vm_fault:return /self->fault_depth && timestamp-self->fault_at[self->fault_depth]>=1000000/
{@slow_fault[self->fault_root[self->fault_depth],ustack(16)]=count();}
fbt:mach_kernel:vm_fault:return /self->fault_depth/ {
    self->fault_at[self->fault_depth]=0;self->fault_root[self->fault_depth]=0;self->fault_depth--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->root==1 && timestamp-self->root_at>=50000000/
{printf("LONG_UPDATE start_us=%d wall_us=%d virtual_cpu_us=%d io_union_us=%d vm_fault_us=%d\n",
    (self->root_at-began)/1000,(timestamp-self->root_at)/1000,(vtimestamp-self->root_cpu)/1000,self->io_us,self->fault_us);}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->root/ {self->root--;@roots=count();}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
END {
    printf("ROOT COUNT not frames\n");printa(@roots);
    printf("OUTERMOST I/O method selector count inclusive_us maximum_us\n");
    printa("%s %u %@d %@d %@d\n",@io_calls,@io_total,@io_max);
    printf("ACTUAL VM faults inside_root count inclusive_us maximum_us\n");
    printa("%d %@d %@d %@d\n",@fault_calls,@fault_total,@fault_max);
    printf("I/O >=20ms callers\n");printa(@slow_io);
    printf("VM faults >=1ms callers\n");printa(@slow_fault);
}
