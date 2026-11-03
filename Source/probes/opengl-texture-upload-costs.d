/* Metadata-only, finite upload survey. Run against the real guest GL client,
 * wait for READY, then exercise an empty Finder content-view selection. Times
 * are inclusive wall times, not CPU costs. No pixels or addresses are logged.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN { printf("READY texture upload costs\n"); }
pid$target:libVIMPassthroughOpenGL.dylib:glPixelStorei:entry
{ @pixel_store[arg0,arg1]=count(); }
pid$target:libVIMPassthroughOpenGL.dylib:glTexSubImage2D:entry
{ self->upload_at=timestamp;self->width=arg4;self->height=arg5; }
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /self->upload_at/
{ @span[self->width,self->height,*(uint32_t *)copyin(arg1+8,4)]=count(); }
pid$target:libVIMPassthroughOpenGL.dylib:DGConnectionTransfer:entry /self->upload_at/
{ @transfer[arg2,arg3]=count(); }
pid$target:libVIMPassthroughOpenGL.dylib:glTexSubImage2D:return /self->upload_at/
{
    @calls=count();@us=sum((timestamp-self->upload_at)/1000);
    @max_us=max((timestamp-self->upload_at)/1000);
    self->upload_at=0;
}
tick-12sec { exit(0); }
END {
    printf("UPLOAD calls wall-us max-us\n");printa("%@d %@d %@d\n",@calls,@us,@max_us);
    printf("SPAN width height bytes calls\n");printa("%d %d %d %@d\n",@span);
    printf("TRANSFER bytes writing calls\n");printa("%d %d %@d\n",@transfer);
    printf("PIXEL STORE enum value calls\n");printa("%x %d %@d\n",@pixel_store);
}
