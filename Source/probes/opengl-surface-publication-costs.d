/* Finite own-client publication survey. Typed public C function arguments:
 * DG{Publish,Copy}SurfaceRegions(lease, regions, unsigned count, ...).
 * No region/pixel contents, per-GL-call probes, or memory/code modification.
 * Wait for READY and confirm a live dtrace before real production input.
 * Inclusive wall times include transport/GPU waits and instrumentation;
 * nested costs are NOT additive or evidence of clean presented FPS.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN { began=timestamp; printf("READY own surface publication costs target=%d\n",$target); }
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishSurfaceRegions:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGCopySurfaceRegions:entry
{ self->regions[probefunc]=(uint32_t)arg2; }
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishSurfaceRegions:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGCopySurfaceRegions:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishSurfaceEpoch:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGSealSurfaceVersion:entry
{ self->at[probefunc]=timestamp; }
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishSurfaceRegions:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGCopySurfaceRegions:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishSurfaceEpoch:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGSealSurfaceVersion:return
/self->at[probefunc]/
{
    this->us=(timestamp-self->at[probefunc])/1000;
    @calls[probefunc,self->regions[probefunc],(int32_t)arg1]=count();
    @us[probefunc,self->regions[probefunc],(int32_t)arg1]=sum(this->us);
    @max_us[probefunc,self->regions[probefunc],(int32_t)arg1]=max(this->us);
    self->at[probefunc]=0;
}
tick-1s /timestamp-began>=12000000000/ { exit(0); }
END {
    printf("function regions result calls inclusive_us maximum_us\n");
    printa("%s %u %d %@d %@d %@d\n",@calls,@us,@max_us);
}
