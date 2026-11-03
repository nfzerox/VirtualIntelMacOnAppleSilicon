/* Bounded real compositor submission attribution. DGSubmit's fixed-width
 * packet header belongs to OUR source, not an inferred Apple object layout.
 * A synchronous packet may drain earlier asynchronous commands: its inclusive
 * time is NOT the isolated opcode's cost, GPU time, or clean frame pacing.
 * Pair with actual app-owned input after READY; no pixels/payloads are read.
 * For the disassembled Leopard91 i386 client only, pass -C
 * -DVIM_DTRACE_I386_FASTCALL. Clang's local fastcall puts DGSubmit's reply
 * pointer/context in ECX/EDX, packet/sync on the stack; the pid provider's
 * arg0/arg1 are those stack words, not the source parameter positions.
 * DGConnectionTransfer similarly passes connection/data in ECX/EDX.
 * Requalify compiled calling conventions before using this on another build.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=16m
BEGIN {
    began=timestamp;self->root=0;self->root_at=(uint64_t)0;
    self->submit_at=(uint64_t)0;self->sync_us=(uint64_t)0;self->sync_calls=0;
    self->async_us=(uint64_t)0;self->async_calls=0;self->transfer_at=(uint64_t)0;
    self->transfer_us=(uint64_t)0;self->transfer_calls=0;
    printf("READY compositor synchronous boundaries target=%d\n",$target);
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{
    self->root++;
    self->root_at=self->root==1?timestamp:self->root_at;
    self->sync_us=self->root==1?0:self->sync_us;
    self->sync_calls=self->root==1?0:self->sync_calls;
    self->async_us=self->root==1?0:self->async_us;
    self->async_calls=self->root==1?0:self->async_calls;
    self->transfer_us=self->root==1?0:self->transfer_us;
    self->transfer_calls=self->root==1?0:self->transfer_calls;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
/self->root/
{
    this->header=(uint32_t *)copyin(
#ifdef VIM_DTRACE_I386_FASTCALL
        arg0,
#else
        arg1,
#endif
        16);
    self->submit_at=timestamp;self->submit_op=this->header[0];
#ifdef VIM_DTRACE_I386_FASTCALL
    self->submit_sync=arg1;
#else
    self->submit_sync=arg2;
#endif
    self->submit_bytes=this->header[2];
    @entered[self->submit_op,self->submit_sync]=count();
    @payload[self->submit_op,self->submit_sync]=sum(self->submit_bytes);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return
/self->submit_at/
{
    this->us=(timestamp-self->submit_at)/1000;
    self->sync_us+=self->submit_sync?this->us:0;self->sync_calls+=self->submit_sync!=0;
    self->async_us+=self->submit_sync?0:this->us;self->async_calls+=self->submit_sync==0;
    @returned[self->submit_op,self->submit_sync]=count();@total[self->submit_op,self->submit_sync]=sum(this->us);
    @maximum[self->submit_op,self->submit_sync]=max(this->us);
    @latency[self->submit_sync]=quantize(this->us);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return
/self->submit_at && timestamp-self->submit_at>=20000000/
{
    @slow[self->submit_op,self->submit_sync,ustack(14)]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return
/self->submit_at/
{ self->submit_at=0;self->submit_op=0;self->submit_sync=0;self->submit_bytes=0; }
pid$target:libVIMPassthroughOpenGL.dylib:DGConnectionTransfer:entry
/self->root/
{
    self->transfer_at=timestamp;
#ifdef VIM_DTRACE_I386_FASTCALL
    self->transfer_size=arg0;self->transfer_write=arg1;
#else
    self->transfer_size=arg2;self->transfer_write=arg3;
#endif
}
pid$target:libVIMPassthroughOpenGL.dylib:DGConnectionTransfer:return
/self->transfer_at/
{
    this->us=(timestamp-self->transfer_at)/1000;
    self->transfer_us+=this->us;self->transfer_calls++;
    @transfers[self->transfer_write]=count();@transfer_time[self->transfer_write]=sum(this->us);
    @transfer_max[self->transfer_write]=max(this->us);@transfer_bytes[self->transfer_write]=sum(self->transfer_size);
    self->transfer_at=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->root==1 && self->sync_calls/
{
    printf("UPDATE start_us=%d wall_us=%d synchronous_us=%d synchronous_calls=%d asynchronous_us=%d asynchronous_calls=%d transfer_us=%d transfer_calls=%d\n",
        (self->root_at-began)/1000,(timestamp-self->root_at)/1000,
        self->sync_us,self->sync_calls,self->async_us,self->async_calls,self->transfer_us,self->transfer_calls);
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->root/ {self->root--;}
tick-1s /timestamp-began>=32000000000/ {exit(0);}
ERROR {printf("INVALID compositor synchronous attribution error\n");exit(2);}
END {
    printf("SUBMIT opcode synchronous entered returned inclusive_us maximum_us payload_bytes\n");
    printa("%x %d %@d %@d %@d %@d %@d\n",@entered,@returned,@total,@maximum,@payload);
    printf("TRANSFER writing calls inclusive_us maximum_us bytes (nested in submissions)\n");
    printa("%d %@d %@d %@d %@d\n",@transfers,@transfer_time,@transfer_max,@transfer_bytes);
    printa(@latency);
    printf("Submissions >=20ms by caller (includes earlier batch execution)\n");
    printa(@slow);
}
