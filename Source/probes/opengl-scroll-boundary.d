/* Finite metadata-only x86 guest scroll survey: WINDOWSERVER_PID APP_PID.
 * No private structure offsets, memory writes, policy changes or pixel reads.
 * Optional symbols differ by OS; -Z permits absent functions. Run in a guest,
 * with its normal tracing authorization, never against host WindowServer.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { printf("READY scroll boundary\n"); }

pid$1:CoreGraphics:CGSBackingStoreCopy:entry,
pid$1:CoreGraphics:CGBlt_copyBytes:entry,
pid$1:CoreGraphics:CGXBackingStoreDataLock:entry,
pid$1:CoreGraphics:CGXBackingStoreDataUnlock:entry,
pid$1:CoreGraphics:*CGXHWMoveRegion*:entry,
pid$2:CoreGraphics:CGSDeviceScroll:entry,
pid$2:CoreGraphics:CGBlt_copyBytes:entry,
pid$2:CoreGraphics:CGSDeviceLock:entry,
pid$2:CoreGraphics:CGSDeviceUnlock:entry,
pid$2:GLEngine:*VIMImportAcceleratorBacking*:entry,
pid$2:GLEngine:*VIMPrepareOrdinaryDrawing*:entry,
pid$2:GLEngine:*VIMPresentDrawableKnown*:entry,
pid$2:libVIMPassthroughOpenGL*:*DGImportDrawable*:entry,
pid$2:libVIMPassthroughOpenGL*:*DGReadDrawableRectRows*:entry,
pid$2:libVIMPassthroughOpenGL*:*DGPublishBacking*:entry
/events++<1200/
{
    printf("%llu pid=%d %s:%s args=%llx,%llx,%llx,%llx,%llx,%llx\n",
        (unsigned long long)timestamp,pid,probemod,probefunc,
        (unsigned long long)arg0,(unsigned long long)arg1,(unsigned long long)arg2,
        (unsigned long long)arg3,(unsigned long long)arg4,(unsigned long long)arg5);
}
tick-15sec { printf("END scroll boundary\n"); exit(0); }
