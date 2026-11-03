/* Real owned-client future consumers, not an advertised capability or GPU
 * submission count. All exported probes must match; input starts only after
 * READY and a positively live tracer. Inclusive wall time is not FPS/CPU%. */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=4m
BEGIN {began=timestamp;printf("READY actual future consumers target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGL.dylib:DGCopyBackingFutureTexture:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGEndBackingRender:entry,
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry
{self->depth[probefunc]++;self->at[probefunc,self->depth[probefunc]]=timestamp;@entered[probefunc]=count();}
pid$target:libVIMPassthroughOpenGL.dylib:DGCopyBackingFutureTexture:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGEndBackingRender:return,
pid$target:CoreGraphics:CGXUpdateMagicMirror:return
/self->depth[probefunc]/
{
    @returned[probefunc]=count();
    @us[probefunc]=sum((timestamp-self->at[probefunc,self->depth[probefunc]])/1000);
    @status[probefunc,(int32_t)arg1]=count();
    self->at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {
    printf("function entered returned inclusive_wall_us\n");printa("%s %@d %@d %@d\n",@entered,@returned,@us);
    printf("function return_value count\n");printa("%s %d %@d\n",@status);
}
