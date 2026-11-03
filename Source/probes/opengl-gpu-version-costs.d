/* Bounded inclusive native version costs. No pixel reads or inferior calls.
 * sudo dtrace -q -Z -p EXECUTOR_PID -s this-file; wait for READY before input.
 * Missing optional probes are not coverage. Do not add nested times or use
 * instrumented runs as presentation-rate evidence.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN { printf("READY native GPU version costs\n"); }
pid$target::DGSurfaceSnapshotCreate:entry,
pid$target::DGSurfaceCopyRegions:entry,
pid$target::DGSurfacePublishRect:entry,
pid$target::DGSurfaceAcquire:entry,
pid$target::DGDrawableStorage:entry,
pid$target::DGCompletionQueueWait:entry,
pid$target::DGDisplaySubmitSealed:entry,
pid$target::IOSurfaceCreate:entry,
pid$target::CGLTexImageIOSurface2D:entry
{ self->started[probefunc]=timestamp; }
pid$target::DGSurfaceSnapshotCreate:return,
pid$target::DGSurfaceCopyRegions:return,
pid$target::DGSurfacePublishRect:return,
pid$target::DGSurfaceAcquire:return,
pid$target::DGDrawableStorage:return,
pid$target::DGCompletionQueueWait:return,
pid$target::DGDisplaySubmitSealed:return,
pid$target::IOSurfaceCreate:return,
pid$target::CGLTexImageIOSurface2D:return
/self->started[probefunc]/
{
    this->us=(timestamp-self->started[probefunc])/1000;
    @calls[probefunc]=count();@us[probefunc]=sum(this->us);
    @maximum[probefunc]=max(this->us);@distribution[probefunc]=quantize(this->us);
    self->started[probefunc]=0;
}
tick-8sec { exit(0); }
END {
    printa("%s calls=%@d inclusive_us=%@d max_us=%@d\n",@calls,@us,@maximum);
    printa(@distribution);
}
