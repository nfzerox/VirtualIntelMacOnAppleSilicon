/* Finite metadata-only survey of our source-defined GLI. Use the matching
 * Snow x64 build with distinct ordinary/render Flush callbacks. The drawable
 * offsets describe OUR structs, not an Apple ABI. Never enabled in production.
 * Exercise Spotlight typing or Dashboard insertion after READY. */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN {budget=192;printf("READY Snow render boundary\n");}
pid$target:GLEngine:VIMGLIFlushMode:entry /budget>0/
{
    this->d=*(uint64_t*)copyin(arg0+6544,8);
    self->primary=this->d && *(uint32_t*)copyin(this->d+8,4)==1;
    self->render=arg2;self->finish=arg1;
}
pid$target:GLEngine:VIMGLIFlushMode:entry /budget>0 && self->primary/
{
    budget--;
    printf("%llu SUBMIT tid=%d finish=%d render_only=%d\n",
        (unsigned long long)timestamp,(int)tid,(int)arg1,(int)arg2);
    @submits[(int)arg1,(int)arg2]=count();
}
pid$target:GLEngine:VIMPresentDrawableKnown:entry /budget>0 && self->primary/
{
    budget--;@presents[(int)self->finish,(int)self->render]=count();
    printf("%llu PUBLISH tid=%d finish=%d render_only=%d\n",
        (unsigned long long)timestamp,(int)tid,(int)self->finish,(int)self->render);
}
pid$target:GLEngine:VIMGLIFlushMode:return {self->primary=0;}
tick-16s {exit(0);}
END {printf("SUBMITS finish render-only\n");printa(@submits);
    printf("PUBLICATIONS finish render-only\n");printa(@presents);}
