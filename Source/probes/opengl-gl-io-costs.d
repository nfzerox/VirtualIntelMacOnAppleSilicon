/* Fixed per-thread nesting, no ustack/string keys or pixel capture. Bounded
 * inclusive wall times: method scopes include renderer/GPU waits, not CPU
 * execution alone. Pair with real input only after READY; reject drops.
 */
#pragma D option quiet
#pragma D option dynvarsize=32m
#pragma D option aggsize=8m
BEGIN { printf("READY GL IOKit boundary observation\n"); }
pid$target::IOServiceOpen:entry { self->open_at=timestamp; }
pid$target::IOServiceOpen:return /self->open_at/
{ @opens=count();@open_us=sum((timestamp-self->open_at)/1000);self->open_at=0; }
pid$target::IOServiceClose:entry { self->close_at=timestamp; }
pid$target::IOServiceClose:return /self->close_at/
{ @closes=count();@close_us=sum((timestamp-self->close_at)/1000);self->close_at=0; }
pid$target::IOConnectCallMethod:entry
{ self->method=arg1;self->method_at=timestamp; }
pid$target::IOConnectCallMethod:return /self->method_at/
{
    @calls[self->method]=count();@us[self->method]=sum((timestamp-self->method_at)/1000);
    @maximum[self->method]=max((timestamp-self->method_at)/1000);self->method_at=0;
}
tick-12sec { exit(0); }
END {
    printf("open count/us\n");printa("%@d %@d\n",@opens,@open_us);
    printf("close count/us\n");printa("%@d %@d\n",@closes,@close_us);
    printf("method calls/inclusive-us/max-us\n");printa("%d %@d %@d %@d\n",@calls,@us,@maximum);
}
