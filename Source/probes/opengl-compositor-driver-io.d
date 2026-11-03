/* Whole rendered-update I/O mix, including OUR live VMsvga2Accel ABI.
 * Numeric selectors overlap unrelated framebuffer/service user clients:
 * qualify their actual caller before assigning an own-driver operation.
 * Outer IOKit wrappers are unioned, not double-counted with Struct->Method.
 * No kernel lock/fault/scheduler probes, payload reads, pixels or writes.
 * Wall includes native work/ownership/device scheduling, NOT just syscall
 * overhead. Instrumented timings are not clean FPS; require actual input.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=16m
BEGIN {began=timestamp;budget=256;printf("READY compositor driver I/O mix target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{self->active=2;self->root=++roots;self->at=timestamp;self->io_depth=1;self->io_us=(uint64_t)0;self->finishes=1;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->active==2/ {self->finishes++;}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->active==2/ {self->io_depth++;}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->active==2 && self->io_depth==2/
{self->io_at=timestamp;self->selector=(uint32_t)arg1;self->kind=probefunc;@entered=count();}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->active==2 && self->io_depth==2/
{
    this->us=(timestamp-self->io_at)/1000;self->io_us+=this->us;@returned=count();
    @calls[self->kind,self->selector]=count();@wall[self->kind,self->selector]=sum(this->us);
    @maximum[self->kind,self->selector]=max(this->us);
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->active==2 && self->io_depth==2 && timestamp-self->io_at>=1000000 && budget>0/
{@slow[self->root,self->kind,self->selector,(timestamp-self->io_at)/1000,ustack(16)]=count();budget--;}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->active==2 && self->io_depth>1/ {self->io_depth--;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->active==2 && self->finishes>1/
{@updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,self->io_us,self->finishes-1]=count();}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1/ {self->active=1;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID driver I/O mix: probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us io_union_us finishes count\n");printa("%lu %u %lu %lu %u %@d\n",@updates);
    printf("IO kind selector calls union_us max_us\n");printa("%s %u %@d %@d %@d\n",@calls,@wall,@maximum);
    printf("BALANCE outer I/O entered/returned\n");printa(@entered);printa(@returned);
    printf("SLOW outer I/O root kind selector us actual caller\n");printa(@slow);
    printf("END compositor driver I/O mix budget=%d roots=%u\n",budget,roots);
}
