/* Attribute real IOKit ownership waits inside MagicMirror. StructMethod may
 * call Method: inclusive nested totals must not be summed. Selector integers
 * and bounded caller stacks only; no arguments/pixels/addresses are copied. */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY future backing dependencies target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry {self->mirror++;@mirrors=count();}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->mirror/ {self->mirror--;}
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:return
{@drawable_status[(uint32_t)arg1]=count();}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry
/self->mirror/
{
    self->depth[probefunc]++;self->at[probefunc,self->depth[probefunc]]=timestamp;
    self->selector[probefunc,self->depth[probefunc]]=(uint32_t)arg1;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return
/self->depth[probefunc] && arg1!=0/
{@errors[self->selector[probefunc,self->depth[probefunc]],(uint32_t)arg1]=count();}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return
/self->depth[probefunc] && timestamp-self->at[probefunc,self->depth[probefunc]]>=500000/
{@slow[self->selector[probefunc,self->depth[probefunc]],ustack(16)]=count();}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return
/self->depth[probefunc]/
{
    this->selector=self->selector[probefunc,self->depth[probefunc]];
    this->us=(timestamp-self->at[probefunc,self->depth[probefunc]])/1000;
    @calls[probefunc,this->selector]=count();@us[probefunc,this->selector]=sum(this->us);
    @maximum[probefunc,this->selector]=max(this->us);
    self->at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
fbt:mach_kernel:vm_fault:entry /pid==$target/
{self->fault_depth++;self->fault_at[self->fault_depth]=timestamp;self->fault_mirror[self->fault_depth]=self->mirror!=0;}
fbt:mach_kernel:vm_fault:return
/self->fault_depth && timestamp-self->fault_at[self->fault_depth]>=100000/
{@fault_stacks[self->fault_mirror[self->fault_depth],ustack(16)]=count();}
fbt:mach_kernel:vm_fault:return /self->fault_depth/
{
    @fault_calls[self->fault_mirror[self->fault_depth]]=count();
    @fault_us[self->fault_mirror[self->fault_depth]]=sum((timestamp-self->fault_at[self->fault_depth])/1000);
    @fault_max[self->fault_mirror[self->fault_depth]]=max((timestamp-self->fault_at[self->fault_depth])/1000);
    self->fault_at[self->fault_depth]=0;self->fault_depth--;
}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {
    printf("MagicMirror count\n");printa(@mirrors);
    printf("method selector calls inclusive_us max_us\n");printa("%s %d %@d %@d %@d\n",@calls,@us,@maximum);
    printf("IOKit calls >=500us, caller stacks\n");printa(@slow);
    printf("Nonzero IOKit replies selector status count\n");printa(@errors);
    printf("Actual drawable attach CGLError count (0 is success)\n");printa(@drawable_status);
    printf("Actual kernel VM faults inside-Mirror calls inclusive_us max_us\n");printa("%d %@d %@d %@d\n",@fault_calls,@fault_us,@fault_max);
    printf("Actual VM faults >=100us, user stacks\n");printa(@fault_stacks);
}
