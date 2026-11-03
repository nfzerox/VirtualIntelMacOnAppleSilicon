/* Physical 13F1911 Intel reference, entry counts only (no tail-return timing).
 * Match native GLEngine x64 30643C97-7EBB-36EE-BEEA-13844EA0F680 and
 * AppleIntelHD5000GraphicsGLDriver 828EA701-E30F-353B-A731-A9A2D2DB9C29.
 * The GLI execution entry takes context before ordinary GL arguments.
 * No client pointers/pixels or names inspected. Counts are not presented FPS.
 * Wildcard optional native Exec names allow -Z to omit them when pairing this
 * survey with a direct-driver guest trace; zero native Exec hits there are
 * not evidence of no direct-driver/internal readbacks.
 */
#pragma D option quiet
#pragma D option aggsize=8m
BEGIN {printf("READY physical Intel animation/readback survey target=%d\n",$target);}
pid$target:*Intel*:glrFlushContextToken:entry
{@intel[probemod]=count();}
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:entry,
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry,
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{@work[probefunc]=count();}
pid$target:GLEngine:*glReadPixels_Exec:entry
{
    @reads[(int32_t)arg3,(int32_t)arg4]=count();
    @readers[ustack(9)]=count();
}
pid$target:GLEngine:*glGetTexImage_Exec:entry
{@downloads[ustack(9)]=count();}
tick-30s {exit(0);}
END {
    printf("actual Intel-driver entries\n");printa(@intel);
    printf("native compositor work\n");printa(@work);
    printf("glReadPixels width height calls\n");printa("%d %d %@d\n",@reads);
    printf("read callers\n");printa(@readers);
    printf("glGetTexImage callers\n");printa(@downloads);
}
