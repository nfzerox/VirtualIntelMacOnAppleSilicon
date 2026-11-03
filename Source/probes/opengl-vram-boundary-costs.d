/* Real executor transfer-boundary costs. Fixed per-thread fields and bounded
 * operation/function aggregates, no pixels or guest injection. Inclusive
 * nested scopes must NOT be added or used as instrumented FPS measurements.
 * sudo dtrace -q -p RENDERER_PID -s this-file; wait for READY before input.
 */
#pragma D option quiet
#pragma D option dynvarsize=32m
#pragma D option aggsize=8m
BEGIN { printf("READY VRAM ownership boundary observation\n"); }
pid$target::DGVRAMTransfer:entry
{
    self->transfer_at=timestamp;self->textured_publish=0;
}
/* Classify the executed production path, not a guessed optimized static
 * function's argument registers. No traced-process memory is dereferenced. */
pid$target::DGVRAMCopyDrawable:entry /self->transfer_at/
{ self->textured_publish=1; }
pid$target::DGVRAMBacking:entry,
pid$target::DGVRAMCopyDrawable:entry,
pid$target::DGVRAMWait:entry
/self->transfer_at/
{ self->started[probefunc]=timestamp; }
pid$target::DGVRAMBacking:return,
pid$target::DGVRAMCopyDrawable:return,
pid$target::DGVRAMWait:return
/self->transfer_at && self->started[probefunc]/
{
    this->elapsed=(timestamp-self->started[probefunc])/1000;
    @calls[self->textured_publish,probefunc]=count();@us[self->textured_publish,probefunc]=sum(this->elapsed);
    @maximum[self->textured_publish,probefunc]=max(this->elapsed);self->started[probefunc]=0;
}
pid$target::DGVRAMTransfer:return /self->transfer_at/
{
    this->elapsed=(timestamp-self->transfer_at)/1000;
    @calls[self->textured_publish,"whole transfer"]=count();@us[self->textured_publish,"whole transfer"]=sum(this->elapsed);
    @maximum[self->textured_publish,"whole transfer"]=max(this->elapsed);self->transfer_at=0;self->textured_publish=0;
}
tick-12sec { exit(0); }
END { printa("textured_publish=%d %s calls=%@d inclusive_us=%@d max_us=%@d\n",@calls,@us,@maximum); }
