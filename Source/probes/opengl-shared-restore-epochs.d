/* Bounded metadata-only survey of OUR shared-drawable handoffs, x86_64.
 * VIMSurfaceVRAM is the fixed 64-byte public provider ABI: four uint32_t,
 * offset/length uint64_t, then VIMSurfaceState at +32 (see vim_framebuffer.h).
 * No pixels, caller CPU addresses, Apple structure offsets or writes.
 *
 * Public wrappers may tail-call DGVRAMTransaction; time the latter's real
 * entry/return, not a wrapper return. The wrapper entry identifies the action.
 * Wait for READY before real Finder input. Low probe count intentionally
 * excludes ordinary GL/DGSubmit calls. Inclusive times are NOT displayed FPS.
 * Repeat means the immediately preceding restore on this same thread had
 * identical object/incarnation/epoch/view, not a global destination cache.
 * Equal epochs alone DO NOT authorize skipping a restore: another drawable,
 * context, view or unflushed GPU write may have changed the destination.
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=4m
self uint64_t prior_object;
self uint64_t prior_incarnation;
self uint64_t prior_epoch;
self int32_t prior_x;
self int32_t prior_y;
self int32_t prior_w;
self int32_t prior_h;
self int32_t prior_flip;
BEGIN {began=timestamp;budget=64;printf("READY shared restore epochs target=%d\n",$target);}
pid$target:libVIMPassthroughOpenGL.dylib:DGImportRAMDrawable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGImportVRAMDrawable:entry
{self->action=1;}
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishVRAMDrawable:entry
{self->action=2;}
pid$target:libVIMPassthroughOpenGL.dylib:DGImportSharedDrawableRects:entry
{self->action=3;}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:entry
/self->action && arg0/
{
    this->backing=(uint64_t *)copyin(arg0,64);
    self->object=this->backing[5];self->incarnation=this->backing[6];self->epoch=this->backing[7];
    self->x=(int32_t)arg1;self->y=(int32_t)arg2;
    self->w=(int32_t)arg3;self->h=(int32_t)arg4;self->flip=(int32_t)arg5;
    self->repeat=self->action==1 && self->prior_object==self->object &&
        self->prior_incarnation==self->incarnation && self->prior_epoch==self->epoch &&
        self->prior_x==self->x && self->prior_y==self->y &&
        self->prior_w==self->w && self->prior_h==self->h && self->prior_flip==self->flip;
    self->at=timestamp;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:return
/self->at/
{
    this->us=(timestamp-self->at)/1000;
    @calls[self->action,self->repeat,(int32_t)arg1]=count();
    @us[self->action,self->repeat,(int32_t)arg1]=sum(this->us);
    @maximum[self->action,self->repeat,(int32_t)arg1]=max(this->us);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:return
/self->at && budget>0/
{
    printf("HANDOFF at=%lu tid=%d action=%u object=%lx incarnation=%lu epoch=%lu view=%d,%d,%d,%d flip=%d repeated=%d result=%d us=%lu\n",
        (unsigned long)self->at,(int)tid,self->action,(unsigned long)self->object,(unsigned long)self->incarnation,(unsigned long)self->epoch,self->x,self->y,self->w,self->h,
        self->flip,self->repeat,(int32_t)arg1,(unsigned long)((timestamp-self->at)/1000));budget--;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:return
/self->at && self->action==1/
{
    self->prior_object=self->object;self->prior_incarnation=self->incarnation;self->prior_epoch=self->epoch;
    self->prior_x=self->x;self->prior_y=self->y;self->prior_w=self->w;self->prior_h=self->h;self->prior_flip=self->flip;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:return
{self->at=0;self->action=0;}
profile:::tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {
    printf("action (1 restore / 2 publish / 3 patch), repeated same-view epoch, result, calls, inclusive_us, max_us\n");
    printa("%u %u %d %@d %@d %@d\n",@calls,@us,@maximum);
    printf("Remaining detail budget=%d\n",budget);
}
