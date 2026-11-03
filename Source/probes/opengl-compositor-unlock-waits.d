/* OWNED Mavericks guest only. Require live kern.uuid and copied /mach_kernel
 * UUID 8926E512-6714-34FC-B27C-6AE90EB51D5E before use. Exact disassembly puts
 * lck_mtx_unlock+168/16a/16d in its interlock PAUSE/load/retry loop. Capture
 * the actual lock argument and read-only owner/state metadata there; do not
 * infer the interlock holder from the mutex owner (they are NOT equivalent).
 * No lock writes, pixel reads, scheduler changes or assumed safe bypass.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=16m
BEGIN {began=timestamp;budget=1024;printf("READY compositor actual mutex-unlock waits target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{self->root=++roots;self->at=timestamp;root_by_tid[tid]=self->root;self->finishes=1;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->depth/ {self->finishes++;}
pid$target:IOKit:IOConnectTrap4:entry /self->depth/
{self->trap=2;unlock_depth[tid]=1;unlock_address[tid]=(uint64_t)0;@trap_entered=count();}
fbt:mach_kernel:lck_mtx_unlock:entry /pid==$target && self->trap==2/
{
    this->depth=unlock_depth[tid]+1;unlock_depth[tid]=this->depth;
    this->address=(uintptr_t)arg0;
    this->indirect=*(uint32_t *)(this->address+8)==0x07ff1007;
    this->actual=this->indirect?*(uintptr_t *)this->address:this->address;
    unlock_stack[tid,this->depth]=this->actual;unlock_start[tid,this->depth]=timestamp;
    unlock_address[tid]=this->actual;@unlock_entered=count();
}
profile-97 /pid==$target && unlock_address[tid] &&
    arg0>=(uintptr_t)&`lck_mtx_unlock+0x168 && arg0<(uintptr_t)&`lck_mtx_unlock+0x172/
{
    this->lock=unlock_address[tid];
    @spin[root_by_tid[tid],tid,this->lock,*(uintptr_t *)this->lock,
        *(uint32_t *)(this->lock+8),stack(14)]=count();
}
fbt:mach_kernel:lck_mtx_unlock:return /pid==$target && self->trap==2 && unlock_depth[tid]>1 &&
    timestamp-unlock_start[tid,unlock_depth[tid]]>=1000000 && budget>0/
{
    @slow[root_by_tid[tid],tid,unlock_stack[tid,unlock_depth[tid]],
        (timestamp-unlock_start[tid,unlock_depth[tid]])/1000,stack(14),ustack(18)]=count();budget--;
}
fbt:mach_kernel:lck_mtx_unlock:return /pid==$target && self->trap==2 && unlock_depth[tid]>1/
{
    this->depth=unlock_depth[tid];this->elapsed=(timestamp-unlock_start[tid,this->depth])/1000;
    @unlock_returned=count();@unlock_max=max(this->elapsed);
    unlock_stack[tid,this->depth]=0;unlock_start[tid,this->depth]=0;
    unlock_depth[tid]=this->depth-1;unlock_address[tid]=unlock_stack[tid,this->depth-1];
}
pid$target:IOKit:IOConnectTrap4:return /self->trap==2/
{
    @trap_returned=count();@remaining_depth[unlock_depth[tid]]=count();
    self->trap=1;unlock_address[tid]=(uint64_t)0;unlock_depth[tid]=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->finishes>1 && budget>0/
{@updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,self->finishes-1]=count();budget--;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID mutex metadata attribution: probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us finishes count\n");printa("%lu %u %lu %u %@d\n",@updates);
    printf("BALANCE trap enter/return, unlock enter/return/max_us, remaining_depth\n");
    printa(@trap_entered);printa(@trap_returned);printa(@unlock_entered);printa(@unlock_returned);printa(@unlock_max);printa(@remaining_depth);
    printf("SLOW unlock root tid actual_lock elapsed_us kernel/user callers\n");printa(@slow);
    printf("SPIN root tid actual_lock mutex_owner state kernel_stack (owner is NOT interlock holder)\n");printa(@spin);
    printf("END compositor actual mutex-unlock waits budget=%d roots=%u\n",budget,roots);
}
