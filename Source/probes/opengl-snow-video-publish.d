/* Follow a real Snow video renderer's output handoff (not decoder input).
 * Run in the guest: sudo dtrace -Z -q -p WINDOWSERVER_PID -s THIS_FILE
 * Seek and play after READY. Bounded metadata only; no pixel/state mutation.
 */
#pragma D option quiet
#pragma D option bufsize=2m
#pragma D option aggsize=1m

BEGIN { printf("READY 12-second video publication trace pid=%d\n",$target); }

pid$target:*:cglsAttachPBuffer:entry,
pid$target:*:cglsSetPBuffer:entry,
pid$target:*:cglsTexImagePBuffer*:entry,
pid$target:*:cgls*Swap*:entry,
pid$target:*:cgls*Flush*:entry,
pid$target:*:gliSwapBuffers:entry,
pid$target:*:VIMGLISwap:entry,
pid$target:*:VIMGLIFlushMode:entry,
pid$target:*:VIMPresentDrawableKnown:entry,
pid$target:*:VIMPresentTypedPBuffer:entry,
pid$target:*:DGBind*DrawableTexture*:entry,
pid$target:*:DGSnapshotDrawableTexture:entry,
pid$target:*:DGPublishSurfaceEpoch:entry
{ @calls[probemod,probefunc,arg0] = count(); }

pid$target:*:VIMGLIFlushMode:entry
{ @flushMode[arg0,arg1,arg2] = count(); }

pid$target:*:cglsAttachPBuffer:entry,
pid$target:*:cglsTexImagePBuffer*:entry,
pid$target:*:VIMGLISwap:entry,
pid$target:*:VIMPresentTypedPBuffer:entry
/details<64/
{
    details++;
    printf("%llu %s:%s tid=%d a=%p,%p,%p,%p\n",
        (unsigned long long)timestamp,probemod,probefunc,tid,arg0,arg1,arg2,arg3);
}

pid$target:*:cglsTexImagePBuffer*:return,
pid$target:*:cglsAttachPBuffer:return
/errors<32 && arg1/
{
    errors++;
    printf("RETURN %s:%s tid=%d value=%d\n",probemod,probefunc,tid,(int)arg1);
}

tick-12sec { exit(0); }
END {
    printa("%-20s %-36s arg0=%p count=%@d\n",@calls);
    printa("flush ctx=%p finish=%d render-only=%d count=%@d\n",@flushMode);
}
