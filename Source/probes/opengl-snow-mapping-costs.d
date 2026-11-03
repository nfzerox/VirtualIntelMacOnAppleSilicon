/* Finite Snow WindowServer mapping attribution. These are actual Darwin-10
 * IOMemoryDescriptor/IOMemoryMap symbols, not guessed kext offsets. Restrict
 * to the supplied WindowServer PID; mappings are not read or changed. Costs
 * include nested VM work and must not be added to enclosing rendering time.
 * Run the real Finder fixture after READY. No per-operation output or stacks.
 */
#pragma D option quiet
#pragma D option aggsize=2m
#pragma D option dynvarsize=2m
BEGIN {printf("READY Snow mapping attribution\n");}
fbt:mach_kernel:_ZN18IOMemoryDescriptor19createMappingInTaskEP4taskyjyy:entry,
fbt:mach_kernel:_ZN11IOMemoryMap5unmapEv:entry
/pid==$target/
{self->at[probefunc]=timestamp;}
fbt:mach_kernel:_ZN18IOMemoryDescriptor19createMappingInTaskEP4taskyjyy:return,
fbt:mach_kernel:_ZN11IOMemoryMap5unmapEv:return
/self->at[probefunc]/
{
    this->us=(timestamp-self->at[probefunc])/1000;
    @calls[probefunc]=count();@wall[probefunc]=sum(this->us);
    @maximum[probefunc]=max(this->us);@distribution[probefunc]=quantize(this->us);
    self->at[probefunc]=0;
}
tick-12s {exit(0);}
ERROR {printf("INVALID: mapping probe error\n");exit(2);}
END {
    printa("MAP %s calls=%@d wall_us=%@d max_us=%@d\n",@calls,@wall,@maximum);
    printa(@distribution);
}
