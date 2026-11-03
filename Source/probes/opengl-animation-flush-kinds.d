/* Source-owned flush/finish flag versus the physical Intel engine's actual
 * execution entries. Entry counts only: no return/tail-call timing or pixels.
 * Native GLEngine x64 30643C97-7EBB-36EE-BEEA-13844EA0F680;
 * owned GLEngine B36ADC06-D019-340C-9919-3D23FABD80F3.
 * Only calls inside CGXUpdateDisplay contribute. The binaries share matched
 * 13F1911 CoreGraphics; do not equate either driver function's numeric ABI.
 */
#pragma D option quiet
#pragma D option aggsize=8m
BEGIN {printf("READY animation flush kinds target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->display_depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->display_depth/ {self->display_depth--;}
/* A leading wildcard permits -Z to leave the other engine's optional
 * entries unmatched, instead of failing an exact missing-symbol lookup. */
pid$target:GLEngine:*glFlush_Exec:entry,
pid$target:GLEngine:*glFinish_Exec:entry
/self->display_depth/
{@native[probefunc]=count();@native_callers[probefunc,ustack(7)]=count();}
pid$target:GLEngine:*VIMGLIFlushMode:entry
/self->display_depth/
{@owned[(uint32_t)arg1]=count();@owned_callers[(uint32_t)arg1,ustack(7)]=count();}
pid$target:*Intel*:glrFlushContextToken:entry
/self->display_depth/ {@intel[probemod]=count();}
tick-30s {exit(0);}
END {
    printf("native execution function calls\n");printa(@native);
    printf("owned source finish flag calls\n");printa(@owned);
    printf("actual Intel driver entries\n");printa(@intel);
    printf("native callers\n");printa(@native_callers);
    printf("owned callers\n");printa(@owned_callers);
}
