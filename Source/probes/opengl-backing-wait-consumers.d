/* Finite OWN GLI attribution: which frontend asks for CPU ownership after a
 * pending GPU publication? IDs are caller CGS handles, NOT backing identity
 * or permission to cache/reuse a resource. No pixels or private Apple layout.
 * All probes must match. Use the UUID-guarded future control separately;
 * this script cannot enable an experiment or change a lock/CPU permission.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY actual backing wait consumers target=%d\n",$target);}
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:entry
{
    self->attach_depth++;
    self->attach_id[self->attach_depth]=*(uint32_t *)copyin(arg1+8,4);
    self->waited_handle=0;
    @attaches[self->attach_id[self->attach_depth]]=count();
}
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:return /self->attach_depth/
{self->attach_id[self->attach_depth]=0;self->attach_depth--;}
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:entry
{
    self->writer_depth++;self->writer_id[self->writer_depth]=(uint32_t)arg0;
    self->writer_after_wait[self->writer_depth]=self->waited_handle==(uint32_t)arg0 && self->waited_handle!=0;
    self->waited_handle=0;@writers[(uint32_t)arg0]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:return
/self->writer_depth && self->writer_after_wait[self->writer_depth]/
{@cpu_wait_then_writer[self->writer_id[self->writer_depth],(int32_t)arg1]=count();}
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:return /self->writer_depth/
{self->writer_after_wait[self->writer_depth]=0;self->writer_id[self->writer_depth]=0;self->writer_depth--;}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /arg2!=0 && self->writer_depth/
{
    this->header=(uint32_t *)copyin(arg1,8);
    @writer_contexts[self->writer_id[self->writer_depth],this->header[0],this->header[1]]=count();
}
pid$target:IOKit:IOConnectCallStructMethod:entry /arg1==40/
{
    self->wait_at=timestamp;
    self->wait_attach=self->attach_depth?self->attach_id[self->attach_depth]:0;
    self->wait_writer=self->writer_depth?self->writer_id[self->writer_depth]:0;
    @wait_stacks[ustack(16)]=count();
}
pid$target:IOKit:IOConnectCallStructMethod:return /self->wait_at/
{
    this->us=(timestamp-self->wait_at)/1000;
    @waits[self->wait_attach,self->wait_writer,(uint32_t)arg1]=count();
    @wait_us[self->wait_attach,self->wait_writer,(uint32_t)arg1]=sum(this->us);
    @wait_max[self->wait_attach,self->wait_writer,(uint32_t)arg1]=max(this->us);
    @wait_distribution=quantize(this->us);
    self->waited_handle=arg1==0?self->wait_attach:0;
    self->wait_at=0;self->wait_attach=0;self->wait_writer=0;
}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {
    printf("Actual attach CGS_handle calls (not resource identity)\n");printa("%u %@d\n",@attaches);
    printf("Actual GPU writer Begin CGS_handle calls\n");printa("%u %@d\n",@writers);
    printf("GPU writer synchronous wire CGS_handle opcode context_id calls (IDs are stream-local)\n");
    printa("%u %x %u %@d\n",@writer_contexts);
    printf("CPU waits attach_handle writer_Begin_handle status calls inclusive_us max_us\n");
    printa("%u %u %x %@d %@d %@d\n",@waits,@wait_us,@wait_max);
    printf("Same-attach completed CPU wait then GPU writer Begin handle result calls (1 is scoped success)\n");
    printa("%u %d %@d\n",@cpu_wait_then_writer);
    printf("CPU wait duration_us distribution\n");printa(@wait_distribution);
    printf("CPU wait consumer stacks\n");printa(@wait_stacks);
}
