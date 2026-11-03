/* Actual Preview texture allocation/import evidence. Bounded scalar records,
 * no bitmap reads, injection, or GL calls from an observer thread. */
#pragma D option quiet
#pragma D option bufsize=8m
BEGIN {budget=2048;stacks=12;printf("READY Preview texture lifetime\n");}
pid$target:GLEngine:gliSetInteger*:entry
{self->key=arg1;}
pid$target:GLEngine:gliSetInteger*:return
/arg1!=0 && budget>0/
{printf("PRIVATE_ERROR key=%x result=%d\n",self->key,arg1);budget--;}
pid$target:GLEngine:gliSetInteger*:entry
/budget>0 && (arg1==0x3e5 || arg1==0x3e6 || arg1==0x3e7)/
{printf("SURFACE key=%x ctx=%p record=%x,%x,%x,%u,%u\n",arg1,arg0,
 *(uint32_t*)copyin(arg2,4),*(uint32_t*)copyin(arg2+4,4),*(uint32_t*)copyin(arg2+8,4),
 *(uint32_t*)copyin(arg2+12,4),*(uint32_t*)copyin(arg2+16,4));budget--;}
pid$target:GLEngine:CGLTexImageIOSurface2D*:entry
/budget>0/
{printf("IOSURFACE ctx=%p target=%x internal=%x size=%u,%u format=%x\n",arg0,arg1,arg2,arg3,arg4,arg5);budget--;}
pid$target:GLEngine:CGLTexImageIOSurface2D*:return
/budget>0/
{printf("IOSURFACE result=%d\n",arg1);budget--;}
pid$target:GLEngine:VIMDispatch_bind_texture*:entry,
pid$target:GLEngine:VIMDispatch_tex_image2D*:entry,
pid$target:GLEngine:VIMDispatch_copy_tex_image2D*:entry,
pid$target:GLEngine:VIMDispatch_tex_sub_image2D*:entry
/budget>0/
{printf("TEXTURE %s ctx=%p a=%llx b=%llx c=%llx d=%llx e=%llx\n",probefunc,arg0,arg1,arg2,arg3,arg4,arg5);budget--;}
pid$target:GLEngine:VIMDispatch_delete_textures*:entry
/budget>0 && arg1>0 && arg1<=16/
{printf("DELETE ctx=%p count=%u names=%x\n",arg0,arg1,*(uint32_t*)copyin(arg2,4));budget--;}
pid$target:GLEngine:gliSetInteger*:return
/self->key==0x3e7 && stacks>0/
{ustack(12);stacks--;}
pid$target:AppleIntelHD5000GraphicsGLDriver:*glrIntelBeginPrimitiveBuffer:entry
{@intel_submissions=count();}
tick-12s {exit(0);}
END {printf("END remaining=%u actual Intel submissions\n",budget);printa(@intel_submissions);}
