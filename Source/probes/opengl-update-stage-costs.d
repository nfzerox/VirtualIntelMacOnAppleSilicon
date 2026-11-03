/* Low-frequency, bounded update attribution, not a clean FPS measurement.
 * Mavericks CoreGraphics x64 ED5E3C37-7E2F-3885-8091-DFC9B44B38E8;
 * CGXUpdateDisplay and CGXGLAccelFinish have verified real returns.
 * Own import/publish wrappers may tail-call: time DGVRAMTransaction, not
 * wrapper returns. No pixels or private Apple argument layouts are read.
 * All observations are buffered until END, with a finite BEGIN deadline.
 * Guest vtimestamp is NOT host on-CPU time: a synchronous emulated-device
 * wait can elapse while this vCPU is outside the guest kernel scheduler.
 * Do not interpret a wall/vtimestamp match as proof of guest CPU computation.
 */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=4m
BEGIN {began=timestamp;budget=384;printf("READY bounded update stage costs target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{
    self->at=timestamp;self->cpu_at=vtimestamp;self->finish_us=0;self->finishes=0;
    self->restore_us=0;self->restores=0;self->publish_us=0;self->publishes=0;
}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->depth/
{self->finish_at=timestamp;self->finishes++;}
pid$target:CoreGraphics:CGXGLAccelFinish:return /self->depth && self->finish_at/
{self->finish_us+=(timestamp-self->finish_at)/1000;self->finish_at=0;}
pid$target:libVIMPassthroughOpenGL.dylib:DGImportRAMDrawable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGImportVRAMDrawable:entry /self->depth/ {self->action=1;}
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishVRAMDrawable:entry /self->depth/ {self->action=2;}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:entry /self->depth && self->action/
{self->vram_at=timestamp;}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:return /self->depth && self->vram_at && self->action==1/
{self->restore_us+=(timestamp-self->vram_at)/1000;self->restores++;}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:return /self->depth && self->vram_at && self->action==2/
{self->publish_us+=(timestamp-self->vram_at)/1000;self->publishes++;}
pid$target:libVIMPassthroughOpenGL.dylib:DGVRAMTransaction:return {self->vram_at=0;self->action=0;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->at/
{
    this->us=(timestamp-self->at)/1000;
    @wall_us=quantize(this->us);@calls=count();@total_us=sum(this->us);
    @finish_us=sum(self->finish_us);@restore_us=sum(self->restore_us);@publish_us=sum(self->publish_us);
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->at && budget>0/
{
    @rows[(uint64_t)self->at,(uint64_t)((timestamp-self->at)/1000),(uint64_t)((vtimestamp-self->cpu_at)/1000),
        (uint64_t)self->finish_us,(uint32_t)self->finishes,
        (uint64_t)self->restore_us,(uint32_t)self->restores,
        (uint64_t)self->publish_us,(uint32_t)self->publishes]=count();budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {
    printf("ROWS start_ns wall_us guest_vtime_us finish_us finishes restore_us restores publish_us publishes count\n");
    printa("%lu %lu %lu %lu %u %lu %u %lu %u %@d\n",@rows);
    printf("WALL distribution\n");printa(@wall_us);
    printf("TOTAL calls wall_us finish_us restore_us publish_us (nested, NOT additive)\n");
    printa("%@d %@d %@d %@d %@d\n",@calls,@total_us,@finish_us,@restore_us,@publish_us);
    printf("DONE update costs budget=%d\n",budget);
}
