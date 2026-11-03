/* Match a sampled stack to its ACTUAL long compositor root, not an idle
 * WindowServer sample. The sparse profile is statistical, not a GPU timer.
 * Only one real Trap4 boundary is timed inside Mirror; no all-mutex or
 * scheduler probes. Pair CPU stacks with ordered calls before changing code.
 * The same read-only survey runs on physical Intel; require positive actual
 * Intel driver hits before treating it as an accelerated reference.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=32m
BEGIN {began=timestamp;budget=1024;printf("READY compositor actual-root hotspots target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{
    self->active=2;self->root=++roots;self->at=timestamp;self->cpu=vtimestamp;
    self->mirror_depth=1;self->mirror_us=(uint64_t)0;self->trap_us=(uint64_t)0;self->finishes=1;
    active_by_tid[tid]=2;root_by_tid[tid]=self->root;mirror_by_tid[tid]=1;
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry /self->active==2/ {self->mirror_depth++;mirror_by_tid[tid]=self->mirror_depth;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry /self->active==2 && self->mirror_depth==2/ {self->mirror_at=timestamp;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->active==2 && self->mirror_depth==2/
{self->mirror_us+=(timestamp-self->mirror_at)/1000;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->active==2 && self->mirror_depth>1/ {self->mirror_depth--;mirror_by_tid[tid]=self->mirror_depth;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->active==2/ {self->finishes++;}
pid$target:IOKit:IOConnectTrap4:entry /self->active==2 && self->mirror_depth>1/
{self->trap=2;trap_by_tid[tid]=2;self->trap_at=timestamp;self->trap_cpu=vtimestamp;@trap_entered=count();}
pid$target:IOKit:IOConnectTrap4:return /self->trap==2/
{
    self->trap_us+=(timestamp-self->trap_at)/1000;@trap_returned=count();
    @trap_max=max((timestamp-self->trap_at)/1000);
}
pid$target:IOKit:IOConnectTrap4:return /self->trap==2 && timestamp-self->trap_at>=1000000 && budget>0/
{
    @slow[(self->trap_at-began)/1000,self->root,(timestamp-self->trap_at)/1000,
        (vtimestamp-self->trap_cpu)/1000]=count();budget--;
    @callers[self->root,ustack(20)]=count();
}
pid$target:IOKit:IOConnectTrap4:return /self->trap==2/ {self->trap=1;trap_by_tid[tid]=1;}
/* Profile interrupts need not expose pid-provider TLS as self->... on this
 * period kernel. Use explicit bounded thread identities and retain an ALL-WS
 * sample count so absent conditional samples cannot masquerade as zero cost. */
profile-97 /pid==$target/
{
    @all_samples[tid,active_by_tid[tid],mirror_by_tid[tid]]=count();
    @all_user[ustack(24)]=count();
}
profile-97 /pid==$target && active_by_tid[tid]==2 && mirror_by_tid[tid]>1/
{
    @execution[root_by_tid[tid],arg0?"kernel":"user",trap_by_tid[tid]==2]=count();
    @user[root_by_tid[tid],trap_by_tid[tid]==2,ustack(24)]=count();
}
profile-97 /pid==$target && active_by_tid[tid]==2 && mirror_by_tid[tid]>1 && arg0/
{@kernel[root_by_tid[tid],stack(18)]=count();}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->active==2 && self->finishes>1 && budget>0/
{
    @updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,
        (vtimestamp-self->cpu)/1000,self->mirror_us,self->trap_us,self->finishes-1]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1/ {self->active=1;active_by_tid[tid]=1;mirror_by_tid[tid]=1;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
pid$target:AppleIntelHD5000GraphicsGLDriver*:glrFlushContextToken:entry {@intel=count();}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID root hotspot attribution: probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us cpu_us mirror_us mirror_trap_us finishes count\n");
    printa("%lu %u %lu %lu %lu %lu %u %@d\n",@updates);
    printf("SLOW_TRAP start_us root wall_us cpu_us count\n");printa("%lu %u %lu %lu %@d\n",@slow);
    printf("BALANCE Mirror trap enter/return/max\n");printa(@trap_entered);printa(@trap_returned);printa(@trap_max);
    printf("ROOT_SAMPLES root execution inside_trap count\n");printa("%u %s %u %@d\n",@execution);
    printf("Actual slow trap callers by root\n");printa(@callers);
    printf("Actual Mirror user stacks by root\n");printa(@user);
    printf("Actual Mirror kernel stacks by root\n");printa(@kernel);
    printf("ALL target profile samples tid active mirror_depth\n");printa(@all_samples);
    printf("ALL target user stacks (not root-qualified)\n");printa(@all_user);
    printf("Actual physical Intel submit hits\n");printa(@intel);
    printf("END compositor actual-root hotspots budget=%d roots=%u\n",budget,roots);
}
