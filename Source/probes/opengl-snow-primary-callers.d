/* Finite owned-GLI publication survey. Drawable offset 6544 and surface +8
 * are from the matching Snow x64 GLI record-layout/disassembly, not Apple ABI.
 * No pixels, input, mutation or production logging. Read stacks only while
 * this opt-in probe is active. A flush is not assumed to be a final frame.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN {budget=80;printf("READY Snow primary callers\n");}
pid$target:GLEngine:VIMPresentDrawableKnown:entry /budget>0/
{
    this->d=*(uint64_t*)copyin(arg0+6544,8);
    self->primary=this->d && *(uint32_t*)copyin(this->d+8,4)==1;
}
pid$target:GLEngine:VIMPresentDrawableKnown:entry /budget>0 && self->primary/
{
    budget--;
    printf("%llu PRESENT tid=%d known=%d framebuffer=%d\n",(unsigned long long)timestamp,(int)tid,(int)arg1,(int)arg2);
    ustack(16);
}
tick-20s {exit(0);}
