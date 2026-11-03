/* Bounded real WebKit/CA GLI publication survey. Owned diagnostic process
 * only; --wait-for-tracer delays its page load until SIGUSR1 after READY.
 * No pixel reads, private-object offsets, forced return values or GL calls. */
#pragma D option quiet
#pragma D option bufsize=4m
#pragma D option dynvarsize=4m
BEGIN { printf("READY IOSurface publication survey\n"); }
pid$target:GLEngine:VIMGLIFlushMode:entry
/events++<256/
{ printf("FLUSH ctx=%p finish=%u\n",arg0,(unsigned)arg1); }
pid$target:GLEngine:VIMFlushSurfacesForFramebuffer:entry
/events++<256/
{ printf("PUBLISH ctx=%p known=%u fbo=%u\n",arg0,(unsigned)arg1,(unsigned)arg2); }
pid$target:GLEngine:VIMBindSurfaceFramebuffer:entry
/events++<256/
{ printf("BIND ctx=%p target=%x fbo=%u\n",arg0,(unsigned)arg1,(unsigned)arg2); }
pid$target:GLEngine:VIMDispatch_framebuffer_texture2D_EXT:entry
/events++<256/
{ printf("ATTACH ctx=%p target=%x attachment=%x textarget=%x texture=%u level=%u\n",arg0,(unsigned)arg1,(unsigned)arg2,(unsigned)arg3,(unsigned)arg4,(unsigned)arg5); }
pid$target:GLEngine:gliSetInteger:entry
/arg1==0x38e && arg2 && events++<256/
{
    this->a=(uint32_t *)copyin(arg2,32);
    printf("IMPORT ctx=%p surface=%u target=%x internal=%x size=%u,%u\n",arg0,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4]);
}
tick-15sec { exit(0); }
