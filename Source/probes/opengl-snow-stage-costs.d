/* Finite Snow CoreGraphics stage survey, matched to actual10J3250/10K549
 * symbols. Only named function boundaries; no Apple object layouts, pixels,
 * instruction patches, per-call printing or permanent sampling. Stage times
 * are inclusive/nested and NOT additive or a displayed-frame count. Generic
 * function-return probes can miss tail exits: compare counts before relying
 * on any stage, and use the actual period disassembly to qualify a boundary.
 * Start real Finder input only after READY, in the same orchestration cell.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=4m
BEGIN {printf("READY Snow compositor named-stage costs\n");}
pid$target:CoreGraphics:CGXAccessDisplayDeviceSurface:entry,
pid$target:CoreGraphics:CGXReleaseDisplayDeviceSurface:entry,
pid$target:CoreGraphics:CGXGLAccelAccess:entry,
pid$target:CoreGraphics:CGXGLAccelFinish:entry,
pid$target:CoreGraphics:CGXGLAccelComposite:entry,
pid$target:CoreGraphics:CGLayerComposite:entry,
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:entry,
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry,
pid$target:CoreGraphics:CGXHWCopyRegion:entry,
pid$target:CoreGraphics:CGXSynchronizeDisplayDrivers:entry
{
    self->depth[probefunc]++;
    @entries[probefunc]=count();
    self->at[probefunc,self->depth[probefunc]]=timestamp;
}
pid$target:CoreGraphics:CGXAccessDisplayDeviceSurface:return,
pid$target:CoreGraphics:CGXReleaseDisplayDeviceSurface:return,
pid$target:CoreGraphics:CGXGLAccelAccess:return,
pid$target:CoreGraphics:CGXGLAccelFinish:return,
pid$target:CoreGraphics:CGXGLAccelComposite:return,
pid$target:CoreGraphics:CGLayerComposite:return,
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:return,
pid$target:CoreGraphics:CGXUpdateMagicMirror:return,
pid$target:CoreGraphics:CGXHWCopyRegion:return,
pid$target:CoreGraphics:CGXSynchronizeDisplayDrivers:return
/self->depth[probefunc]/
{
    this->ns=timestamp-self->at[probefunc,self->depth[probefunc]];
    @returns[probefunc]=count();@wall[probefunc]=sum(this->ns);
    @maximum[probefunc]=max(this->ns);@cost[probefunc]=quantize(this->ns/1000);
    self->at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
tick-12s {exit(0);}
ERROR {printf("INVALID: named-stage probe error\n");exit(2);}
END {
    printa("ENTRY %s count=%@d\n",@entries);
    printa("RETURN %s count=%@d wall_ns=%@d max_ns=%@d\n",@returns,@wall,@maximum);
    printa(@cost);
}
