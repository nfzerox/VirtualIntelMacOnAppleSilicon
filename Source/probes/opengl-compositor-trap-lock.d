/* OWNED x64 Mavericks guest only. Require kernel UUID8926E512-6714-34FC-
 * B27C-6AE90EB51D5E and accelerator AB5ECEF6-10C0-3603-9513-802DE98A286C.
 * Unlike all-unlock FBT instrumentation, observe the retained trap target's
 * lookup, then sample its actual lock without instrumenting kernel mutexes.
 * Exact binary: IOUserClient virtual slot958 calls getTargetAndTrapForIndex;
 * our qualified function's first16 bytes below, and direct mutex field100.
 * Read-only. Mutex owner is NOT necessarily the interlock holder. Peer stacks
 * locate concurrent work, not proof that a peer owns this particular lock.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=16m
BEGIN {began=timestamp;budget=512;printf("READY compositor retained trap-lock sampling target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{self->root=++roots;self->at=timestamp;root_by_tid[tid]=self->root;root_active[tid]=2;self->finishes=1;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->depth/ {self->finishes++;}
pid$target:IOKit:IOConnectTrap4:entry /self->depth && arg1==0/
{self->trap=2;self->trap_at=timestamp;direct_lock[tid]=(uint64_t)0;@trap_entered=count();}
/* The current_task wrapper tail-jumps: its FBT return precedes the lookup.
 * Capture the real callee RET, not its wrapper's unfinished RAX value. */
fbt:mach_kernel:iokit_lookup_connect_ref:return /pid==$target && self->trap==2 && arg1/
{
    this->object=(uintptr_t)arg1;this->vtable=*(uintptr_t *)this->object;
    this->method=this->vtable?*(uintptr_t *)(this->vtable+0x958):0;
    this->ours=this->method && *(uint64_t *)this->method==0x8548c031e5894855 &&
        *(uint64_t *)(this->method+8)==0x480a75d2850e74f6;
    direct_lock[tid]=this->ours?*(uintptr_t *)(this->object+0x100):0;
    @qualified_lookup[this->ours]=count();
    @objects[this->object,direct_lock[tid]]=count();
}
profile-97 /pid==$target && root_active[tid]==2 && arg0/
{
    @root_kernel[root_by_tid[tid],arg0,stack(16)]=count();
    @root_times[root_by_tid[tid],tid,cpu,(timestamp-began)/1000,arg0]=count();
}
profile-97 /pid==$target && direct_lock[tid] &&
    arg0>=(uintptr_t)&`lck_mtx_unlock+0x168 && arg0<(uintptr_t)&`lck_mtx_unlock+0x172/
{
    this->outer=direct_lock[tid];
    this->lock=*(uint32_t *)(this->outer+8)==0x07ff1007?*(uintptr_t *)this->outer:this->outer;
    @spin[root_by_tid[tid],tid,cpu,this->lock,*(uintptr_t *)this->lock,*(uint32_t *)(this->lock+8)]=count();
    @spin_stack[root_by_tid[tid],stack(16),ustack(20)]=count();
    @sample_times[root_by_tid[tid],tid,cpu,(timestamp-began)/1000,arg0]=count();
    spin_at=timestamp;spin_tid=tid;spin_root=root_by_tid[tid];
}
profile-97 /arg0 && spin_at && timestamp-spin_at<20000000 && tid!=spin_tid/
{@peers[spin_root,tid,pid,cpu,execname,stack(18)]=count();}
pid$target:IOKit:IOConnectTrap4:return /self->trap==2 && timestamp-self->trap_at>=1000000 && budget>0/
{@slow[(self->trap_at-began)/1000,self->root,(timestamp-self->trap_at)/1000,direct_lock[tid],ustack(18)]=count();budget--;}
pid$target:IOKit:IOConnectTrap4:return /self->trap==2/
{@trap_returned=count();self->trap=1;direct_lock[tid]=(uint64_t)0;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->finishes>1 && budget>0/
{@updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,self->finishes-1]=count();budget--;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1/ {root_active[tid]=1;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID retained trap-lock metadata: probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us finishes count\n");printa("%lu %u %lu %u %@d\n",@updates);
    printf("BALANCE trap enter/return, qualified lookup signature0=refused1=owned\n");printa(@trap_entered);printa(@trap_returned);printa(@qualified_lookup);
    printf("OBJECTS retained_object direct_mutex count (full-width addresses)\n");printa(@objects);
    printf("SLOW trap start_us root wall_us direct_mutex caller\n");printa(@slow);
    printf("SPIN root tid cpu actual_mutex mutex_owner state (owner is NOT interlock holder)\n");printa(@spin);printa(@spin_stack);
    printf("SPIN_TIMES root tid cpu timestamp_us kernel_pc count (check sample spacing)\n");printa(@sample_times);
    printf("ROOT_KERNEL actual root pc stack\n");printa(@root_kernel);
    printf("ROOT_TIMES root tid cpu timestamp_us kernel_pc count\n");printa(@root_times);
    printf("PEERS near actual spin root tid pid cpu execname kernel_stack; NOT proven lock ownership\n");printa(@peers);
    printf("END compositor retained trap-lock sampling budget=%d roots=%u\n",budget,roots);
}
