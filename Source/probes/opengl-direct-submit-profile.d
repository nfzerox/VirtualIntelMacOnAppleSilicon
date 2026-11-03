/* Bounded own-client wall-time survey. Pair with an already-built real Finder
 * workload; do NOT use the instrumented run for frame-pacing qualification.
 * sudo dtrace -q -p WINDOWSERVER_PID -s opengl-direct-submit-profile.d
 * Numeric operation/pname only, no pixel/body capture. DGPacket starts with
 * the fixed DGMessage header (144 bytes in both supported guest widths).
 * Wait for READY before starting the workload; probe attachment can take
 * time and that setup must not silently shorten the measured interval. */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN
{
    began=timestamp;
    printf("READY DGSubmit observation attached\n");
}
pid$target::DGSubmit:entry
{
    this->m=(uint64_t *)copyin(arg1,24);
    self->op=this->m[0]&0xffffffff;
    self->pname=self->op==104?this->m[2]:0;
    self->bytes=(this->m[1]&0xffffffff);
    self->started=timestamp;
}
pid$target::DGSubmit:return
/self->started/
{
    @calls[self->op,self->pname]=count();
    @us[self->op,self->pname]=sum((timestamp-self->started)/1000);
    @maxus[self->op,self->pname]=max((timestamp-self->started)/1000);
    @bytes[self->op,self->pname]=sum(self->bytes);
    self->started=0;
}
tick-1sec /timestamp-began>=12000000000/
{
    exit(0);
}
END
{
    printa("op=%d pname=%x calls=%@d total_us=%@d max_us=%@d bytes=%@d\n",@calls,@us,@maxus,@bytes);
}
