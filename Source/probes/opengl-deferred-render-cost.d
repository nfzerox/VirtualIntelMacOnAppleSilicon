/* Low-probe-count display-update wall times, versus per-GL-call tracing.
 * Verify the matching CoreGraphics UUID and real CGXUpdateDisplay ret first.
 * No pixels, scheduler writes or assertions of displayed FPS.
 */
#pragma D option quiet
BEGIN {printf("READY deferred render wall cost\n");}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
/self->depth==1/
{self->start=timestamp;self->renders=0;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry
/self->depth/
{self->renders++;}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->depth==1 && self->start/
{
    @calls[self->renders]=count();
    @wall_us[self->renders]=quantize((timestamp-self->start)/1000);
    @sum_us[self->renders]=sum((timestamp-self->start)/1000);
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->depth/
{self->depth--;}
tick-12s {exit(0);}
END {printf("call counts by actual finish-entry count\n");printa(@calls);
    printf("inclusive wall microseconds\n");printa(@wall_us);
    printf("total inclusive wall microseconds\n");printa(@sum_us);}
