/* Compare the real Preview private drawable contract on our GLI and native
 * Intel. Finite, scalar metadata only, no guessed Apple structure offsets. */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN {budget=256;stacks=6;origins=8;printf("READY Preview surface contract\n");}
pid$target:GLEngine:gliGetInteger*:entry
{self->getkey=arg1;self->getout=arg2;}
pid$target:GLEngine:gliGetInteger*:return
/budget>0/
{printf("GET key=%x result=%d value=%x\n",self->getkey,arg1,
 arg1==0 && self->getout?*(uint32_t*)copyin(self->getout,4):0);budget--;}
pid$target:GLEngine:gliSetInteger*:entry
/budget>0 && arg1==0x3e7/
{printf("TEXTURE_SURFACE ctx=%p record=%x,%x,%x,%u,%u\n",arg0,
 *(uint32_t*)copyin(arg2,4),*(uint32_t*)copyin(arg2+4,4),*(uint32_t*)copyin(arg2+8,4),
 *(uint32_t*)copyin(arg2+12,4),*(uint32_t*)copyin(arg2+16,4));budget--;}
pid$target:GLEngine:gliSetInteger*:entry
/arg1==0x3e7 && stacks>0/
{ustack(20);stacks--;}
pid$target:GLEngine:gliSetInteger*:entry
/arg1==0x38e && budget>0/
{self->surface=1;printf("IOSURFACE ctx=%p id=%u target=%x internal=%x size=%u,%u format=%x type=%x plane=%u\n",arg0,
 *(uint32_t*)copyin(arg2,4),*(uint32_t*)copyin(arg2+4,4),*(uint32_t*)copyin(arg2+8,4),
 *(uint32_t*)copyin(arg2+12,4),*(uint32_t*)copyin(arg2+16,4),
 *(uint32_t*)copyin(arg2+20,4),*(uint32_t*)copyin(arg2+24,4),*(uint32_t*)copyin(arg2+28,4));budget--;}
pid$target:GLEngine:gliSetInteger*:return
/self->surface/
{printf("IOSURFACE result=%d\n",arg1);self->surface=0;}
pid$target:GLEngine:gliAttachDrawableWithOptions*:entry
/budget>0 && arg1!=0/
{printf("ATTACH ctx=%p type=%x data=%p screen=%u options=%x\n",arg0,arg1,arg2,arg3,arg4);budget--;}
pid$target:AppleIntelHD5000GraphicsGLDriver:*glrIntelBeginPrimitiveBuffer:entry
{@intel_submissions=count();}
pid$target:QuartzCore:CARenderSurfaceNew:entry
/origins>0/
{printf("NEW_SURFACE id=%x size=%u,%u flags=%u,%u\n",arg0,arg1,arg2,arg3,arg4);ustack(16);origins--;}
pid$target:QuartzCore:*Render*Surface*Surface*unsigned*unsigned*unsigned*unsigned*unsigned*:entry
/origins>0/
{printf("SURFACE_CONSTRUCT %s id=%x size=%u,%u format=%x flags=%x\n",probefunc,arg1,arg2,arg3,arg4,arg5);ustack(18);origins--;}
pid$target:OpenGL:CGLSetPBuffer:return
/budget>0/
{printf("SET_PBUFFER result=%d\n",arg1);budget--;}
pid$target:IOKit:IOAccelCreateSurface:entry
/budget>0/
{self->creating=1;printf("CREATE_ACCEL_SURFACE service=%x id=%x mode=%x\n",arg0,arg1,arg2);budget--;}
pid$target:IOKit:IOServiceOpen:entry
/self->creating && budget>0/
{printf("SURFACE_OPEN service=%x task=%x type=%x\n",arg0,arg1,arg2);budget--;}
pid$target:IOKit:IOServiceOpen:return
/self->creating && budget>0/
{printf("SURFACE_OPEN result=%x\n",arg1);budget--;}
pid$target:IOKit:IOConnectCallScalarMethod:entry
/self->creating && budget>0/
{printf("SURFACE_SCALAR connection=%x selector=%u count=%u first=%llx second=%llx\n",
 arg0,arg1,arg3,arg3>0?*(uint64_t*)copyin(arg2,8):0,
 arg3>1?*(uint64_t*)copyin(arg2+8,8):0);budget--;}
pid$target:IOKit:IOConnectCallScalarMethod:return
/self->creating && budget>0/
{printf("SURFACE_SCALAR result=%x\n",arg1);budget--;}
pid$target:IOKit:IOAccelCreateSurface:return
/budget>0/
{printf("CREATE_ACCEL_SURFACE result=%x\n",arg1);self->creating=0;budget--;}
tick-10s {exit(0);}
END {printf("END remaining=%u actual Intel submits\n",budget);printa(@intel_submissions);}
