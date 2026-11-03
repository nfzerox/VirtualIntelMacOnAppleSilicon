/* 13F1911 CoreGraphics UUID ED5E3C37-7E2F-3885-8091-DFC9B44B38E8.
 * Matching disassembly verifies real rets for all timed functions, including
 * CGXSynchronizeSurfaces (3f85ac) and UpdateDisplaySynchronizeUpdate (1c3049).
 * Inclusive nested wall times are NOT additive and are NOT presentation FPS.
 * No pixels, pointer layouts, system writes or per-GL-call instrumentation.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY transform service wall cost\n");}
pid$target:CoreGraphics:CGXSetWindowTransformList:entry,
pid$target:CoreGraphics:CGXUpdateDisplaySynchronizeUpdate:entry,
pid$target:CoreGraphics:CGXSynchronizeSurfaces:entry,
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{self->depth[probefunc]++;}
pid$target:CoreGraphics:CGXSetWindowTransformList:entry,
pid$target:CoreGraphics:CGXUpdateDisplaySynchronizeUpdate:entry,
pid$target:CoreGraphics:CGXSynchronizeSurfaces:entry,
pid$target:CoreGraphics:CGXUpdateDisplay:entry
/self->depth[probefunc]==1/
{self->start[probefunc]=timestamp;}
pid$target:CoreGraphics:CGXSetWindowTransformList:return,
pid$target:CoreGraphics:CGXUpdateDisplaySynchronizeUpdate:return,
pid$target:CoreGraphics:CGXSynchronizeSurfaces:return,
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->depth[probefunc]==1 && self->start[probefunc]/
{
    this->us=(timestamp-self->start[probefunc])/1000;
    @count[probefunc]=count();@sum[probefunc]=sum(this->us);
    @max[probefunc]=max(this->us);@wall[probefunc]=quantize(this->us);
}
pid$target:CoreGraphics:CGXSetWindowTransformList:return,
pid$target:CoreGraphics:CGXUpdateDisplaySynchronizeUpdate:return,
pid$target:CoreGraphics:CGXSynchronizeSurfaces:return,
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->depth[probefunc]/
{self->depth[probefunc]--;}
tick-12s {exit(0);}
END {printa("%s calls=%@d inclusive_us=%@d max_us=%@d\n",@count,@sum,@max);
    printa(@wall);}
