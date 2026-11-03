/* Lower-perturbation follow-up: real compositor phases + OUR selector50 only.
 * No probes on every kernel mutex, fault or other IOKit method. Scheduler
 * blocking sites are captured only while the actual retained-writer call is
 * in flight. This locates waiting; it does not prove a lock may be removed.
 * Match the loaded driver/13F1911 CoreGraphics first. All buffered, finite,
 * metadata-only; no private lock fields or pixels are read or modified.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=16m
BEGIN {began=timestamp;budget=1024;printf("READY compositor retained-writer waits target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{
    self->active=2;self->root=++roots;self->root_at=timestamp;self->root_cpu=vtimestamp;
    self->mirror_depth=1;self->mirror_us=(uint64_t)0;self->claim_us=(uint64_t)0;self->finishes=1;
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry /self->active==2/
{self->mirror_depth++;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry /self->active==2 && self->mirror_depth==2/
{self->mirror_at=timestamp;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->active==2 && self->mirror_depth==2/
{self->mirror_us+=(timestamp-self->mirror_at)/1000;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->active==2 && self->mirror_depth>1/
{self->mirror_depth--;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->active==2/ {self->finishes++;}
pid$target:IOKit:IOConnectCallStructMethod:entry /arg1==50/
{
    self->claim=2;self->claim_at=timestamp;self->claim_cpu=vtimestamp;
    self->sleep_active=1;self->sleep_us=(uint64_t)0;@entered=count();
}
sched:::off-cpu /pid==$target && self->claim==2/
{
    self->sleep_active=2;self->sleep_at=timestamp;@sleep_entered=count();
    @sites[stack(18),ustack(16)]=count();
}
sched:::on-cpu /self->claim==2 && self->sleep_active==2/
{
    self->sleep_us+=(timestamp-self->sleep_at)/1000;self->sleep_active=1;
    @sleep_returned=count();@sleep_max=max((timestamp-self->sleep_at)/1000);
}
pid$target:IOKit:IOConnectCallStructMethod:return /self->claim==2/
{
    self->claim_us+=self->active==2?(timestamp-self->claim_at)/1000:0;
    @returned=count();@maximum=max((timestamp-self->claim_at)/1000);
}
pid$target:IOKit:IOConnectCallStructMethod:return
/self->claim==2 && timestamp-self->claim_at>=1000000 && budget>0/
{
    @slow[(self->claim_at-began)/1000,self->root,self->mirror_depth>1,
        (timestamp-self->claim_at)/1000,(vtimestamp-self->claim_cpu)/1000,self->sleep_us,(uint32_t)arg1]=count();budget--;
    @callers[ustack(18)]=count();
}
pid$target:IOKit:IOConnectCallStructMethod:return /self->claim==2/ {self->claim=1;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->active==2 && self->finishes>1 && budget>0/
{
    @updates[(self->root_at-began)/1000,self->root,(timestamp-self->root_at)/1000,
        (vtimestamp-self->root_cpu)/1000,self->mirror_us,self->claim_us,self->finishes-1]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1/ {self->active=1;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID writer attribution: probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us cpu_us mirror_us claim_us finishes count\n");
    printa("%lu %u %lu %lu %lu %lu %u %@d\n",@updates);
    printf("SLOW_CLAIM start_us root mirror wall_us cpu_us offcpu_us result count\n");
    printa("%lu %u %u %lu %lu %lu %x %@d\n",@slow);
    printf("BALANCE claim enter/return/max, scheduler off/on/max\n");
    printa(@entered);printa(@returned);printa(@maximum);
    printa(@sleep_entered);printa(@sleep_returned);printa(@sleep_max);
    printf("Actual slow claim callers\n");printa(@callers);
    printf("Actual claim blocking sites\n");printa(@sites);
    printf("END compositor retained-writer waits budget=%d roots=%u\n",budget,roots);
}
