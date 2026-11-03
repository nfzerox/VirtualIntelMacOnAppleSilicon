/* Bounded, metadata-only native Intel / direct-driver comparison. Attach to
 * WindowServer, drive the SAME real transition workload after READY, and
 * require calls in the relevant buckets before interpreting empty results.
 * Wall times are inclusive and MUST NOT be summed across nested functions or
 * called FPS. No Apple structure layouts, pixels, writes or policy changes.
 * -Z permits the alternative driver-specific probes to be absent.
 */
#pragma D option quiet
#pragma D option aggsize=8m
#pragma D option dynvarsize=4m
BEGIN {began=timestamp;printf("READY animation transaction wall times\n");}
/* Entry-only helpers: optimized tail-return probes do not necessarily cover
 * their final callee. Only the two disassembled real-return functions below
 * are timed, to avoid reporting pre-callee returns as full wall duration. */
pid$target:CoreGraphics:CGXUpdateDisplaySynchronizeSurface:entry,
pid$target:CoreGraphics:CGXSetWindowTransformList:entry,
pid$target:CoreGraphics:CGXGLAccelFinish:entry,
pid$target:GLEngine:*VIMGLIFlushMode*:entry,
pid$target:GLEngine:*VIMPresentDrawableKnown*:entry,
pid$target:libVIMPassthroughOpenGL*:*DGReadDrawable*:entry,
pid$target:AppleIntelHD5000GraphicsGLDriver:*glrFlushContextToken*:entry
{@entries[probefunc,(timestamp-began)/25000000]=count();}
pid$target:CoreGraphics:CGXUpdateDisplay:entry,
pid$target:CoreGraphics:CGXReenableUpdateForConnection:entry
{self->depth[probefunc]++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry,
pid$target:CoreGraphics:CGXReenableUpdateForConnection:entry
/self->depth[probefunc]==1/
{self->start[probefunc]=timestamp;@entries[probefunc,(timestamp-began)/25000000]=count();}
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXReenableUpdateForConnection:return
/self->depth[probefunc]==1 && self->start[probefunc]/
{this->us=(timestamp-self->start[probefunc])/1000;
 @count[probefunc]=count();@inclusive_us[probefunc]=sum(this->us);
 @max_us[probefunc]=max(this->us);@wall_us[probefunc]=quantize(this->us);
 self->start[probefunc]=0;}
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXReenableUpdateForConnection:return
/self->depth[probefunc]/
{self->depth[probefunc]--;}
tick-12s {exit(0);}
END {printf("completed outermost calls\n");printa(@count);
 printf("inclusive wall microseconds (nested, not additive)\n");printa(@inclusive_us);
 printf("maximum call microseconds\n");printa(@max_us);
 printf("wall microsecond histograms\n");printa(@wall_us);
 printf("entries in 25-ms buckets (not presented FPS)\n");printa(@entries);}
