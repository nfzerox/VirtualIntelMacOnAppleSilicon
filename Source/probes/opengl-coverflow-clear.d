/* Compare real native Intel and bridge GLI entry points during Cover Flow
 * selection changes. The SDK GLI ABI supplies context in arg0 for both.
 * Bounded metadata only: no pixels, strings, private OS-structure offsets or
 * GL state changes. Aggregate view sizes and clearing; not an FPS test. */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
#pragma D option bufsize=4m
BEGIN { printf("READY Cover Flow clear survey\n"); }
pid$target:GLEngine:*glViewport_Exec:entry,
pid$target:GLEngine:*VIMDispatch_viewport:entry
{
    width[arg0]=arg3;height[arg0]=arg4;
    @views[probefunc,arg3,arg4]=count();
}
pid$target:GLEngine:*glClear_Exec:entry,
pid$target:GLEngine:*VIMDispatch_clear:entry
{
    @clears[probefunc,width[arg0],height[arg0],arg1]=count();
}
pid$target:GLEngine:*glClear_Exec:entry,
pid$target:GLEngine:*VIMDispatch_clear:entry
/height[arg0]>0 && height[arg0]<=100/
{
    printf("small clear %s context=%llx view=%dx%d mask=%x\n",probefunc,
        (unsigned long long)arg0,(int)width[arg0],(int)height[arg0],(uint32_t)arg1);
    ustack(12);
}
tick-12sec { exit(0); }
pid$target:GLEngine:*glDraw*Exec:entry,
pid$target:GLEngine:*VIMDispatch_draw*:entry
/height[arg0]>0 && height[arg0]<=100/
{ @small_draws[probefunc,ustack(16)]=count(); }
pid$target:*AppleIntel*:*glrIntelBeginPrimitiveBuffer:entry
{ @intel_submissions[probemod]=count(); }
END {
    printf("VIEWS\n");printa(@views);printf("CLEARS\n");printa(@clears);
    printf("SMALL DRAW STACKS\n");printa(@small_draws);
    printf("ACTUAL INTEL SUBMISSIONS\n");printa(@intel_submissions);
}
