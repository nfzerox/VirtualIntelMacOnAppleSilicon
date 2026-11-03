/* Native composition-buffer selection survey. Numeric metadata only.
 * Run separately from performance tests against physical WindowServer.
 * The log is bounded to 256 entries and 15 seconds. */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { budget=256; }
pid$target:IOKit:IOAccelCreateSurface:entry
{ self->create_id=arg1;self->create_mode=arg2;self->create_result=arg3; }
pid$target:IOKit:IOAccelCreateSurface:return
/budget>0 && self->create_result/
{
    this->port=arg1==0?*(uint32_t *)copyin(self->create_result,4):0;
    printf("%lu CREATE surface=%x mode=%x connection=%x status=%x\n",(unsigned long)timestamp,
        (unsigned int)self->create_id,(unsigned int)self->create_mode,(unsigned int)this->port,(unsigned int)arg1);
    self->create_result=0;budget--;
}
pid$target:GLEngine:gliSetInteger:entry
/budget>0 && arg2/
{
    this->v=*(uint32_t *)copyin(arg2,4);
    printf("%lu SET tid=%d context=%p key=%x value=%x\n",(unsigned long)timestamp,(int)tid,(void *)arg0,(unsigned int)arg1,(unsigned int)this->v);
    budget--;
}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/budget>0 && arg2 && (arg1==90 || arg1==80)/
{
    this->a=(uint32_t *)copyin(arg2,16);
    printf("%lu ATTACH tid=%d context=%p type=%x data=%x,%x,%x,%x screen=%x options=%x\n",
        (unsigned long)timestamp,(int)tid,(void *)arg0,(unsigned int)arg1,
        (unsigned int)this->a[0],(unsigned int)this->a[1],(unsigned int)this->a[2],(unsigned int)this->a[3],(unsigned int)arg3,(unsigned int)arg4);
    budget--;
}
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/budget>0/
{
    printf("%lu SHAPE tid=%d connection=%x options=%x framebuffer=%x\n",(unsigned long)timestamp,(int)tid,(unsigned int)arg0,(unsigned int)arg2,(unsigned int)arg3);
    ustack(4);budget--;
}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:entry
/budget>0/
{
    printf("%lu FLUSH tid=%d connection=%x options=%x mask=%x\n",(unsigned long)timestamp,(int)tid,(unsigned int)arg0,(unsigned int)arg1,(unsigned int)arg2);budget--;
}
tick-15s {exit(0);}
