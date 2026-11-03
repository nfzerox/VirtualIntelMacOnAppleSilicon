/* Read-only Darwin10 mapped-display clock attribution. Actual Snow
 * MPGetNextWakeupTime loads device+0x30 directly as StdFBShmem, whose period
 * SDK vblTime/vblDelta are at0x40/0x48. Do NOT use Mavericks' pointer chain.
 * Raw Mach ticks only: no subtraction from DTrace's unrelated clock epoch.
 * Require two stable copies. Callback/clock values are not rendered FPS.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY Snow mapped-display VBL clock\n");}
pid$target:CoreGraphics:MPGetNextWakeupTime:entry /arg0/
{
    this->shared=*(uint64_t *)copyin(arg0+0x30,8);
    this->a=(uint64_t *)copyin(this->shared+0x40,16);
    this->b=(uint64_t *)copyin(this->shared+0x40,16);
    this->stable=this->a[0]==this->b[0] && this->a[1]==this->b[1];
    @stable[this->stable]=count();
    @clock[this->stable?this->a[0]:0,this->stable?this->a[1]:0]=count();
    @selector[(int32_t)arg1]=count();
}
tick-12s {exit(0);}
ERROR {printf("INVALID: mapped-display clock probe error\n");exit(2);}
END {
    printa("STABLE %d count=%@d\n",@stable);printa("SELECTOR %d count=%@d\n",@selector);
    printa("VBL ticks=%lu delta_ticks=%lu reads=%@d\n",@clock);
}
