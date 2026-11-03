/* Native Intel recovery oracle: scalar GLI callback contract during the
 * owned public-CGL screen-selection fixture. No Apple structure offsets,
 * state blobs, pixels, writes or per-frame production instrumentation. */
#pragma D option quiet
BEGIN {budget=128;printf("READY native virtual-screen callback trace\n");}
pid$target:OpenGL:CGLSetVirtualScreen:entry
{self->select++;printf("SCREEN requested=%d\n",(int32_t)arg1);}
pid$target:GLEngine:gliSetInteger:entry
/self->select && budget>0/
{printf("SET key=%x value=%d\n",(uint32_t)arg1,arg2?*(int32_t *)copyin(arg2,4):0);budget--;}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/self->select && budget>0/
{printf("ATTACH type=%x screen=%u options=%u\n",(uint32_t)arg1,(uint32_t)arg3,(uint32_t)arg4);budget--;}
pid$target:GLEngine:gliGetAttribute:entry,
pid$target:GLEngine:gliSetAttribute:entry
/self->select && budget>0/
{printf("ATTRIBUTE entry=%s mask=%x\n",probefunc,(uint32_t)arg1);budget--;}
pid$target:OpenGL:CGLSetVirtualScreen:return
/self->select/
{printf("SCREEN result=%d\n",(int32_t)arg1);self->select--;}
pid$target:AppleIntelHD5000GraphicsGLDriver:glrFlushContextToken:entry
{@intel_submissions=count();}
END {printf("remaining record budget=%d; actual Intel submission entries\n",budget);printa(@intel_submissions);}
