/* Finite owned-code survey for the Snow x64 build. These offsets are from
 * clang -fdump-record-layouts of our matching 764-entry GLI, not Apple data.
 * No pixels, user data, changed policy or unbounded per-frame logging.
 */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN {budget=96;printf("READY Snow primary publication\n");}
pid$target:GLEngine:VIMPresentDrawableKnown:entry /budget>0/
{
    this->d=*(uint64_t*)copyin(arg0+6544,8);
    self->d=this->d;
    self->primary=this->d!=0;
}
pid$target:GLEngine:VIMPresentDrawableKnown:entry /self->primary/
{
    budget--;self->began=timestamp;
    printf("BEGIN tid=%d drawable=%x surface=%d framebuffer=%x size=%d,%d origin=%d,%d region=%d flipped=%d\n",tid,self->d,
        *(int*)copyin(self->d+8,4),*(int*)copyin(self->d+56,4),
        *(int*)copyin(self->d+20,4),*(int*)copyin(self->d+24,4),
        *(int*)copyin(self->d+124,4),*(int*)copyin(self->d+128,4),
        *(int*)copyin(self->d+120,4),!*(int*)copyin(self->d+108,4));
}
pid$target:GLEngine:VIMUnlockSurfaceMode:entry /self->primary/
{printf("UNLOCK tid=%d readonly=%d read=%d,%d %dx%d\n",tid,arg1,
    *(int*)copyin(self->d+136,4),*(int*)copyin(self->d+140,4),
    *(int*)copyin(self->d+144,4),*(int*)copyin(self->d+148,4));}
pid$target:GLEngine:VIMPresentDrawableKnown:return /self->primary/
{printf("END tid=%d wall_us=%d\n",tid,(timestamp-self->began)/1000);self->primary=0;}
tick-12s {exit(0);}
