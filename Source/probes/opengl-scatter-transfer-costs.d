/* QEMU-side prepared-row DMA boundary only. Metadata, no pixels/addresses.
 * Inclusive time includes stream backpressure. Input starts after READY.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN { printf("READY native scatter transfer costs\n"); }
pid$target::vmsvga_passthrough_opengl_scatter:entry
{ self->at=timestamp;self->bytes=arg3;self->ranges=arg4; }
pid$target::vmsvga_passthrough_opengl_scatter:return /self->at/
{
    @calls[self->bytes]=count();@us[self->bytes]=sum((timestamp-self->at)/1000);
    @max[self->bytes]=max((timestamp-self->at)/1000);@ranges[self->ranges]=count();self->at=0;
}
tick-12sec { exit(0); }
END {
    printf("BYTES calls wall-us max-us\n");printa("%d %@d %@d %@d\n",@calls,@us,@max);
    printf("RANGES calls\n");printa("%d %@d\n",@ranges);
}
