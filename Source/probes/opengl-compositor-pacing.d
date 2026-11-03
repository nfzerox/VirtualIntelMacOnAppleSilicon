/* Bounded Mavericks compositor scheduling survey, separate from uninstrumented
 * FPS runs. sudo dtrace -Z -q -p WINDOWSERVER_PID -s opengl-compositor-pacing.d
 * Wait for READY. On the physical reference require gpuswitch=0 AND an actual
 * Intel-driver probe hit; the main display's ancestry alone is insufficient.
 * No pixels, argument pointers or guest/driver state are modified. */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN {
    self->last[""]=0;
    printf("READY compositor cadence attached\n");
}
pid$target::CGXUpdateDisplay:entry,
pid$target::CGXGLAccelComposite:entry,
pid$target::CGXGLAccelFinish:entry,
pid$target::MPAccessSurfaceForDisplayDevice:entry,
pid$target::MPFinishSurfaceForDisplayDevice:entry,
pid$target:*Intel*:glrFlushContextToken:entry,
pid$target:*Intel*:gldFlushQueue:entry
{
    @calls[probemod,probefunc,tid]=count();
    self->begun[probefunc]=timestamp;
}
pid$target::CGXUpdateDisplay:entry,
pid$target::CGXGLAccelComposite:entry,
pid$target::MPFinishSurfaceForDisplayDevice:entry
/self->last[probefunc]/
{
    @gap_us[probefunc,tid]=quantize((timestamp-self->last[probefunc])/1000);
}
pid$target::CGXUpdateDisplay:entry,
pid$target::CGXGLAccelComposite:entry,
pid$target::MPFinishSurfaceForDisplayDevice:entry
{
    self->last[probefunc]=timestamp;
}
pid$target:*Intel*:glrFlushContextToken:entry,
pid$target:*Intel*:gldFlushQueue:entry
/!intel_seen/
{
    intel_seen=1;printf("INTEL_DRIVER_HIT %s:%s\n",probemod,probefunc);ustack(8);
}
pid$target::CGXUpdateDisplay:return,
pid$target::CGXGLAccelComposite:return,
pid$target::CGXGLAccelFinish:return,
pid$target::MPAccessSurfaceForDisplayDevice:return,
pid$target::MPFinishSurfaceForDisplayDevice:return,
pid$target:*Intel*:glrFlushContextToken:return,
pid$target:*Intel*:gldFlushQueue:return
/self->begun[probefunc]/
{
    @wall_us[probefunc,tid]=sum((timestamp-self->begun[probefunc])/1000);
    @duration_us[probefunc,tid]=quantize((timestamp-self->begun[probefunc])/1000);
    self->begun[probefunc]=0;
}
tick-12sec { exit(0); }
END {
    printf("END Intel-driver-observed=%d\nCALLS module function thread\n",(int)intel_seen);
    printa(@calls);printf("WALL microseconds (nested scopes are NOT additive)\n");printa(@wall_us);
    printf("ENTRY GAP microseconds per function/thread\n");printa(@gap_us);
    printf("DURATION microseconds per function/thread\n");printa(@duration_us);
}
