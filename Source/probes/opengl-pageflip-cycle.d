/* Read-only 13F1911 x86_64 CoreGraphics buffer-family contract.
 * UUID ED5E3C37-7E2F-3885-8091-DFC9B44B38E8. Device index/count offsets
 * derive from MPDisplayDeviceDidFlush assembly, NOT a driver layout guess.
 * At most 512 metadata records and 15 seconds. No pixels, memory writes or
 * performance claims. Verify the actual WindowServer uses Intel separately.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { budget=512; printf("READY native page-flip family cycle\n"); }
pid$target:CoreGraphics:MPAccessSurfaceForDisplayDevice:entry
/budget>0/
{
    this->index=*(uint32_t *)copyin(arg0+0x4c,4);
    printf("%lu ACCESS_REQUEST tid=%d companion=%d index=%u\n",(unsigned long)timestamp,
        (int)tid,(int)(arg3 & 0xff),(unsigned int)this->index);budget--;
}
pid$target:CoreGraphics:MPDisplayDeviceDidFlush:entry
/budget>0/
{ self->flushed_device=arg0;self->before_index=*(uint32_t *)copyin(arg0+0x4c,4); }
pid$target:CoreGraphics:MPDisplayDeviceDidFlush:return
/budget>0 && self->flushed_device/
{
    this->index=*(uint32_t *)copyin(self->flushed_device+0x4c,4);
    this->count=*(uint32_t *)copyin(self->flushed_device+0x48,4);
    printf("%lu DID_FLUSH tid=%d before=%u after=%u count=%u\n",(unsigned long)timestamp,
        (int)tid,(unsigned int)self->before_index,(unsigned int)this->index,(unsigned int)this->count);
    budget--;self->flushed_device=0;
}
pid$target:IOKit:IOAccelSurfaceControl:entry
/budget>0 && arg1==18/
{ self->control_port=arg0;self->control_output=arg3; }
pid$target:IOKit:IOAccelSurfaceControl:return
/budget>0 && self->control_output/
{
    this->value=arg1==0?*(uint32_t *)copyin(self->control_output,4):0;
    printf("%lu CONTROL18 tid=%d connection=%x status=%x value=%x\n",(unsigned long)timestamp,
        (int)tid,(unsigned int)self->control_port,(unsigned int)arg1,(unsigned int)this->value);
    budget--;self->control_output=0;
}
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/budget>0 && arg2==0x1000/
{
    printf("%lu COPY_FORWARD tid=%d connection=%x framebuffer=%x\n",(unsigned long)timestamp,
        (int)tid,(unsigned int)arg0,(unsigned int)arg3);budget--;
}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:entry
/budget>0/
{
    printf("%lu FLUSH tid=%d connection=%x options=%x mask=%x\n",(unsigned long)timestamp,
        (int)tid,(unsigned int)arg0,(unsigned int)arg1,(unsigned int)arg2);budget--;
}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/budget>0 && arg2 && (arg1==90 || arg1==80)/
{
    this->a=(uint32_t *)copyin(arg2,16);
    printf("%lu ATTACH tid=%d context=%p type=%x data=%x,%x,%x,%x\n",(unsigned long)timestamp,
        (int)tid,(void *)arg0,(unsigned int)arg1,(unsigned int)this->a[0],
        (unsigned int)this->a[1],(unsigned int)this->a[2],(unsigned int)this->a[3]);budget--;
}
/* Diagnose the actual WindowServer driver rather than assuming it from
 * display ownership or an unrelated test app. Reject a non-Intel reference.
 * Full entry instrumentation is intentionally for contract survey ONLY. */
pid$target:*GLDriver:*:entry
{ @driver[probemod]=count(); }
pid$target:*GLDriver:*:entry
/probemod=="GeForceGLDriver"/
{ @other_driver[probefunc]=count(); }
pid$target:AppleIntelHD5000GraphicsGLDriver:glrBindContextDrawFramebuffer:entry
{ @intel_draw_context[arg0,ustack(5)]=count(); }
tick-15s {exit(0);}
END {
    printf("ACTUAL_DRIVER_CALLS (require Intel, not GeForce)\n");printa(@driver);
    printf("OTHER_DRIVER_FUNCTIONS (separate capability queries from drawing)\n");printa(@other_driver);
    printf("INTEL_DRAW_CONTEXTS\n");printa(@intel_draw_context);
}
