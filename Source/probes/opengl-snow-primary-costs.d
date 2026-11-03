/* Finite Snow x64 publication attribution. The offset is the measured OWN
 * VIMContext.drawable (6544), not an Apple ABI; VIMCGSDrawable starts with
 * connection/window/surface ints. Match this build's layout before use.
 * Separate completed primary publication from intermediate CoreGraphics
 * rendering invocations. Inclusive times/trace counts are NOT displayed FPS.
 */
#pragma D option quiet
#pragma D option dynvarsize=2m
#pragma D option aggsize=4m
BEGIN {printf("READY Snow primary-publication costs\n");}
pid$target:GLEngine:VIMPresentDrawableKnown:entry /arg0/
{
    this->d=*(uint64_t *)copyin(arg0+6544,8);
    self->primary_at=this->d && *(uint32_t *)copyin(this->d+8,4)==1?timestamp:0;
}
pid$target:GLEngine:VIMPresentDrawableKnown:return /self->primary_at/
{
    this->us=(timestamp-self->primary_at)/1000;
    @calls=count();@wall=sum(this->us);@cost=quantize(this->us);
    @rows[(uint64_t)self->primary_at,(uint64_t)timestamp]=count();self->primary_at=0;
}
tick-12s {exit(0);}
ERROR {printf("INVALID: primary probe error\n");exit(2);}
END {
    printa("PRIMARY calls=%@d inclusive_us=%@d\n",@calls,@wall);printa(@cost);
    printa("PRIMARY_SPAN start_ns=%lu end_ns=%lu count=%@d\n",@rows);
}
