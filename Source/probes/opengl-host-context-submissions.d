/* Actual Intel-host entry/stack survey, no guessed tail-call return timing.
 * Scope only the current owned QEMU process; verify loaded function/module
 * names first. Stack counts identify native submission callers, not CPU time,
 * exact frame cost or clean displayed FPS. No pointer/pixel contents or writes.
 * Wait for READY and positive live PID before posting actual guest transitions.
 */
#pragma D option quiet
#pragma D option aggsize=8m
BEGIN { began=timestamp;printf("READY native GL context/submission survey target=%d\n",$target); }
pid$target:libVIMPassthroughOpenGLHost.dylib:DGInlineWritev:entry
{ @inline_calls=count(); }
pid$target:libGL.dylib:CGLSetCurrentContext:entry
{ @current[arg0==0]=count(); }
pid$target:libGL.dylib:glFlush:entry
{ @explicit_flush=count(); }
pid$target:AppleIntelICLGraphicsGLDriver:intelSubmitCommands:entry
{ @submissions[ustack(14)]=count(); }
tick-1s /timestamp-began>=16000000000/ { exit(0); }
END {
    printf("INLINE exchanges\n");printa(@inline_calls);
    printf("CGL switches is_null calls\n");printa("%u %@d\n",@current);
    printf("EXPLICIT glFlush entries\n");printa(@explicit_flush);
    printf("ACTUAL Intel submissions by caller stack (NOT additive CPU time)\n");printa(@submissions);
}
