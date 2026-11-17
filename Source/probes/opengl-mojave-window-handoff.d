/* Finite physical-Mojave Intel surface handoff survey for the owned Retina
 * NSOpenGLView fixture. The fixture's GL_RENDERER and mux state must identify
 * Intel; a requested pmset policy or empty DTrace output is not evidence.
 * glReadPixels in the fixture checks a few pixels: do not count those reads
 * as a native compositor's full-frame readback or use this as an FPS test. */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN { budget=32;printf("READY Mojave window handoff pid=%u\n",$target); }
pid$target:GLEngine:gliAttachDrawableWithOptions*:entry /budget>0/
{ printf("ATTACH type=%x screen=%u options=%x\n",arg1,arg3,arg4);budget--; }
pid$target:SkyLight:SLSBindSurface:entry /budget>0/
{ printf("BIND connection=%x window=%x mode=%x flags=%x accelerator=%x\n",arg0,arg1,arg3,arg4,arg5);budget--; }
pid$target:SkyLight:SLSFlushSurfaceWithOptionsAndIndex:entry /budget>0/
{ printf("FLUSH window=%x surface=%x flags=%x index=%u\n",arg1,arg2,arg4,arg5);budget--; }
pid$target:IOSurface:IOSurfaceLock:entry
{ @surface_locks[arg1]=count(); }
pid$target:IOKit:IOConnectCallStructMethod:entry
{ @struct_requests[arg1,arg3]=count(); }
pid$target:IOKit:IOConnectCallScalarMethod:entry
{ @scalar_requests[arg1,arg3]=count(); }
pid$target:AppleIntelHD5000GraphicsGLDriver:*glrIntelBeginPrimitiveBuffer*:entry
{ @intel_submissions=count(); }
tick-6s { exit(0); }
END {
  printf("Intel submissions\n");printa(@intel_submissions);
  printf("IOSurface lock options\n");printa(@surface_locks);
  printf("IOKit struct selector/input bytes\n");printa(@struct_requests);
  printf("IOKit scalar selector/input count\n");printa(@scalar_requests);
}
