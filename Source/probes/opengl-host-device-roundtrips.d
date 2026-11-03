/* Owned QEMU functions only. No copyin, host ptrace, Apple layouts or security
 * changes. These nested spans are NOT additive. BQL wait precedes hvf_handle_io
 * and is NOT measured here. The actual bql_lock_impl has only tail returns;
 * do not pretend a missing return probe means zero wait. Inspect slow callers.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;budget=128;printf("READY host device roundtrips target=%d\n",$target);}
pid$target:qemu-system-x86_64:hvf_handle_io:entry,
pid$target:qemu-system-x86_64:vmsvga_passthrough_opengl:entry,
pid$target:qemu-system-x86_64:vmsvga_passthrough_opengl_notify_pop:entry,
pid$target:qemu-system-x86_64:vmsvga_passthrough_opengl_notify_subscribe:entry
{
    self->depth[probefunc]++;self->at[probefunc,self->depth[probefunc]]=timestamp;
    self->cpu[probefunc,self->depth[probefunc]]=vtimestamp;
    @entered[probefunc]=count();
}
pid$target:qemu-system-x86_64:hvf_handle_io:return,
pid$target:qemu-system-x86_64:vmsvga_passthrough_opengl:return,
pid$target:qemu-system-x86_64:vmsvga_passthrough_opengl_notify_pop:return,
pid$target:qemu-system-x86_64:vmsvga_passthrough_opengl_notify_subscribe:return
/self->depth[probefunc]/
{
    this->us=(timestamp-self->at[probefunc,self->depth[probefunc]])/1000;
    @returned[probefunc]=count();@wall[probefunc]=sum(this->us);
    @cpu[probefunc]=sum((vtimestamp-self->cpu[probefunc,self->depth[probefunc]])/1000);
    @maximum[probefunc]=max(this->us);@latency[probefunc]=quantize(this->us);
    if(this->us>=20000 && budget>0){
        @slow[probefunc,(self->at[probefunc,self->depth[probefunc]]-began)/1000,
            this->us,(vtimestamp-self->cpu[probefunc,self->depth[probefunc]])/1000,ustack(18)]=count();budget--;
    }
    self->at[probefunc,self->depth[probefunc]]=0;self->cpu[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
ERROR {printf("INVALID host device roundtrips\n");exit(2);}
END {
    printf("SPAN function entered returned inclusive_wall_us thread_cpu_us max_us (NESTED)\n");
    printa("%s %@d %@d %@d %@d %@d\n",@entered,@returned,@wall,@cpu,@maximum);
    printa(@latency);printf("SLOW function start_us wall_us cpu_us actual caller\n");printa(@slow);
    printf("END host device roundtrips budget=%d\n",budget);
}
