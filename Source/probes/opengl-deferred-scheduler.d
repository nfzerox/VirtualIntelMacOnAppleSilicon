/* Read-only, bounded scheduler survey for CoreGraphics 13F1911 x86_64,
 * UUID ED5E3C37-7E2F-3885-8091-DFC9B44B38E8. Verify the loaded UUID before
 * using this probe on guest OR physical Intel. No application/system writes.
 *
 * Exact disassembly, not inferred floating-point return registers:
 * - begin_deferred_update_cycle RDX is state; +110/+118 are doubles and
 *   +120 the flags. Print double BITS; decode offline, not with D floats.
 * - mach_absolute_time has a real scalar return: RAX is raw mach ticks.
 *   Anchor those ticks against the SAME probe's timestamp;
 *   never assume DTrace's timestamp epoch equals mach_absolute_time.
 * - MPGetNextWakeupTime calls mach_absolute_time before reading the public
 *   framebuffer clock. Read its verified +158/+278 pointer chain there and
 *   print two snapshots with the actual now used by its deadline formula.
 *   These adjacent snapshots are NOT claimed to be the helper's eventual
 *   operands if the clock changes as it resumes. Reject unequal snapshots.
 * Mavericks' pid provider exposes neither instruction/return probes for
 * MPGetNextWakeupTime nor instruction probes for CGSCurrentRealTime. Never
 * mistake a scalar return register for a floating-point XMM0 result.
 * Times include instrumentation; do not report this run as displayed FPS.
 */
#pragma D option quiet
#pragma D option bufsize=8m
#pragma D option dynvarsize=4m
BEGIN {began=timestamp; budget=6000; printf("READY matched deferred scheduler\n");}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{self->update++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
/self->update==1/
{self->started=timestamp; self->state=(uint64_t)0; self->renders=0;}
pid$target:CoreGraphics:begin_deferred_update_cycle:entry
{self->begin=1; self->state=arg2;}
pid$target:CoreGraphics:begin_deferred_update_cycle:entry
/budget>0/
{
    this->s=(uint64_t *)copyin(arg2+0x110,24);
    printf("%lu BEGIN tid=%d state=%p force=%u deadline=%llx limit=%llx flags=%x\n",
        (unsigned long)timestamp,(int)tid,(void *)arg2,(uint32_t)arg1,
        this->s[0],this->s[1],(uint32_t)this->s[2]); budget--;
}
pid$target:CoreGraphics:MPGetNextWakeupTime:entry
{self->wake=(uint64_t)arg0; self->selector=(int32_t)arg1;}
pid$target:libsystem_kernel.dylib:mach_absolute_time:return
/(self->begin || self->update || self->wake) && budget>0/
{
    printf("%lu NOW tid=%d mach=%lu\n",(unsigned long)timestamp,(int)tid,
        (unsigned long)uregs[R_RAX]); budget--;
    self->mach=uregs[R_RAX];
}
pid$target:libsystem_kernel.dylib:mach_absolute_time:return
/self->wake && budget>0/
{
    this->description=*(uint64_t *)copyin(self->wake+0x158,8);
    this->shared=*(uint64_t *)copyin(this->description+0x278,8);
    this->clock=(uint64_t *)copyin(this->shared+0x40,16);
    this->again=(uint64_t *)copyin(this->shared+0x40,16);
    printf("%lu WAKE tid=%d selector=%d now=%lu vbl=%lu delta=%lu stable=%d\n",
        (unsigned long)timestamp,(int)tid,self->selector,
        (unsigned long)uregs[R_RAX],(unsigned long)this->clock[0],
        (unsigned long)this->clock[1],
        this->clock[0]==this->again[0] && this->clock[1]==this->again[1]); budget--;
}
pid$target:libsystem_kernel.dylib:mach_absolute_time:return
{self->wake=0;}
pid$target:CoreGraphics:begin_deferred_update_cycle:return
/self->begin && self->state && budget>0/
{
    this->s=(uint64_t *)copyin(self->state+0x110,24);
    printf("%lu DEFER tid=%d result=%u deadline=%llx limit=%llx flags=%x\n",
        (unsigned long)timestamp,(int)tid,(uint32_t)(arg1&255),
        this->s[0],this->s[1],(uint32_t)this->s[2]); budget--;
}
pid$target:CoreGraphics:begin_deferred_update_cycle:return
{self->begin=0;}
pid$target:CoreGraphics:update_display_callback:entry
/budget>0/
{printf("%lu CALLBACK tid=%d\n",(unsigned long)timestamp,(int)tid); budget--;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry
/self->update/
{self->renders++;}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->update==1 && self->state && budget>0/
{
    this->s=(uint64_t *)copyin(self->state+0x110,24);
    printf("%lu UPDATE tid=%d us=%lu renders=%d deadline=%llx limit=%llx flags=%x\n",
        (unsigned long)timestamp,(int)tid,(unsigned long)((timestamp-self->started)/1000),
        self->renders,this->s[0],this->s[1],(uint32_t)this->s[2]); budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->update/
{self->update--;}
/* A global tick-12s may fire immediately after attach. Measure a complete
 * BEGIN-relative interval instead; an empty trace is not scheduler evidence. */
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {printf("Remaining metadata budget=%d\n",budget);}
