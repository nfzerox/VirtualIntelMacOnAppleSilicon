/* Bounded native-Intel/guest compositor critical-path survey. This records
 * actual nested CPU call boundaries, NOT GPU completion or displayed FPS.
 * No Apple object offsets, pixels, keyboard data or memory writes. Match the
 * loaded CoreGraphics UUID to the studied 13F1911 image before use. The real
 * Intel driver counter must be positive for a physical-reference workload.
 *
 * Rows are buffered until END to avoid per-event SSH/output work. Durations
 * include instrumentation and guest-vCPU scheduling. Compare phase shapes,
 * not an unqualified native/guest speed ratio. A zero remaining budget means
 * truncation; absent optional probes are not evidence of zero cost.
 */
#pragma D option quiet
#pragma D option aggsize=8m
#pragma D option dynvarsize=4m
BEGIN {
    began=timestamp;budget=8192;
    printf("READY compositor frame phases target=%d\n",$target);
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->update_depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
/self->update_depth==1 && budget>0/
{
    self->root=++roots;self->started=timestamp;self->finishes=0;
    @rows[(timestamp-began)/1000,tid,self->root,10,(uint64_t)0,(uint64_t)0]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry
/self->root && budget>0/
{
    @rows[(timestamp-began)/1000,tid,self->root,20,(uint64_t)0,(uint64_t)0]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return
/self->root && budget>0/
{
    @rows[(timestamp-began)/1000,tid,self->root,21,(uint64_t)0,(uint64_t)0]=count();budget--;
}
pid$target:CoreGraphics:CGXGLAccelFinish:entry
/self->root && budget>0/
{
    self->finishes++;
    @rows[(timestamp-began)/1000,tid,self->root,30,(uint64_t)0,(uint64_t)0]=count();budget--;
}
pid$target:CoreGraphics:CGXGLAccelFinish:return
/self->root && budget>0/
{
    @rows[(timestamp-began)/1000,tid,self->root,31,(uint64_t)0,(uint64_t)0]=count();budget--;
}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:entry
/self->root && budget>0/
{
    @rows[(timestamp-began)/1000,tid,self->root,40,(uint64_t)(uint32_t)arg1,
        (uint64_t)(uint32_t)arg2]=count();budget--;
}
/* Studied IOKit wrapper has a real return, not a tail jump. */
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:return
/self->root && budget>0/
{
    @rows[(timestamp-began)/1000,tid,self->root,41,(uint64_t)(uint32_t)arg1,
        (uint64_t)0]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->update_depth==1 && self->root && budget>0/
{
    @rows[(timestamp-began)/1000,tid,self->root,11,
        (timestamp-self->started)/1000,self->finishes]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->update_depth==1/ {self->root=0;}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->update_depth/ {self->update_depth--;}
pid$target:AppleIntelHD5000GraphicsGLDriver*:glrFlushContextToken:entry
{@intel_submits=count();}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {
    printf("ROWS us tid root phase(10=update-begin,11=end,20=mirror-begin,21=end,30=finish-begin,31=end,40=display-flush-begin,41=end) value0 value1 count\n");
    printa("%lu %d %u %d %lu %lu %@d\n",@rows);
    printf("END compositor frame phases budget=%d roots=%u; Intel driver submits:\n",budget,roots);
    printa(@intel_submits);
}
