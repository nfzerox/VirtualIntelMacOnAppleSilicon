/* Finite WindowServer CI/ARB survey during one real widget insertion.
 * No GL error consumption, instruction patch, pixel or source dump.
 * Native reference runs must retain Intel driver/context evidence. */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { printf("READY Dashboard render survey\n"); }
pid$target:CoreGraphics:CGSCoreImageApplyFilter2:entry,
pid$target:libGL.dylib:glProgramStringARB:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glProgramStringARB:entry
{
    @calls[probemod,probefunc]=count();
    ustack(12);
}
tick-20sec { exit(0); }
END { printa(@calls); }
