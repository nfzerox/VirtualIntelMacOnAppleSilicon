/* Finite owned-driver surface-upload survey, x86_64 guest WindowServer.
 * Only our source-defined mapping/state ABI is decoded. No Apple private
 * structures, pixel contents, pointers, keys or authentication data are logged.
 * Wait for READY, then run real Finder dragging. Instrumented durations are
 * attribution evidence, not an uninstrumented frame-rate benchmark.
 * sudo dtrace -p WINDOWSERVER_PID -s THIS_FILE
 */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=4m
BEGIN { began=timestamp; last[0]=0; printf("READY owned CGS upload survey\n"); }
pid$target:GLEngine:VIMUpdateCGSLocked:entry
{
    this->layout=(uint32_t *)copyin(arg2+16,24);
    this->state=(uint64_t *)copyin(arg3,32);
    this->object=this->state[1];
    @bindings[this->layout[1],this->layout[2],(uint32_t)(this->state[0]>>32)]=count();
    @epochs[last[this->object]==this->state[3]?"same":"changed"]=count();
    last[this->object]=this->state[3];
}
pid$target:libVIMPassthroughOpenGL.dylib:DGUploadSurface:entry
{
    self->upload=timestamp;self->width=(uint32_t)arg2;self->height=(uint32_t)arg3;
    self->row=(uint32_t)arg4;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGUploadSurface:return /self->upload/
{
    @uploads[self->width,self->height]=count();
    @us[self->width,self->height]=sum((timestamp-self->upload)/1000);
    @maxus[self->width,self->height]=max((timestamp-self->upload)/1000);
    @bytes[self->width,self->height]=sum(self->row*self->height);
    self->upload=0;
}
pid$target:GLEngine:VIMRetainCPUPublishedDrawable:entry
{
    this->drawing=(uint32_t *)copyin(arg1+20,8);
    this->orientation=*(uint32_t *)copyin(arg1+108,4);
    this->view=(uint32_t *)copyin(arg1+120,12);
    this->layout=(uint32_t *)copyin(arg2,24);
    @publications[this->drawing[0],this->drawing[1],this->layout[1],this->layout[2],
        this->orientation,this->view[0],this->view[1],this->view[2]]=count();
}
tick-1s /timestamp-began>=14000000000/ { exit(0); }
END {
    printa("BIND width=%u height=%u flags=%x count=%@d\n",@bindings);
    printa("EPOCH %s count=%@d\n",@epochs);
    printa("PUBLISH view=%ux%u backing=%ux%u unflipped=%u region=%u origin=%d,%d count=%@d\n",@publications);
    printa("UPLOAD width=%u height=%u calls=%@d total_us=%@d max_us=%@d bytes=%@d\n",
        @uploads,@us,@maxus,@bytes);
}
