/* Common physical-Intel/guest attribution of the WHOLE display update.
 * Method/Struct/Scalar/Trap4/Trap6 wrappers are unioned, not added twice.
 * Phases overlap: Mirror and GL composite totals are NOT additive to I/O or
 * the root. Bounded aggregate metadata only; no private struct/pixel reads.
 * Keep separate coarse/uninstrumented pacing runs: these probes perturb time.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN {
    self->start=0;self->cpu=0;self->io_us=0;self->io_mirror_us=0;self->io_count=0;
    self->mirror_us=0;self->composite_us=0;self->finish_us=0;
    self->io_at=0;self->io_mirror=0;self->io_kind="";self->io_selector=0;
    self->update=0;self->mirror=0;self->io_depth=0;
    began=timestamp;printf("READY whole-update I/O union target=%d\n",$target);
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{
    self->update++;
    self->start=self->update==1?timestamp:self->start;self->cpu=self->update==1?vtimestamp:self->cpu;
    self->io_us=self->update==1?0:self->io_us;
    self->io_mirror_us=self->update==1?0:self->io_mirror_us;
    self->io_count=self->update==1?0:self->io_count;
    self->mirror_us=self->update==1?0:self->mirror_us;
    self->composite_us=self->update==1?0:self->composite_us;
    self->finish_us=self->update==1?0:self->finish_us;
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:entry,
pid$target:CoreGraphics:CGXGLAccelComposite:entry,
pid$target:CoreGraphics:CGXGLAccelFinish:entry
/self->update/
{
    self->depth[probefunc]++;
    self->phase_at[probefunc,self->depth[probefunc]]=timestamp;
    self->mirror+=probefunc=="CGXUpdateMagicMirror";
}
pid$target:CoreGraphics:CGXUpdateMagicMirror:return,
pid$target:CoreGraphics:CGXGLAccelComposite:return,
pid$target:CoreGraphics:CGXGLAccelFinish:return
/self->depth[probefunc]/
{
    this->us=(timestamp-self->phase_at[probefunc,self->depth[probefunc]])/1000;
    self->mirror_us+=self->depth[probefunc]==1 && probefunc=="CGXUpdateMagicMirror"?this->us:0;
    self->composite_us+=self->depth[probefunc]==1 && probefunc=="CGXGLAccelComposite"?this->us:0;
    self->finish_us+=self->depth[probefunc]==1 && probefunc=="CGXGLAccelFinish"?this->us:0;
    self->mirror-=probefunc=="CGXUpdateMagicMirror";
    self->phase_at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry,
pid$target:IOKit:IOConnectCallScalarMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry,
pid$target:IOKit:IOConnectTrap6:entry
/self->update/
{
    self->io_at=self->io_depth?self->io_at:timestamp;
    self->io_mirror=self->io_depth?self->io_mirror:self->mirror!=0;
    self->io_kind=self->io_depth?self->io_kind:probefunc;
    self->io_selector=self->io_depth?self->io_selector:(uint32_t)arg1;
    self->io_count+=self->io_depth==0;
    self->io_depth++;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->io_depth==1/
{
    this->us=(timestamp-self->io_at)/1000;
    self->io_us+=this->us;self->io_mirror_us+=self->io_mirror?this->us:0;
    @calls[self->io_kind,self->io_selector,self->io_mirror]=count();
    @us[self->io_kind,self->io_selector,self->io_mirror]=sum(this->us);
    @maximum[self->io_kind,self->io_selector,self->io_mirror]=max(this->us);
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->io_depth==1 && timestamp-self->io_at>=1000000/
{@slow[self->io_kind,self->io_selector,self->io_mirror,ustack(14)]=count();}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return,
pid$target:IOKit:IOConnectCallScalarMethod:return,
pid$target:IOKit:IOConnectTrap4:return,
pid$target:IOKit:IOConnectTrap6:return
/self->io_depth/
{
    self->io_depth--;
    self->io_at=self->io_depth?self->io_at:0;self->io_mirror=self->io_depth?self->io_mirror:0;
    self->io_kind=self->io_depth?self->io_kind:"";self->io_selector=self->io_depth?self->io_selector:0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->update==1 && timestamp-self->start>=500000/
{
    printf("UPDATE tid=%d start_ns=%llu wall_us=%d cpu_us=%d mirror_us=%d composite_us=%d finish_us=%d io_union_us=%d mirror_io_us=%d io_calls=%d\n",
        tid,(unsigned long long)self->start,(timestamp-self->start)/1000,(vtimestamp-self->cpu)/1000,
        self->mirror_us,self->composite_us,self->finish_us,self->io_us,self->io_mirror_us,self->io_count);
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->update/ {self->update--;}
tick-1s /timestamp-began>=16000000000/ {exit(0);}
END {
    printf("OUTERMOST I/O kind selector inside-Mirror calls inclusive_us max_us\n");
    printa("%s %u %u %@d %@d %@d\n",@calls,@us,@maximum);
    printf("OUTERMOST I/O >=1ms callers (not additive CPU samples)\n");printa(@slow);
}
