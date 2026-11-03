/* Actual IOKit/GLI transaction order for an effect, not an FPS test. Public
 * region layout is from the matching 10.9 SDK; no kernel/object offsets.
 * A caller must verify native Intel selection separately. Finite 12-second
 * survey, at most 2048 metadata records, never image bytes or memory writes. */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { budget=2048;printf("READY effect publication pid=%d\n",$target); }
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/budget>0 && arg1/
{
    this->count=*(uint32_t *)copyin(arg1,4);this->b=(int16_t *)copyin(arg1+4,8);
    printf("%lu SHAPE tid=%d connection=%x options=%x framebuffer=%u count=%u bounds=%d,%d,%d,%d\n",
        (unsigned long)timestamp,(int)tid,(uint32_t)arg0,(uint32_t)arg2,(uint32_t)arg3,this->count,
        this->b[0],this->b[1],this->b[2],this->b[3]);budget--;
}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/budget>0 && arg1==0x5a && arg2/
{
    this->a=(uint32_t *)copyin(arg2,12);
    printf("%lu ATTACH tid=%d context=%p surface=%x\n",(unsigned long)timestamp,(int)tid,(void *)arg0,this->a[2]);budget--;
}
pid$target:GLEngine:VIMGLIFlus?:entry,
pid$target:GLEngine:VIMGLIFinis?:entry
/budget>0/
{ printf("%lu GL_SUBMIT tid=%d context=%p entry=%s\n",(unsigned long)timestamp,(int)tid,(void *)arg0,probefunc);budget--; }
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:entry
/budget>0/
{ self->present_connection=arg0;self->present_start=timestamp;
  printf("%lu PRESENT tid=%d connection=%x options=%x mask=%x\n",(unsigned long)timestamp,(int)tid,(uint32_t)arg0,(uint32_t)arg1,(uint32_t)arg2);budget--; }
/* Matching 13F1911 x64 IOKit at 0xf704 has a real call to
 * IOConnectCallMethod followed by a ret, not a tail exit. */
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:return
/self->present_start && budget>0/
{
    printf("%lu PRESENT_RESULT tid=%d connection=%x result=%x elapsed-ns=%lu\n",
        (unsigned long)timestamp,(int)tid,(uint32_t)self->present_connection,(uint32_t)arg1,
        (unsigned long)(timestamp-self->present_start));budget--;
}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:return
/self->present_start/
{
    @present_results[(uint32_t)arg1]=count();
    self->present_connection=0;self->present_start=0;
}
pid$target:AppleIntelHD5000GraphicsGLDriver*:glrFlushContextToken:entry
{ @intel_submits=count(); }
tick-12s { exit(0); }
END { printf("remaining record budget=%d\n",budget);printf("present results\n");printa(@present_results);
    printf("native Intel context submits\n");printa(@intel_submits); }
