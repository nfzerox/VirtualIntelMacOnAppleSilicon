/* Supplemental OWN bridge timing, used with opengl-mirror-critical-path.d.
 * The packet opcode is our fixed first uint32_t, not Apple object data.
 * Only synchronous exchanges are timed: instrumenting every buffered scalar
 * packet perturbs the workload and can exhaust Mavericks' dirty dynamic
 * variable list. Nested durations are inclusive and must not be summed with
 * the common root times. No per-call trace output or pixel capture. */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{self->display_depth++;self->display_at[self->display_depth]=timestamp;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->display_depth/
{
    this->us=(timestamp-self->display_at[self->display_depth])/1000;
    @display_calls=count();@display_us=sum(this->us);@display_max=max(this->us);
    self->display_at[self->display_depth]=0;self->display_depth--;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /arg2!=0/
{
    self->packet_depth++;self->packet_at[self->packet_depth]=timestamp;
    self->packet_op[self->packet_depth]=*(uint32_t *)copyin(arg1,4);
    self->packet_sync[self->packet_depth]=arg2!=0;
    self->packet_mirror[self->packet_depth]=self->mirror!=0;
    self->packet_display[self->packet_depth]=self->display_depth!=0;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return /self->packet_depth/
{
    this->op=self->packet_op[self->packet_depth];this->sync=self->packet_sync[self->packet_depth];
    this->mirror=self->packet_mirror[self->packet_depth];this->display=self->packet_display[self->packet_depth];
    this->us=(timestamp-self->packet_at[self->packet_depth])/1000;
    @packet_calls[this->op,this->sync,this->mirror,this->display]=count();
    @packet_us[this->op,this->sync,this->mirror,this->display]=sum(this->us);
    @packet_max[this->op,this->sync,this->mirror,this->display]=max(this->us);
    self->packet_op[self->packet_depth]=0;self->packet_sync[self->packet_depth]=0;
    self->packet_mirror[self->packet_depth]=0;self->packet_display[self->packet_depth]=0;
    self->packet_at[self->packet_depth]=0;self->packet_depth--;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGEndBackingRender:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGFlushDrawableState:entry,
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:entry,
pid$target:GLEngine:*VIMImportAcceleratorBacking*:entry,
pid$target:GLEngine:*VIMRetainCPUPublishedDrawable*:entry
{self->bridge_depth[probefunc]++;self->bridge_at[probefunc,self->bridge_depth[probefunc]]=timestamp;}
pid$target:libVIMPassthroughOpenGL.dylib:DGBeginBackingRender:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGEndBackingRender:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGFlushDrawableState:return,
pid$target:GLEngine:*VIMCreateAcceleratorDrawable*:return,
pid$target:GLEngine:*VIMImportAcceleratorBacking*:return,
pid$target:GLEngine:*VIMRetainCPUPublishedDrawable*:return
/self->bridge_depth[probefunc]/
{
    this->us=(timestamp-self->bridge_at[probefunc,self->bridge_depth[probefunc]])/1000;
    @bridge_calls[probefunc,self->mirror!=0,self->display_depth!=0]=count();
    @bridge_us[probefunc,self->mirror!=0,self->display_depth!=0]=sum(this->us);
    @bridge_max[probefunc,self->mirror!=0,self->display_depth!=0]=max(this->us);
    self->bridge_at[probefunc,self->bridge_depth[probefunc]]=0;self->bridge_depth[probefunc]--;
}
END {
    printf("OWN synchronous packet opcode sync inside_Mirror inside_UpdateDisplay calls inclusive_us max_us\n");
    printa("%x %d %d %d %@d %@d %@d\n",@packet_calls,@packet_us,@packet_max);
    printf("OWN bridge function inside_Mirror inside_UpdateDisplay calls inclusive_us max_us\n");
    printa("%s %d %d %@d %@d %@d\n",@bridge_calls,@bridge_us,@bridge_max);
    printf("UpdateDisplay calls inclusive_us max_us\n");printa("%@d %@d %@d\n",@display_calls,@display_us,@display_max);
}
