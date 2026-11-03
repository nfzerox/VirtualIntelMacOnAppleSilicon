/* Deliberately coarse native/guest comparison: only two compositor function
 * pairs. Use separately from per-draw/packet probes to avoid mistaking their
 * instrumentation overhead for a clean frame budget. Updates/Mirrors are
 * NOT displayed frames. No OS-private layouts or pixel capture. start_us is
 * relative to this finite probe; legacy start_ns may truncate on old DTrace.
 * Entry gaps include idle and are not animation FPS. Finish-entry counts
 * identify actual rendering updates without calling them completed GPU work.
 * Guest vtimestamp is not host CPU execution time during emulated I/O.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
BEGIN {began=timestamp;self->previous[""]=(uint64_t)0;self->n["CGXUpdateDisplay"]=0;
    printf("READY coarse compositor intervals target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
/self->n["CGXUpdateDisplay"]==0/ {self->finishes=0;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry
/self->n["CGXUpdateDisplay"]/ {self->finishes++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry,
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry
{
    self->n[probefunc]++;self->wall[probefunc,self->n[probefunc]]=timestamp;
    self->running[probefunc,self->n[probefunc]]=vtimestamp;
    self->gap[probefunc,self->n[probefunc]]=self->previous[probefunc]?timestamp-self->previous[probefunc]:0;
    self->previous[probefunc]=timestamp;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXUpdateMagicMirror:return
/self->n[probefunc]/
{
    printf("INTERVAL function=%s tid=%d start_ns=%d wall_us=%d cpu_us=%d start_us=%d entry_gap_us=%d finish_entries=%u\n",
        probefunc,tid,self->wall[probefunc,self->n[probefunc]],
        (timestamp-self->wall[probefunc,self->n[probefunc]])/1000,
        (vtimestamp-self->running[probefunc,self->n[probefunc]])/1000,
        (self->wall[probefunc,self->n[probefunc]]-began)/1000,
        self->gap[probefunc,self->n[probefunc]]/1000,self->finishes);
    self->wall[probefunc,self->n[probefunc]]=0;self->n[probefunc]--;
}
tick-1s /timestamp-began>=24000000000/ {exit(0);}
END {printf("END coarse compositor intervals\n");}
