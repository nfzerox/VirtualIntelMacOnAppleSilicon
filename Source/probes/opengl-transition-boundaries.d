/* WindowServer entry timings without screen capture. No return probes,
 * guessed offsets, private object reads, or per-event logging. Run with -Z
 * because the native and virtual graphics driver probes are alternatives.
 * Tracing itself adds overhead: do not substitute this for an FPS test. */
#pragma D option quiet
#pragma D option aggsize=4m

BEGIN
{
    began=timestamp;
    budget=8192;
    printf("READY compositor entry timings target=%d\n",$target);
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /budget>0/
{ @events[timestamp-began,tid,1]=count(); budget--; }
pid$target:CoreGraphics:CGXGLAccelFinish:entry /budget>0/
{ @events[timestamp-began,tid,2]=count(); budget--; }
pid$target:CoreGraphics:begin_deferred_update_cycle:entry /budget>0/
{ @events[timestamp-began,tid,3]=count(); budget--; }
pid$target:libVIMPassthroughOpenGL*:DGPresentSurfaceVersion:entry /budget>0/
{ @events[timestamp-began,tid,4]=count(); budget--; }
pid$target:AppleIntelHD5000GraphicsGLDriver*:glrFlushContextToken:entry /budget>0/
{ @events[timestamp-began,tid,5]=count(); budget--; }

tick-1s /timestamp-began>=15000000000/ { exit(0); }

END
{
    printf("elapsed_ns tid event count (1=update 2=finish 3=defer 4=virtual publish 5=Intel submit)\n");
    printa("%lu %d %d %@d\n", @events);
    printf("END budget=%d\n",budget);
}
