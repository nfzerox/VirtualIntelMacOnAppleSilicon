/* Bounded common 13F1911 guest/physical-Intel compositor attribution.
 * All probes must match (no -Z). Each MIRROR row is ONE outer invocation,
 * not a presented frame. CPU time uses the target thread's vtimestamp;
 * category wall times can overlap and MUST NOT be added together. IOKit
 * wrapper nesting is unioned, rather than double-counting Method callers.
 * No pixels, pointers, object names or private data layouts are read.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=8m
BEGIN {began=timestamp;printf("READY common Mirror critical path target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry /!self->mirror/
{
    self->root=timestamp;self->cpu=vtimestamp;
    self->ogl=0;self->sw=0;self->bind=0;self->finish=0;self->blit=0;
    self->io=0;self->io_calls=0;
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry {self->mirror++;}
pid$target:QuartzCore:*OGL??GLContext??draw_elements*:entry,
pid$target:QuartzCore:*OGL??SWContext??draw_elements*:entry,
pid$target:QuartzCore:*OGL??CGLContext??bind_image_impl*:entry,
pid$target:QuartzCore:*OGL??CGLContext??finish_image*:entry,
pid$target:CoreGraphics:layer_blit_entry:entry
{
    self->depth[probefunc]++;
    self->at[probefunc,self->depth[probefunc]]=timestamp;
    @entries[probefunc,self->mirror!=0]=count();
}
pid$target:QuartzCore:*OGL??GLContext??draw_elements*:return,
pid$target:QuartzCore:*OGL??SWContext??draw_elements*:return,
pid$target:QuartzCore:*OGL??CGLContext??bind_image_impl*:return,
pid$target:QuartzCore:*OGL??CGLContext??finish_image*:return,
pid$target:CoreGraphics:layer_blit_entry:return
/self->depth[probefunc]/
{
    this->us=(timestamp-self->at[probefunc,self->depth[probefunc]])/1000;
    @calls[probefunc,self->mirror!=0]=count();
    @us[probefunc,self->mirror!=0]=sum(this->us);
    @maximum[probefunc,self->mirror!=0]=max(this->us);
    self->ogl+=self->mirror && strstr(probefunc,"OGL::GLContext::draw_elements")!=NULL?this->us:0;
    self->sw+=self->mirror && strstr(probefunc,"OGL::SWContext::draw_elements")!=NULL?this->us:0;
    self->bind+=self->mirror && strstr(probefunc,"bind_image_impl")!=NULL?this->us:0;
    self->finish+=self->mirror && strstr(probefunc,"finish_image")!=NULL?this->us:0;
    self->blit+=self->mirror && probefunc=="layer_blit_entry"?this->us:0;
    self->at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->mirror && !self->io_depth/
{self->io_at=timestamp;}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->mirror/
{self->io_depth++;self->io_calls++;}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->io_depth==1/
{self->io+=(timestamp-self->io_at)/1000;}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->io_depth/
{self->io_depth--;}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->mirror==1/
{
    printf("MIRROR tid=%d start_ns=%d wall_us=%d cpu_us=%d ogl_us=%d sw_us=%d bind_us=%d finish_us=%d blit_us=%d io_union_us=%d io_calls=%d\n",
        tid,self->root,(timestamp-self->root)/1000,(vtimestamp-self->cpu)/1000,
        self->ogl,self->sw,self->bind,self->finish,self->blit,self->io,self->io_calls);
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return /self->mirror/ {self->mirror--;}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {
    printf("Common phase function inside_Mirror entered returned inclusive_us maximum_us\n");
    printa("%s %d %@d %@d %@d %@d\n",@entries,@calls,@us,@maximum);
    printf("END common Mirror critical path\n");
}
