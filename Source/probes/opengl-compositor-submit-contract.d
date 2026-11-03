/* Finite current-renderer submission audit. Distinguish the actual finish
 * argument, not function names such as CGXGLAccelFinish. Phase times overlap
 * and must NOT be summed. No pixels/private structures or per-packet probes;
 * instrumented durations are attribution, not clean FPS/GPU timing. Require
 * READY, overlapping real input and positive renderer hits. */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY compositor submission contract target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->update++;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->update/ {self->update--;}
pid$target:GLEngine:VIMGLIFlushMode:entry
{
    self->flush_depth++;
    self->flush_at[self->flush_depth]=timestamp;
    self->flush_finish[self->flush_depth]=(uint32_t)arg1;
    self->flush_update[self->flush_depth]=self->update!=0;
}
pid$target:GLEngine:VIMGLIFlushMode:return /self->flush_depth/
{
    this->finish=self->flush_finish[self->flush_depth];
    this->update=self->flush_update[self->flush_depth];
    this->us=(timestamp-self->flush_at[self->flush_depth])/1000;
    @calls[this->finish,this->update]=count();
    @us[this->finish,this->update]=sum(this->us);
    @maximum[this->finish,this->update]=max(this->us);
    self->flush_at[self->flush_depth]=0;self->flush_depth--;
}
pid$target:GLEngine:VIMPresentDrawableKnown:entry,
pid$target:GLEngine:VIMPublishIOSurfaceWritesLocked:entry,
pid$target:GLEngine:VIMFlushSurfacesForFramebuffer:entry
{
    self->depth[probefunc]++;
    self->at[probefunc,self->depth[probefunc]]=timestamp;
    self->up[probefunc,self->depth[probefunc]]=self->update!=0;
}
pid$target:GLEngine:VIMPresentDrawableKnown:return,
pid$target:GLEngine:VIMPublishIOSurfaceWritesLocked:return,
pid$target:GLEngine:VIMFlushSurfacesForFramebuffer:return
/self->depth[probefunc]/
{
    this->us=(timestamp-self->at[probefunc,self->depth[probefunc]])/1000;
    this->update=self->up[probefunc,self->depth[probefunc]];
    @phase_calls[probefunc,this->update]=count();
    @phase_us[probefunc,this->update]=sum(this->us);
    @phase_max[probefunc,this->update]=max(this->us);
    self->at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
END {
    printf("FLUSH finish_argument inside_update calls inclusive_us maximum_us\n");
    printa("%u %u %@d %@d %@d\n",@calls,@us,@maximum);
    printf("PHASE name inside_update calls inclusive_us maximum_us (OVERLAPPING)\n");
    printa("%s %u %@d %@d %@d\n",@phase_calls,@phase_us,@phase_max);
}
