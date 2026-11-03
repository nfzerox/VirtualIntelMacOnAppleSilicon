/* Bounded read-only period GLI metadata trace. Use -p WindowServer/Dock PID
 * on the supplied physical Intel reference. No pixels, code patch or injection.
 * Snow's measured 3e5 record is eight words, 3e6/3e7 five words. */
pid$target::gliSetInteger:entry
/arg1 == 0x3e5/
{
    printf("set ctx=%p op=%x\n", arg0, arg1);
    tracemem(copyin(arg2, 32), 32);
}
pid$target::gliSetInteger:entry
/arg1 == 0x3e6 || arg1 == 0x3e7/
{
    printf("set ctx=%p op=%x\n", arg0, arg1);
    tracemem(copyin(arg2, 20), 20);
}
pid$target::gliAttachDrawable:entry,
pid$target::gliAttachDrawableWithOptions:entry
{
    printf("attach ctx=%p type=%x\n", arg0, arg1);
    tracemem(copyin(arg2, 12), 12);
}
BEGIN { printf("READY native Snow surface contract pid=%d\n",$target); }
tick-12s
{
    exit(0);
}
