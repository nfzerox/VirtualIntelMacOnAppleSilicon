/* Finite, metadata-only native/guest AppKit surface-attachment comparison.
 * Attach to the real application, then change its Preferences toolbar tab.
 * No image data, attribute-array tail, global injection or driver hook.
 */
#pragma D option quiet
pid$target:OpenGL:CGLChoosePixelFormat:entry
{ @entries[probefunc]=count(); self->choosing=1; }
pid$target:OpenGL:CGLChoosePixelFormat:return /self->choosing/
{ printf("tid=%u CGLChoosePixelFormat result=%d\n",tid,arg1);self->choosing=0; }
pid$target:OpenGL:CGLCreateContext:entry
{ @entries[probefunc]=count();self->creating=1; }
pid$target:OpenGL:CGLCreateContext:return /self->creating/
{ printf("tid=%u CGLCreateContext result=%d\n",tid,arg1);self->creating=0; }
pid$target:OpenGL:CGLSetSurface:entry
{
    @entries[probefunc]=count();self->attaching=1;
    printf("tid=%u CGLSetSurface context=%p connection=%x window=%u surface=%x\n",tid,arg0,arg1,arg2,arg3);
}
pid$target:OpenGL:CGLSetSurface:return /self->attaching/
{ printf("tid=%u CGLSetSurface result=%d\n",tid,arg1);self->attaching=0; }
pid$target:OpenGL:CGLSetParameter:entry /arg1==0x320/
{ @entries["AppKit surface callback registration"]=count(); }
pid$target:GLEngine:gliAttachDrawableWithOptions:entry,
pid$target:GLEngine:gliAttachDrawable:entry
{
    self->gli_attaching++;
    printf("tid=%u %s context=%p type=%x record=%p screen=%u\n",
        tid,probefunc,arg0,arg1,arg2,arg3);
}
pid$target:GLEngine:gliAttachDrawableWithOptions:return,
pid$target:GLEngine:gliAttachDrawable:return /self->gli_attaching/
{
    printf("tid=%u %s result=%d\n",tid,probefunc,arg1);
    self->gli_attaching--;
}
pid$target::SLSBindSurface:entry /self->gli_attaching/
{
    self->binding=1;
    printf("tid=%u SLSBindSurface window=%u surface=%x mode=%u first=%x second=%x\n",
        tid,arg1,arg2,arg3,arg4,arg5);
}
pid$target::SLSBindSurface:return /self->binding/
{ printf("tid=%u SLSBindSurface result=%x\n",tid,arg1);self->binding=0; }
pid$target::IOConnectCallStructMethod:entry /self->gli_attaching/
{ self->io_method=arg1+1; }
pid$target::IOConnectCallStructMethod:return /self->io_method/
{
    printf("tid=%u IOConnectCallStructMethod selector=%u result=%x\n",tid,self->io_method-1,arg1);
    self->io_method=0;
}
pid$target::CGLSetCurrentContext:entry,
pid$target::CGLUpdateContext:entry,
pid$target::CGLFlushDrawable:entry
{
    @entries[probefunc]=count();
    printf("tid=%u %s:%s context=%p\n",tid,probemod,probefunc,arg0);
}
BEGIN { printf("Tracing AppKit CGL attachment for target %u\n",$target); }
tick-12s { exit(0); }
END { printa("ENTRY %s count=%@u\n",@entries); }
