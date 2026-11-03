/* IOBlitSurface's public 10.8 x86_64 size.width/height offsets (0x14/0x18).
 * Finite allocation metadata only; force Intel for physical qualification. */
#pragma D option quiet
BEGIN { printf("READY GA allocation survey\n"); }
pid$target:*GA*:*AllocateSurface*:entry
/arg2/
{
    this->size=(int32_t *)copyin(arg2+0x14,8);
    @allocations[probemod,arg1,arg3,this->size[0],this->size[1]]=count();
}
tick-12sec { exit(0); }
END { printf("module options surface width height calls\n");printa(@allocations); }
