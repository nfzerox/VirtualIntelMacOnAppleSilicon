/* Bounded attribution only, never clean pacing/CPU-only/FPS evidence.
 * Matched 13F1911 CoreGraphics real CGXUpdateDisplay return; own Begin/End
 * have actual returns after releasing their connection mutex. Nested wall
 * durations are NOT additive. No pixels, arguments, per-GL probes or writes.
 * The explicit candidate must be checked via its exact-UUID control first. */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=4m
BEGIN {began=timestamp;budget=128;printf("READY scoped backing costs target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{self->at=timestamp;self->begin_us=0;self->end_us=0;self->begins=0;self->ends=0;self->retain_us=0;self->flush_us=0;}
pid$target:GLEngine:VIMRetainCPUPublishedDrawable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGFlushDrawableState:entry,
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->depth/
{self->nest[probefunc]++;self->other_at[probefunc,self->nest[probefunc]]=timestamp;}
pid$target:GLEngine:VIMRetainCPUPublishedDrawable:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGFlushDrawableState:return,
pid$target:CoreGraphics:CGXGLAccelFinish:return /self->nest[probefunc]/
{
    this->us=(timestamp-self->other_at[probefunc,self->nest[probefunc]])/1000;
    @other_calls[probefunc]=count();@other_sum[probefunc]=sum(this->us);@other_max[probefunc]=max(this->us);
    self->retain_us+=probefunc=="VIMRetainCPUPublishedDrawable"?this->us:0;
    self->flush_us+=probefunc=="DGFlushDrawableState"?this->us:0;
    self->other_at[probefunc,self->nest[probefunc]]=0;self->nest[probefunc]--;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:entry /self->depth/
{self->begin_at=timestamp;}
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:return /self->begin_at/
{
    this->us=(timestamp-self->begin_at)/1000;self->begin_us+=this->us;self->begins++;
    @calls["Begin",(int32_t)arg1]=count();@sum["Begin",(int32_t)arg1]=sum(this->us);@max["Begin",(int32_t)arg1]=max(this->us);
    self->begin_at=0;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGEndBackingRender:entry /self->depth/
{self->end_at=timestamp;}
pid$target:libVIMPassthroughOpenGL.dylib:DGEndBackingRender:return /self->end_at/
{
    this->us=(timestamp-self->end_at)/1000;self->end_us+=this->us;self->ends++;
    @calls["End",(int32_t)arg1]=count();@sum["End",(int32_t)arg1]=sum(this->us);@max["End",(int32_t)arg1]=max(this->us);
    self->end_at=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->at/
{
    @updates=count();@update_us=sum((timestamp-self->at)/1000);
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->at && self->ends && budget>0/
{
    @rows[(uint64_t)self->at,(uint64_t)((timestamp-self->at)/1000),
        (uint64_t)self->begin_us,(uint32_t)self->begins,(uint64_t)self->end_us,(uint32_t)self->ends,
        (uint64_t)self->retain_us,(uint64_t)self->flush_us]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {
    printf("SCOPES operation result calls inclusive_us max_us\n");printa("%s %d %@d %@d %@d\n",@calls,@sum,@max);
    printf("NESTED function calls inclusive_us max_us\n");printa("%s %@d %@d %@d\n",@other_calls,@other_sum,@other_max);
    printf("ROWS start_ns update_us begin_us begins end_us ends retained_us flush_us count\n");printa("%lu %lu %lu %u %lu %u %lu %lu %@d\n",@rows);
    printf("TOTAL updates inclusive_update_us\n");printa("%@d %@d\n",@updates,@update_us);
}
