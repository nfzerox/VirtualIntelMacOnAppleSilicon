/* Native Intel/owned-driver comparison. Public symbol boundaries only;
 * no private structures, pixel capture, code/policy writes or GL injection.
 * Histograms include deferred/no-draw update calls. Use finish counts to
 * distinguish real rendering from those calls. Not a clean FPS benchmark.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN { began=timestamp; previous=0; printf("READY compositor cadence\n"); }
pid$target:CoreGraphics:CGXUpdateDisplay:entry { self->update=timestamp; }
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->update/
{ @updates=quantize((timestamp-self->update)/1000); self->update=0; }
pid$target:CoreGraphics:CGXGLAccelComposite:entry { self->compose=timestamp; }
pid$target:CoreGraphics:CGXGLAccelComposite:return /self->compose/
{ @composite=quantize((timestamp-self->compose)/1000); self->compose=0; }
pid$target:CoreGraphics:CGXGLAccelFinish:entry
/previous/ { @gaps=quantize((timestamp-previous)/1000); }
pid$target:CoreGraphics:CGXGLAccelFinish:entry { self->finish=timestamp; previous=timestamp; }
pid$target:CoreGraphics:CGXGLAccelFinish:return /self->finish/
{ @finish=quantize((timestamp-self->finish)/1000); self->finish=0; }
tick-1s /timestamp-began>=12000000000/ { exit(0); }
END { printa("update_us %@d\n",@updates); printa("composite_us %@d\n",@composite);
    printa("finish_us %@d\n",@finish); printa("finish_entry_gaps_us %@d\n",@gaps); }
