/* OWNED x64 guest, exact kernel8926E512-6714-34FC-B27C-6AE90EB51D5E.
 * Use the kernel's actual CTF cpu_data_t/x86_saved_state_t to read the SAME
 * interrupt frame that profile_fire/find_kern_regs uses. Validate saved RIP
 * against profile arg0 before interpreting registers. Exact disassembly of
 * lck_mtx_unlock+168..171 proves RDX is the ACTUAL inner lock in that loop.
 * No FBT on mutexes/lookup, no inferred client member offset, no memory write,
 * inferior call or completion bypass. Mutex owner is not interlock holder.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=16m
BEGIN {began=timestamp;budget=1024;printf("READY compositor exact interrupted-kernel registers target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{self->root=++roots;self->at=timestamp;root_by_tid[tid]=self->root;active[tid]=2;self->finishes=1;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->depth/ {self->finishes++;}
profile-97 /pid==$target && active[tid]==2 && arg0/
{
    this->ss=(x86_saved_state_t *)`cpu_data_ptr[cpu]->cpu_int_state;
    this->valid=this->ss && this->ss->uss.ss_64.isf.rip==arg0;
    @validated[this->valid]=count();
    @registers[root_by_tid[tid],tid,cpu,(timestamp-began)/1000,arg0,
        this->valid?this->ss->uss.ss_64.rdx:0,this->valid?this->ss->uss.ss_64.rax:0,
        this->valid?this->ss->uss.ss_64.rdi:0,this->valid?this->ss->uss.ss_64.rsi:0]=count();
    @kernel[root_by_tid[tid],arg0,stack(18),ustack(22)]=count();
}
profile-97 /pid==$target && active[tid]==2 &&
    arg0>=(uintptr_t)&`lck_mtx_unlock+0x168 && arg0<(uintptr_t)&`lck_mtx_unlock+0x172 &&
    ((x86_saved_state_t *)`cpu_data_ptr[cpu]->cpu_int_state)->uss.ss_64.isf.rip==arg0/
{
    this->ss=(x86_saved_state_t *)`cpu_data_ptr[cpu]->cpu_int_state;
    this->lock=this->ss->uss.ss_64.rdx;
    @locks[root_by_tid[tid],tid,cpu,(timestamp-began)/1000,this->lock,
        *(uintptr_t *)this->lock,*(uint32_t *)(this->lock+8),(uintptr_t)curthread]=count();
    spin_at=timestamp;spin_tid=tid;spin_root=root_by_tid[tid];
}
profile-97 /arg0 && spin_at && timestamp-spin_at<20000000 && tid!=spin_tid/
{
    this->ss=(x86_saved_state_t *)`cpu_data_ptr[cpu]->cpu_int_state;
    this->valid=this->ss && this->ss->uss.ss_64.isf.rip==arg0;
    @peers[spin_root,tid,pid,cpu,execname,arg0,
        this->valid?this->ss->uss.ss_64.rdx:0,this->valid?this->ss->uss.ss_64.rbx:0,
        this->valid?this->ss->uss.ss_64.rdi:0,stack(18)]=count();
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->finishes>1 && budget>0/
{@updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,self->finishes-1]=count();budget--;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1/ {active[tid]=1;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID exact interrupted-register attribution: probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us finishes count\n");printa("%lu %u %lu %u %@d\n",@updates);
    printf("VALIDATION interrupt RIP agrees with profile arg0 (0=refused1=matched)\n");printa(@validated);
    printf("REGISTERS root tid cpu timestamp_us pc RDX RAX RDI RSI count; raw metadata, no payload\n");printa(@registers);
    printf("LOCKS actual root tid cpu timestamp_us mutex owner state current_thread count; owner is NOT interlock holder\n");printa(@locks);
    printf("ACTUAL kernel/user stacks by root and PC\n");printa(@kernel);
    printf("PEERS near spin root tid pid cpu name pc RDX RBX RDI stack; NOT proven lock ownership\n");printa(@peers);
    printf("END compositor exact interrupted-kernel registers budget=%d roots=%u\n",budget,roots);
}
