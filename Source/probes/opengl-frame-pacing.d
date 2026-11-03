/* Finite native/guest comparison using symbol boundaries only. No private
 * structure offsets, pixels, policy changes or assumed display FPS. Nested
 * wall times overlap; compare identical input and window geometry. */
#pragma D option quiet
#pragma D option dynvarsize=4m
BEGIN {began=timestamp;last=(uint64_t)0;printf("READY frame pacing target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry,
pid$target:CoreGraphics:CGXGLAccelComposite:entry,
pid$target:CoreGraphics:CGXGLAccelFinish:entry,
pid$target:CoreGraphics:MPGetNextWakeupTime:entry
{
    @entries[probefunc]=count();
    self->depth[probefunc]++;
    self->at[probefunc,self->depth[probefunc]]=timestamp;
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{
    this->gap=last?(timestamp-last)/1000000:0;
    @update_gaps_ms=quantize(this->gap);last=timestamp;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXGLAccelComposite:return,
pid$target:CoreGraphics:CGXGLAccelFinish:return,
pid$target:CoreGraphics:MPGetNextWakeupTime:return
/self->depth[probefunc]/
{
    this->us=(timestamp-self->at[probefunc,self->depth[probefunc]])/1000;
    @calls[probefunc]=count();@wall_us[probefunc]=sum(this->us);
    @max_us[probefunc]=max(this->us);@latency_us[probefunc]=quantize(this->us);
    self->at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID frame pacing trace\n");exit(2);}
END {
    printf("ENTRIES (missing return probes are NOT completed durations)\n");printa(@entries);
    printf("FUNCTION calls inclusive_wall_us maximum_us\n");
    printa("%s %@d %@d %@d\n",@calls,@wall_us,@max_us);
    printf("UPDATE_ENTRY_GAPS_MS\n");printa(@update_gaps_ms);
    printf("INCLUSIVE_LATENCY_US\n");printa(@latency_us);
}
