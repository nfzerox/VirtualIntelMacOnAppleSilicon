/* Matched Mavericks x64 CoreGraphics only:
 * ED5E3C37-7E2F-3885-8091-DFC9B44B38E8. The real returns were inspected:
 * menu-for-space 3aedfb, ensure 3a8ed9, rebuild 3a9560,
 * shadow-get 3a0ddb, plus the previously qualified display/transform returns.
 * No guessed private arguments, pixels, inferior calls or policy writes.
 * Finite BEGIN-anchored observation; output is buffered until END. Rows
 * from separate CPUs must be sorted by start timestamp, never print order.
 * Inclusive/nested timings are not additive and do not establish FPS or
 * when menu pixels became visible. Pair with actual desktop selection video.
 */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=4m
BEGIN { began=timestamp; budget=512; printf("READY bounded space/menu timing\n"); }
pid$target:CoreGraphics:CGXPackagesUpdateMenuBarForSpace:entry,
pid$target:CoreGraphics:ensure_space_menu_bar_wids:entry,
pid$target:CoreGraphics:rebuild_menu_bars_passing_test:entry,
pid$target:CoreGraphics:CGXWindowShadowGet:entry,
pid$target:CoreGraphics:CGXUpdateDisplay:entry,
pid$target:CoreGraphics:CGXSetWindowTransformList:entry
{ self->depth[probefunc]++; }
pid$target:CoreGraphics:CGXPackagesUpdateMenuBarForSpace:entry,
pid$target:CoreGraphics:ensure_space_menu_bar_wids:entry,
pid$target:CoreGraphics:rebuild_menu_bars_passing_test:entry,
pid$target:CoreGraphics:CGXWindowShadowGet:entry,
pid$target:CoreGraphics:CGXUpdateDisplay:entry,
pid$target:CoreGraphics:CGXSetWindowTransformList:entry
/self->depth[probefunc]==1/
{ self->start[probefunc]=timestamp; }
pid$target:CoreGraphics:CGXPackagesUpdateMenuBarForSpace:return,
pid$target:CoreGraphics:ensure_space_menu_bar_wids:return,
pid$target:CoreGraphics:rebuild_menu_bars_passing_test:return,
pid$target:CoreGraphics:CGXWindowShadowGet:return,
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXSetWindowTransformList:return
/self->depth[probefunc]==1 && self->start[probefunc]/
{
    this->elapsed=(timestamp-self->start[probefunc])/1000;
    @calls[probefunc]=count(); @us[probefunc]=sum(this->elapsed);
    @maximum[probefunc]=max(this->elapsed);
}
pid$target:CoreGraphics:CGXPackagesUpdateMenuBarForSpace:return,
pid$target:CoreGraphics:ensure_space_menu_bar_wids:return,
pid$target:CoreGraphics:rebuild_menu_bars_passing_test:return,
pid$target:CoreGraphics:CGXWindowShadowGet:return,
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXSetWindowTransformList:return
/self->depth[probefunc]==1 && self->start[probefunc] && budget>0 &&
 ((probefunc=="CGXWindowShadowGet" && this->elapsed>=1000) ||
  (probefunc=="CGXUpdateDisplay" && this->elapsed>=8000) ||
  (probefunc!="CGXWindowShadowGet" && probefunc!="CGXUpdateDisplay"))/
{
    @rows[(uint64_t)self->start[probefunc],(uint64_t)this->elapsed,probefunc]=count();
    budget--;
}
pid$target:CoreGraphics:CGXPackagesUpdateMenuBarForSpace:return,
pid$target:CoreGraphics:ensure_space_menu_bar_wids:return,
pid$target:CoreGraphics:rebuild_menu_bars_passing_test:return,
pid$target:CoreGraphics:CGXWindowShadowGet:return,
pid$target:CoreGraphics:CGXUpdateDisplay:return,
pid$target:CoreGraphics:CGXSetWindowTransformList:return
/self->depth[probefunc]/
{ self->depth[probefunc]--; }
tick-1s /timestamp-began>=30000000000/ { exit(0); }
END {
    printf("ROWS start_ns inclusive_us function count\n");
    printa("%lu %lu %s %@d\n",@rows);
    printf("TOTAL function calls inclusive_us max_us (NOT additive)\n");
    printa("%s %@d %@d %@d\n",@calls,@us,@maximum);
    printf("DONE budget=%d\n",budget);
}
