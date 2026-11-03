/* Read-only Snow native CGLS/GLI pbuffer contract. Use -Z -p WindowServerPID.
 * At most 256 metadata records/10s; Snow dtrace has no -W name waiter.
 * Pixel-format56 is the actual x64 period record, not a pixel dump. */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN {budget=256;printf("READY Snow CI pbuffer contract\n");}
pid$target:GLEngine:gliCreateContext:entry /budget>0/
{
    budget--;self->creating=1;
    printf("CREATE format=%x share=%x flags=%x\n",arg1,arg2,arg5);
    tracemem(copyin(arg1,56),56);
}
pid$target:GLEngine:gliCreateContext:return /self->creating/
{printf("CREATE_RESULT=%x\n",arg1);self->creating=0;}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry,
pid$target:GLEngine:gliAttachDrawable:entry /budget>0/
{
    budget--;printf("ATTACH %s type=%x data=%x screen=%x options=%x\n",probefunc,arg1,arg2,arg3,arg4);
}
pid$target:CoreGraphics:cglsAttachPBuffer:entry /budget>0/
{
    budget--;self->pbuffer=1;
    printf("CGS_PBUFFER ctx=%x pb=%x face=%x level=%x screen=%x format=%x\n",arg0,arg1,arg2,arg3,arg4,arg5);
    tracemem(copyin(arg1,48),48);
}
pid$target:CoreGraphics:cglsAttachPBuffer:return /self->pbuffer/
{printf("CGS_PBUFFER_RESULT=%x\n",arg1);self->pbuffer=0;}
pid$target:CoreGraphics:cglsChoosePixelFormat:entry /budget>0/
{budget--;printf("CHOOSE\n");tracemem(copyin(arg1,64),64);}
pid$target:CoreGraphics:cglsCreateContext:return /budget>0 && arg1/
{budget--;printf("CGS_CREATE_ERROR=%x\n",arg1);ustack(8);}
pid$target:IOKit:IOAccelCreateSurface:entry /budget>0/
{budget--;self->surface=1;printf("SURFACE id=%x mode=%x\n",arg1,arg2);}
pid$target:IOKit:IOAccelCreateSurface:return /self->surface/
{printf("SURFACE_RESULT=%x\n",arg1);self->surface=0;}
tick-10s {exit(0);}
