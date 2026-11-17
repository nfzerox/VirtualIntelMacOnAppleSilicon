/* Finite guest-only activation attribution. No argument/pixel/file contents,
 * preferences, injected functions or code replacements. Native pid probes
 * perturb execution; this is NOT an unprofiled frame-rate benchmark.
 * vtimestamp is GUEST-accounted thread running time, not host CPU/GPU time.
 * Nested functions overlap: never add their inclusive durations together.
 * Deliberately omit DGSubmit/per-vertex probes: their tens of thousands of
 * traps amplify the very foreground pause being investigated.
 * dtrace -q -p WINDOWSERVER_PID -s THIS_FILE
 */
#pragma D option quiet
#pragma D option aggsize=4m
#pragma D option dynvarsize=4m
BEGIN { began=timestamp; slow=0; printf("READY bounded launch-stage survey target=%d\n",$target); }
pid$target::CGXUpdateMagicMenuBar:entry,
pid$target::CGXAddWindowCIFilter:entry,
pid$target:GLEngine:VIMImportAcceleratorBacking:entry,
pid$target:GLEngine:gliSetInteger:entry,
pid$target:GLEngine:VIMPresentFlushedDrawable:entry,
pid$target:GLEngine:VIMImportCGSTexture:entry,
pid$target:IOKit:IOServiceOpen:entry,
pid$target:IOKit:IOServiceClose:entry
{
    self->depth[probefunc]++;
}
pid$target::CGXUpdateMagicMenuBar:entry,
pid$target::CGXAddWindowCIFilter:entry,
pid$target:GLEngine:VIMImportAcceleratorBacking:entry,
pid$target:GLEngine:gliSetInteger:entry,
pid$target:GLEngine:VIMPresentFlushedDrawable:entry,
pid$target:GLEngine:VIMImportCGSTexture:entry,
pid$target:IOKit:IOServiceOpen:entry,
pid$target:IOKit:IOServiceClose:entry
/self->depth[probefunc]==1/
{
    self->wall[probefunc]=timestamp;self->running[probefunc]=vtimestamp;
}
pid$target::CGXUpdateMagicMenuBar:return,
pid$target::CGXAddWindowCIFilter:return,
pid$target:GLEngine:VIMImportAcceleratorBacking:return,
pid$target:GLEngine:gliSetInteger:return,
pid$target:GLEngine:VIMPresentFlushedDrawable:return,
pid$target:GLEngine:VIMImportCGSTexture:return,
pid$target:IOKit:IOServiceOpen:return,
pid$target:IOKit:IOServiceClose:return
/self->depth[probefunc]==1 && self->wall[probefunc]/
{
    this->wall=(timestamp-self->wall[probefunc])/1000;
    this->running=(vtimestamp-self->running[probefunc])/1000;
    @calls[probefunc]=count();@wall_us[probefunc]=sum(this->wall);
    @running_us[probefunc]=sum(this->running);@maximum_us[probefunc]=max(this->wall);
    @distribution[probefunc]=quantize(this->wall);
    this->record_slow=this->wall>20000 && slow<64;
}
pid$target::CGXUpdateMagicMenuBar:return,
pid$target::CGXAddWindowCIFilter:return,
pid$target:GLEngine:VIMImportAcceleratorBacking:return,
pid$target:GLEngine:gliSetInteger:return,
pid$target:GLEngine:VIMPresentFlushedDrawable:return,
pid$target:GLEngine:VIMImportCGSTexture:return,
pid$target:IOKit:IOServiceOpen:return,
pid$target:IOKit:IOServiceClose:return
/self->depth[probefunc]==1 && self->wall[probefunc] && this->record_slow/
{
    ++slow;
    printf("LONG function=%s tid=%d began_ms=%d wall_us=%d guest_running_us=%d\n",
        probefunc,tid,(self->wall[probefunc]-began)/1000000,this->wall,this->running);
}
pid$target::CGXUpdateMagicMenuBar:return,
pid$target::CGXAddWindowCIFilter:return,
pid$target:GLEngine:VIMImportAcceleratorBacking:return,
pid$target:GLEngine:gliSetInteger:return,
pid$target:GLEngine:VIMPresentFlushedDrawable:return,
pid$target:GLEngine:VIMImportCGSTexture:return,
pid$target:IOKit:IOServiceOpen:return,
pid$target:IOKit:IOServiceClose:return
/self->depth[probefunc]/
{
    self->depth[probefunc]--;
}
/* Lowest public struct/scalar method boundary (their wrappers overlap this).
 * Only method numbers and timing, never arguments or pointed-to contents.
 */
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectTrap4:entry
/self->depth["CGXUpdateMagicMenuBar"]/
{
    self->io_at[probefunc]=timestamp;self->selector[probefunc]=(uint32_t)arg1;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectTrap4:return
/self->io_at[probefunc]/
{
    @io_calls[probefunc,self->selector[probefunc]]=count();
    @io_us[probefunc,self->selector[probefunc]]=sum((timestamp-self->io_at[probefunc])/1000);
    @io_max_us[probefunc,self->selector[probefunc]]=max((timestamp-self->io_at[probefunc])/1000);
    self->io_at[probefunc]=0;
}
tick-1s /timestamp-began>=12000000000/ { exit(0); }
END {
    printf("END function calls inclusive_wall_us guest_running_us max_wall_us\n");
    printa("%s %@d %@d %@d %@d\n",@calls,@wall_us,@running_us,@maximum_us);
    printa(@distribution);
    printf("NESTED menu-bar I/O function selector calls inclusive_us max_us\n");
    printa("%s %u %@d %@d %@d\n",@io_calls,@io_us,@io_max_us);
}
