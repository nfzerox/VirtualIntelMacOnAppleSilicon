/* Finite native-Intel / guest Snow comparison of named CoreGraphics stages.
 * No Apple argument/object layout, pixel capture, instruction patch or input.
 * CGXUpdateDisplay/CGXGLAccelFinish were located in the actual period binaries;
 * require matching real probes, not guessed Mavericks offsets. Stage times
 * are inclusive and must not be added. vtimestamp is NOT host on-CPU time
 * while the guest executes a synchronous emulator operation.
 * Drive the same real Finder window only after READY. This is attribution,
 * not a clean FPS measurement; aggregate actual rendering updates separately
 * from inexpensive deferred invocations. No per-call output while running.
 */
#pragma D option quiet
#pragma D option aggsize=8m
#pragma D option dynvarsize=4m
BEGIN {printf("READY Snow named update-stage comparison\n");}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{self->start=timestamp;self->finish=0;self->finishes=0;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->depth/
{self->finish_at=timestamp;self->finishes++;}
pid$target:CoreGraphics:CGXGLAccelFinish:return /self->finish_at/
{self->finish+=(timestamp-self->finish_at)/1000;self->finish_at=0;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1/
{
    this->us=(timestamp-self->start)/1000;
    @calls[self->finishes>0]=count();@wall[self->finishes>0]=sum(this->us);
    @finish[self->finishes>0]=sum(self->finish);@updates[self->finishes>0]=quantize(this->us);
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->finishes/
{
    @finish_counts[self->finishes]=count();
    @render_rows[(uint64_t)self->start,(uint64_t)((timestamp-self->start)/1000),
        (uint64_t)self->finish,(uint32_t)self->finishes]=count();
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-12s {exit(0);}
ERROR {printf("INVALID: update probe error\n");exit(2);}
END {
    printa("TOTAL rendering=%d calls=%@d wall_us=%@d nested_finish_us=%@d\n",@calls,@wall,@finish);
    printf("UPDATE histograms by rendering flag\n");printa(@updates);
    printa("FINISH_COUNT %u updates=%@d\n",@finish_counts);
    printa("RENDER start_ns=%lu wall_us=%lu finish_us=%lu finishes=%u count=%@d\n",@render_rows);
}
