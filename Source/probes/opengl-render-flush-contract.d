/* Finite, metadata-only native Intel/research comparison. Use -Z because
 * only ONE engine is present in each process. READY must precede actual
 * input. Require real renderer hits, not merely successful probe creation.
 * Tail-jump wrappers have no reliable return probes: this counts caller
 * paths, not durations/FPS, and never reads drawable/resource memory. */
#pragma D option quiet
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY render flush contract target=%d\n",$target);}
pid$target:OpenGL:*glFlushRenderAPPLE:entry,
pid$target:OpenGL:*glFinishRenderAPPLE:entry,
pid$target:OpenGL:*glFlush:entry,
pid$target:OpenGL:*glFinish:entry,
pid$target:*Mercury*:*gldFlushContext:entry,
pid$target:*Mercury*:*gldFinishContext:entry,
pid$target:*Mercury*:*gldWaitForContext:entry,
pid$target:*Intel*:*gldPresentFramebufferData:entry,
pid$target:GLEngine:*VIMGLIFlushMode:entry
{
    @entries[probemod,probefunc]=count();
    @callers[probemod,probefunc,ustack(12)]=count();
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {
    printf("ENTRY COUNTS (not paired timing)\n");printa(@entries);
    trunc(@callers,32);printf("TOP CALLER PATHS (not CPU attribution)\n");printa(@callers);
}
