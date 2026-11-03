/* Common Mavericks compositor timeline; no object/pixel addresses. Use with
 * two owned real windows and finite clicks. Actual Intel driver hits are a
 * required oracle check, not inferred from renderer enumeration/pmset.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN { printf("READY native shadow timeline\n"); }
pid$target:CoreGraphics:CGXOrderWindow:entry,
pid$target:CoreGraphics:*CGXSetWindowShadowParametersWithStretch*:entry,
pid$target:CoreGraphics:CGXWindowShadowInvalidate:entry,
pid$target:CoreGraphics:CGXGLCreateShadowTexture:entry,
pid$target:CoreGraphics:fill_shadow_contents:entry
{ printf("EVENT %llu %llu %s entry\n",(unsigned long long)timestamp,(unsigned long long)walltimestamp,probefunc); }
pid$target:CoreGraphics:CGXOrderWindow:return,
pid$target:CoreGraphics:*CGXSetWindowShadowParametersWithStretch*:return,
pid$target:CoreGraphics:CGXWindowShadowInvalidate:return,
pid$target:CoreGraphics:CGXGLCreateShadowTexture:return,
pid$target:CoreGraphics:fill_shadow_contents:return
{ printf("EVENT %llu %llu %s return\n",(unsigned long long)timestamp,(unsigned long long)walltimestamp,probefunc); }
pid$target:*Intel*:glrFlushContextToken:entry
{ @intel[probemod]=count(); }
tick-12sec { exit(0); }
END { printf("ACTUAL INTEL DRIVER HITS\n");printa(@intel); }
