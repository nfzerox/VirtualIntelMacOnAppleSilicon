/* Finite own-executor composition costs. Aggregates only, no pixels, names,
 * private Apple layouts or injected inferior calls. Nested wall times are
 * NOT additive, and an instrumented transition is not an FPS qualification.
 * -Z permits optimized-away functions; absent probes are not zero work.
 */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=4m
BEGIN {began=timestamp;printf("READY executor transition copies\n");}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCopyFlippedAlias:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMTransfer:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceSnapshotCreate:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceCopyRegions:entry,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGDrawableStorage:entry
{self->started[probefunc]=timestamp;}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGCopyFlippedAlias:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMTransfer:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceSnapshotCreate:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGSurfaceCopyRegions:return,
pid$target:libVIMPassthroughOpenGLHost.dylib:DGDrawableStorage:return
/self->started[probefunc]/
{
    this->us=(timestamp-self->started[probefunc])/1000;
    @calls[probefunc]=count();@us[probefunc]=sum(this->us);@max[probefunc]=max(this->us);
    self->started[probefunc]=0;
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {printa("%s calls=%@d inclusive_us=%@d max_us=%@d\n",@calls,@us,@max);}
