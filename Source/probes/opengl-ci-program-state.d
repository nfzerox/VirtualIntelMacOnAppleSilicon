/* Finite WindowServer filter comparison. Record assembly-program state as
 * exact IEEE bits, not guessed floating-point register arguments. Native
 * context-first prototypes match the target SDK GLIFunctionDispatch table.
 * Program text and numeric parameters only: no texture or desktop pixels.
 */
#pragma D option quiet
#pragma D option bufsize=8m
#pragma D option aggsize=4m
BEGIN { printf("READY compositor program state\n"); }
pid$target:GLEngine:*glProgramLocalParameter4fvARB_Exec*:entry,
pid$target:GLEngine:*glProgramEnvParameter4fvARB_Exec*:entry
/arg3/
{
    this->p=(uint32_t *)copyin(arg3,16);
    @params[probefunc,(uint32_t)arg1,(uint32_t)arg2,this->p[0],this->p[1],this->p[2],this->p[3]]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:glProgramLocalParameter4fvARB:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glProgramEnvParameter4fvARB:entry
/arg2/
{
    this->p=(uint32_t *)copyin(arg2,16);
    @params[probefunc,(uint32_t)arg0,(uint32_t)arg1,this->p[0],this->p[1],this->p[2],this->p[3]]=count();
}
pid$target:GLEngine:*glProgramStringARB_Exec*:entry
/arg3>0 && arg3<16384 && arg4/
{ printf("NATIVE PROGRAM target=%x length=%d\n%s\n",arg1,arg3,stringof(copyin(arg4,arg3))); }
pid$target:libVIMPassthroughOpenGL.dylib:glProgramStringARB:entry
/arg2>0 && arg2<16384 && arg3/
{ printf("BRIDGE PROGRAM target=%x length=%d\n%s\n",arg0,arg2,stringof(copyin(arg3,arg2))); }
pid$target:libCGXCoreImage.A.dylib:*cgxcoreimage_filter_apply*:entry,
pid$target:libCGXCoreImage.A.dylib:*prepare_image*:entry
{ @apply[ustack(8)]=count(); }
tick-18sec { exit(0); }
END { printf("PARAMETERS IEEE bits\n");printa(@params);printf("APPLY\n");printa(@apply); }
