/* Own-client traffic by actual attached surface and drawing-view geometry.
 * Numeric wire metadata only; no pixels or private context offsets. This
 * is an instrumented path survey, not a frame-pacing measurement. Use -Z,
 * wait for READY, then exercise the real active Finder window. DGMessage's
 * fixed header/first arguments are defined in passthrough-opengl-wire.h.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN { printf("READY drawable traffic survey\n"); }
/* The exported attach wrapper is inlined into gliAttachDrawable by the
 * guest compiler. Probe the common, source-defined attachment body instead;
 * otherwise a perfectly valid capture silently labels everything surface 0. */
pid$target:GLEngine:VIMCreateAcceleratorDrawable:entry
/arg1/
{
    this->record=(uint32_t *)copyin(arg1,12);
    self->attaching=this->record[2];
    @attachments[self->attaching]=count();
}
pid$target:GLEngine:VIMCreateAcceleratorDrawable:return
{ self->attaching=0; }
pid$target:GLEngine:VIMGLINotifyCPUSurfaceCopy:entry
{ self->copy_start=timestamp; }
pid$target:GLEngine:VIMGLINotifyCPUSurfaceCopy:return
/self->copy_start/
{
    @copy_calls=count();@copy_us=sum((timestamp-self->copy_start)/1000);
    @copy_max_us=max((timestamp-self->copy_start)/1000);self->copy_start=0;
}
pid$target:IOKit:IOServiceOpen:entry,
pid$target:IOKit:IOServiceClose:entry,
pid$target:IOKit:IOServiceGetMatchingService:entry
/self->attaching/
{ self->service_start[probefunc]=timestamp; }
pid$target:IOKit:IOServiceOpen:return,
pid$target:IOKit:IOServiceClose:return,
pid$target:IOKit:IOServiceGetMatchingService:return
/self->service_start[probefunc]/
{
    @service_calls[probefunc]=count();
    @service_us[probefunc]=sum((timestamp-self->service_start[probefunc])/1000);
    self->service_start[probefunc]=0;
}
/* Appended method 35 in the checked VMsvga2 UCMethods.h. Capture only its
 * IOReturn, not a guessed interpretation of the user client's address space. */
pid$target:IOKit:IOConnectCallStructMethod:entry
/arg1==35/
{ self->defer_start=timestamp; }
pid$target:IOKit:IOConnectCallStructMethod:return
/self->defer_start/
{
    @defer_calls[(uint32_t)arg1]=count();
    @defer_us[(uint32_t)arg1]=sum((timestamp-self->defer_start)/1000);
    self->defer_start=0;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/self->attaching/
{
    this->header=(uint32_t *)copyin(arg1,16);
    surface[this->header[1]]=self->attaching;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/self->attaching && *(uint32_t *)copyin(arg1,4)==0x10002/
{
    this->header=(uint32_t *)copyin(arg1,32);
    @views[self->attaching,this->header[4],this->header[6]]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
{
    this->header=(uint32_t *)copyin(arg1,16);
    self->surface=self->attaching?self->attaching:surface[this->header[1]];
    self->op=this->header[0];self->start=timestamp;
    @calls[self->surface,self->op]=count();
    @bytes[self->surface,self->op]=sum(this->header[2]);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return
/self->start/
{
    @us[self->surface,self->op]=sum((timestamp-self->start)/1000);
    self->start=0;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/* get_integerv is slot 104 in the surveyed 10.9 GLI table. */
/* Its name and source stack distinguish adapter save/restore from app GL. */
/* arg1 points to DGPacket, whose first member is the wire message. */
/* uint64_t args[0] starts after the four fixed uint32_t header words. */
/ *(uint32_t *)copyin(arg1,4)==104 /
{
    this->message=(uint64_t *)copyin(arg1,24);
    @queries[self->surface,(uint32_t)this->message[2],ustack(12)]=count();
}
tick-12sec { exit(0); }
END {
    printf("ATTACHMENTS surface count\n");printa(@attachments);
    printf("VIEWS surface width height attachment count\n");printa(@views);
    printf("TRAFFIC surface opcode calls payload-bytes submit-wall-us\n");
    printa("surface=%x op=%x calls=%@d bytes=%@d wall_us=%@d\n",@calls,@bytes,@us);
    printf("INTEGER QUERIES surface pname stack count\n");printa(@queries);
    printf("ATTACH SERVICE CALLS name count wall-us\n");
    printa("%s calls=%@d wall_us=%@d\n",@service_calls,@service_us);
    printf("DEFER RESULTS IOReturn count wall-us\n");
    printa("error=%x calls=%@d wall_us=%@d\n",@defer_calls,@defer_us);
    printf("CPU COPY COHERENCE calls wall-us max-us\n");
    printa("calls=%@d wall_us=%@d max_us=%@d\n",@copy_calls,@copy_us,@copy_max_us);
}
