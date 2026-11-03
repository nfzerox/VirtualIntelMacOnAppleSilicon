/* Bounded Snow x64 guest attribution, not an FPS measurement. Only our
 * source-defined GLI helpers; no Mavericks-only CoreGraphics symbol names.
 * Wait for READY, then run the real Finder drag fixture for eight seconds.
 * Inclusive times overlap: do not sum attachment/import/presentation totals.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY Snow compositor costs\n");}
pid$target:GLEngine:VIMCreateAcceleratorDrawable:entry,
pid$target:GLEngine:VIMImportAcceleratorBacking:entry,
pid$target:GLEngine:VIMPresentDrawableKnown:entry
{self->start[probefunc]=timestamp;}
pid$target:GLEngine:VIMCreateAcceleratorDrawable:return,
pid$target:GLEngine:VIMImportAcceleratorBacking:return,
pid$target:GLEngine:VIMPresentDrawableKnown:return
/self->start[probefunc]/
{
    this->us=(timestamp-self->start[probefunc])/1000;
    @calls[probefunc]=count();@us[probefunc]=sum(this->us);
    @max[probefunc]=max(this->us);self->start[probefunc]=0;
}
tick-16s {exit(0);}
END {printa("%s calls=%@d wall_us=%@d max_us=%@d\n",@calls,@us,@max);}
