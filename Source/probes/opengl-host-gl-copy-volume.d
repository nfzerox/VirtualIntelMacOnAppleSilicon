/* Host native-GL API ENTRIES only; no unsafe tail-wrapper return timing.
 * Requested texels are metadata, NOT exact GPU bus traffic, completion or FPS.
 * No pixels, pointers, shaders, app names or client payloads are inspected.
 * Keep the actual workload separate from setup/idle before comparing volumes.
 */
#pragma D option quiet
#pragma D option aggsize=8m
BEGIN {printf("READY host native GL copy volume target=%d\n",$target);}
pid$target:libGL.dylib:glBlitFramebufferEXT:entry,
pid$target:libGL.dylib:glBlitFramebuffer:entry
{
    this->sw=(int64_t)(int32_t)arg2-(int64_t)(int32_t)arg0;
    this->sh=(int64_t)(int32_t)arg3-(int64_t)(int32_t)arg1;
    this->dw=(int64_t)(int32_t)arg6-(int64_t)(int32_t)arg4;
    this->dh=(int64_t)(int32_t)arg7-(int64_t)(int32_t)arg5;
    this->src=(uint64_t)(this->sw<0?-this->sw:this->sw)*(uint64_t)(this->sh<0?-this->sh:this->sh);
    this->dst=(uint64_t)(this->dw<0?-this->dw:this->dw)*(uint64_t)(this->dh<0?-this->dh:this->dh);
    @calls[probefunc]=count();@source[probefunc]=sum(this->src);@destination[probefunc]=sum(this->dst);
    @blit_callers[probefunc,ustack(8)]=sum(this->dst);
}
pid$target:libGL.dylib:glCopyTexSubImage2D:entry
/(int32_t)arg6>0 && (int32_t)arg7>0/
{
    this->n=(uint64_t)(uint32_t)arg6*(uint32_t)arg7;
    @calls[probefunc]=count();@source[probefunc]=sum(this->n);@destination[probefunc]=sum(this->n);
}
pid$target:libGL.dylib:glTexSubImage2D:entry
/(int32_t)arg4>0 && (int32_t)arg5>0/
{
    this->n=(uint64_t)(uint32_t)arg4*(uint32_t)arg5;
    @calls[probefunc]=count();@source[probefunc]=sum(this->n);@destination[probefunc]=sum(this->n);
}
pid$target:libGL.dylib:glTexImage2D:entry
/(int32_t)arg3>0 && (int32_t)arg4>0/
{
    this->n=(uint64_t)(uint32_t)arg3*(uint32_t)arg4;
    @calls[probefunc]=count();@source[probefunc]=sum(this->n);@destination[probefunc]=sum(this->n);
}
pid$target:libGL.dylib:glReadPixels:entry
/(int32_t)arg2>0 && (int32_t)arg3>0/
{
    this->n=(uint64_t)(uint32_t)arg2*(uint32_t)arg3;
    @calls[probefunc]=count();@source[probefunc]=sum(this->n);@destination[probefunc]=sum(this->n);
}
tick-30s {exit(0);}
END {
    printf("API calls requested_source_texels requested_destination_texels\n");
    printa("%s %@d %@d %@d\n",@calls,@source,@destination);
    printf("blit callers and requested destination texels\n");printa(@blit_callers);
}
