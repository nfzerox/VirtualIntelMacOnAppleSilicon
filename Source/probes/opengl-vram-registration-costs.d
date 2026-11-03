/* Finite host-side scalar-only survey; no task-memory copyin under SIP.
 * The owned DGVRAMBacking has actual returns after its work (no tail-call
 * timing). DeleteBacking can tail-call free: count entries ONLY. Run the
 * matched visible transition after READY. Inclusive wall time is not FPS,
 * CPU time, or an additive breakdown of the complete compositor workload.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY backing-registration costs\n");}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMBacking:entry
{self->began=timestamp;self->creating=arg2;@calls["registration-lookups"]=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMBacking:return
/self->began/
{
    @count[self->creating,(int32_t)arg1]=count();
    @us[self->creating,(int32_t)arg1]=sum((timestamp-self->began)/1000);
    @maximum[self->creating,(int32_t)arg1]=max((timestamp-self->began)/1000);
    self->began=0;
}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMDeleteBacking:entry
/arg0/
{@calls["registration-destructions"]=count();}
pid$target:libVIMPassthroughOpenGLHost.dylib:DGVRAMCopyDrawable:entry
{@calls["coherent-GPU-copies"]=count();}
tick-15s {exit(0);}
END {
    printa(@calls);
    printf("creating native-result calls inclusive-us maximum-us\n");
    printa("%u %d %@d %@d %@d\n",@count,@us,@maximum);
}
