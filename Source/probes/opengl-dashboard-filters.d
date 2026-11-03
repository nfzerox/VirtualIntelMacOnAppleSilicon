/* Bounded real widget-addition survey. Run against Dock with -p PID -Z,
 * wait for READY, then add one widget through Dashboard's normal UI.
 * Calls/returns and stacks are recorded, not private CF object layouts.
 * Pair with visible animation/capture; filter allocation alone is no pass.
 * CGSAdd/RemoveWindowFilter tail-jump into the MIG sender. Their pid return
 * probes can observe the connection port before the tail call, NOT its result;
 * trace _CGSSetWindowCIFilter's actual return instead. */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { printf("READY Dashboard filter survey\n"); }
pid$target:CoreGraphics:CGSNewCIFilterByName:entry,
pid$target:CoreGraphics:CGSSetCIFilterValuesFromDictionary:entry,
pid$target:CoreGraphics:CGSAddWindowFilter:entry,
pid$target:CoreGraphics:CGSRemoveWindowFilter:entry,
pid$target:CoreGraphics:CGSReleaseCIFilter:entry
{
    printf("%llu %s arg0=%x arg1=%x arg2=%x arg3=%x\n",
        (unsigned long long)timestamp,probefunc,(uint32_t)arg0,(uint32_t)arg1,(uint32_t)arg2,(uint32_t)arg3);
    ustack(8);
    @calls[probefunc]=count();
}
pid$target:CoreGraphics:CGSNewCIFilterByName:return,
pid$target:CoreGraphics:CGSSetCIFilterValuesFromDictionary:return,
pid$target:CoreGraphics:_CGSSetWindowCIFilter:return,
pid$target:CoreGraphics:CGSReleaseCIFilter:return
{ printf("%llu %s return=%d\n",(unsigned long long)timestamp,probefunc,(int)arg1); }
tick-25sec { exit(0); }
END { printa(@calls); }
