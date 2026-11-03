/* Read-only matched CoreGraphics 13F1911 x64:
 * ED5E3C37-7E2F-3885-8091-DFC9B44B38E8. Verify loaded UUID first.
 * Actual disassembly: CGXProcessHIDEvent @ 0x4f3580 receives the event in
 * RDI; event type is uint32 +8 and X/Y double bits +0x10/+0x18. It passes
 * those two doubles to CGSEventScoreboardSetCursorPosition at 0x4f35e9.
 * This function tail-calls: use ENTRY ONLY, never its pid return duration.
 * CGXGLAccelFinish has an actual ret at 0x2bb5ca (function start 0x2bb489).
 * dragWindow has a ret at 0x2e7be9; its calls to WSCurrentWindowBounds use
 * RDI for a 32-byte sret CGRect. That helper returns at 0x3a7e95. Its final
 * drag-scoped query observes actual bounds AFTER WSMoveWindowWithGroup.
 * Matched IOKit UUID 10932113-9F7E-38A0-A158-A019A555CAC3:
 * IOAccelFlushSurfaceOnFramebuffers actual ret 0xf772; SurfaceControl 0xf48e.
 * Mouse-only metadata; do not inspect keyboard, data pointers or pixels.
 * Aggregations buffer every row until END: no per-event SSH traffic or
 * passive tap is needed. The finite trace is NOT a clean pacing benchmark.
 */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=2m
BEGIN {began=timestamp;budget=512;printf("READY native HID/drag/render ordering target=%d\n",$target);}
pid$target:CoreGraphics:CGXProcessHIDEvent:entry /arg0 && budget>0/
{self->mouse_record=arg0;self->event_type=*(uint32_t *)copyin(arg0+8,4);}
pid$target:CoreGraphics:CGXProcessHIDEvent:entry
/self->mouse_record && (self->event_type==1 || self->event_type==2 || self->event_type==5 || self->event_type==6) && budget>0/
{
    this->point=(uint64_t *)copyin(self->mouse_record+0x10,16);
    @rows[(uint64_t)timestamp,1,(uint32_t)self->event_type,this->point[0],this->point[1]]=count();budget--;
}
pid$target:CoreGraphics:CGXProcessHIDEvent:entry {self->mouse_record=0;}
pid$target:CoreGraphics:dragWindow:entry /budget>0/
{self->drag_depth++;@rows[(uint64_t)timestamp,2,0,(uint64_t)0,(uint64_t)0]=count();budget--;}
pid$target:CoreGraphics:dragWindow:return /self->drag_depth/ {self->drag_depth--;}
pid$target:CoreGraphics:WSCurrentWindowBounds:entry /self->drag_depth && arg0/
{self->bounds=arg0;}
pid$target:CoreGraphics:WSCurrentWindowBounds:return /self->bounds && budget>0/
{
    this->origin=(uint64_t *)copyin(self->bounds,16);
    @rows[(uint64_t)timestamp,9,0,this->origin[0],this->origin[1]]=count();budget--;self->bounds=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /budget>0/
{@rows[(uint64_t)timestamp,3,0,(uint64_t)0,(uint64_t)0]=count();budget--;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /budget>0/
{@rows[(uint64_t)timestamp,4,0,(uint64_t)0,(uint64_t)0]=count();budget--;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /budget>0/
{@rows[(uint64_t)timestamp,5,0,(uint64_t)0,(uint64_t)0]=count();budget--;}
pid$target:CoreGraphics:CGXGLAccelFinish:return /budget>0/
{@rows[(uint64_t)timestamp,6,0,(uint64_t)0,(uint64_t)0]=count();budget--;}
/* Our own source ABI, not an Apple structure: opaque context, known-FBO bool,
 * and native/logical DRAW framebuffer. Record no context or drawable pointer. */
pid$target:GLEngine:VIMPresentDrawableKnown:entry /budget>0/
{@rows[(uint64_t)timestamp,10,(uint32_t)arg1,(uint64_t)(uint32_t)arg2,(uint64_t)0]=count();budget--;}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:entry
{self->flush_mask=(uint32_t)arg2;}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:return /budget>0/
{@rows[(uint64_t)timestamp,7,(uint32_t)arg1,(uint64_t)self->flush_mask,(uint64_t)0]=count();budget--;}
pid$target:IOKit:IOAccelSurfaceControl:entry
{self->control=(uint32_t)arg1;self->control_output=arg3;}
pid$target:IOKit:IOAccelSurfaceControl:return /self->control>=16 && self->control<=18 && budget>0/
{
    this->value=(uint32_t)arg1==0 && self->control_output?*(uint32_t *)copyin(self->control_output,4):0;
    @rows[(uint64_t)timestamp,8,(uint32_t)self->control,(uint64_t)(uint32_t)arg1,(uint64_t)this->value]=count();budget--;
}
tick-1s /timestamp-began>=14000000000/ {exit(0);}
END {
    printf("ROWS timestamp kind(1=HID,2=drag,3=update-begin,4=end,5=finish-begin,6=end,7=surface-flush,8=surface-control,9=drag-bounds,10=GLI-present-FBO) type-or-result xbits-or-mask ybits-or-value count\n");
    printa("%lu %d %u %lx %lx %@d\n",@rows);
    printf("DONE native ordering metadata budget=%d\n",budget);
}
