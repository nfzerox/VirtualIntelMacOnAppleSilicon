/* Same bounded public-region/driver-call survey on native Intel and guest.
 * WSWindowSetLevel in the exact 13F1911 CoreGraphics (UUID
 * ED5E3C37-7E2F-3885-8091-DFC9B44B38E8) stores EDX as the requested level,
 * RSI as the window object, calls CGXUpdateDisplay, then executes a real ret.
 * No guessed object fields, pixel reads, writes or timing/FPS claims.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN {budget=512;printf("READY window-level composition contract\n");}
pid$target:CoreGraphics:WSWindowSetLevel:entry
/budget>0/
{
    self->level=1;
    self->first_level=1;
    printf("%lu LEVEL_BEGIN tid=%d window=%p level=%d\n",(unsigned long)timestamp,(int)tid,(void *)arg1,(int32_t)arg2);
    budget--;
}
pid$target:CoreGraphics:WSWindowSetLevel:return
/self->level/
{printf("%lu LEVEL_END tid=%d result=%x\n",(unsigned long)timestamp,(int)tid,(uint32_t)arg1);self->level=0;}
pid$target:CoreGraphics:CGSWindowROStateGetLevel:return
/self->level && self->first_level && budget>0/
{printf("LEVEL_OLD value=%d\n",(int32_t)arg1);self->first_level=0;budget--;}
pid$target:CoreGraphics:CGSWindowROStateIsOrderedIn:return
/self->level && budget>0/
{printf("LEVEL_ORDERED value=%d\n",(int)(arg1&255));budget--;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
/self->level && budget>0/
{printf("%lu LEVEL_UPDATE_ENTER\n",(unsigned long)timestamp);budget--;}
pid$target:CoreGraphics:begin_deferred_update_cycle:entry
/self->level/
{@defer_entries=count();}
/* Both helpers have actual rets in the matching disassembly. Their bool
 * result is AL; upper return-register bits are not part of that result. */
pid$target:CoreGraphics:begin_deferred_update_cycle:return
/self->level/
{@defer_results[arg1&255]=count();}
pid$target:CoreGraphics:WSDisplayUpdatesAreDisabled:return
/self->level/
{@disabled[arg1&255]=count();}
pid$target:CoreGraphics:WSGetDebugOptions:return
/self->level/
{@debug_options[(uint32_t)arg1]=count();}
/* MPGetNextWakeupTime's matching disassembly loads device+0x158, then
 * description+0x278, then the public StdFBShmem vblTime/vblDelta at 0x40/48.
 * Read only this verified MP branch; VFB uses a different object layout. */
pid$target:CoreGraphics:MPGetNextWakeupTime:entry
/self->level && arg0 && budget>0/
{
    this->description=*(uint64_t *)copyin(arg0+0x158,8);
    this->shared=*(uint64_t *)copyin(this->description+0x278,8);
    this->clock=(uint64_t *)copyin(this->shared+0x40,16);
    printf("WAKEUP_CLOCK selector=%d vbl-time=%lu vbl-delta=%lu\n",(int32_t)arg1,
        (unsigned long)this->clock[0],(unsigned long)this->clock[1]);budget--;
}
pid$target:CoreGraphics:VFBGetNextWakeupTime:entry
/self->level/
{@vfb_wakeups=count();}
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/self->level && arg1 && budget>0/
{
    this->count=*(uint32_t *)copyin(arg1,4);this->r=(int16_t *)copyin(arg1+4,8);
    printf("%lu LEVEL_SHAPE connection=%x options=%x mask=%x count=%u bounds=%d,%d,%d,%d\n",
        (unsigned long)timestamp,(uint32_t)arg0,(uint32_t)arg2,(uint32_t)arg3,this->count,
        this->r[0],this->r[1],this->r[2],this->r[3]);budget--;
}
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/self->level && arg1 && budget>0 && *(uint32_t *)copyin(arg1,4)>0 && *(uint32_t *)copyin(arg1,4)<=3/
{
    this->r=(int16_t *)copyin(arg1+12,8);
    printf("LEVEL_RECT index=0 rect=%d,%d,%d,%d\n",this->r[0],this->r[1],this->r[2],this->r[3]);
}
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/self->level && arg1 && budget>0 && *(uint32_t *)copyin(arg1,4)>=2 && *(uint32_t *)copyin(arg1,4)<=3/
{
    this->r=(int16_t *)copyin(arg1+20,8);
    printf("LEVEL_RECT index=1 rect=%d,%d,%d,%d\n",this->r[0],this->r[1],this->r[2],this->r[3]);
}
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/self->level && arg1 && budget>0 && *(uint32_t *)copyin(arg1,4)==3/
{
    this->r=(int16_t *)copyin(arg1+28,8);
    printf("LEVEL_RECT index=2 rect=%d,%d,%d,%d\n",this->r[0],this->r[1],this->r[2],this->r[3]);
}
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/self->level && arg1==0x5a && arg2 && budget>0/
{
    this->a=(uint32_t *)copyin(arg2,12);
    printf("%lu LEVEL_ATTACH context=%p surface=%x\n",(unsigned long)timestamp,(void *)arg0,this->a[2]);budget--;
}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:entry
/self->level && budget>0/
{
    printf("%lu LEVEL_PRESENT connection=%x options=%x mask=%x\n",(unsigned long)timestamp,
        (uint32_t)arg0,(uint32_t)arg1,(uint32_t)arg2);ustack(9);budget--;
}
pid$target:AppleIntelHD5000GraphicsGLDriver:glrFlushContextToken:entry
{ @intel_submits[self->level]=count(); }
tick-12s {exit(0);}
END {printf("remaining metadata budget=%d\n",budget);
    printf("deferred-cycle entries\n");printa(@defer_entries);
    printf("deferred-cycle actual bool returns\n");printa(@defer_results);
    printf("display-updates-disabled actual bool returns\n");printa(@disabled);
    printf("debug option masks\n");printa(@debug_options);
    printf("alternate VFB wakeup path\n");printa(@vfb_wakeups);
    printf("actual Intel submits (0=outside, 1=inside window-level transaction)\n");printa(@intel_submits);}
