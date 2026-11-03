/* Matched 13F1911 IOKit x64 UUID 10932113-9F7E-38A0-A158-A019A555CAC3.
 * Instruction-checked real returns: IOConnectCallMethod 0x52e3 and Trap4
 * 0x56bd; Trap0..5 wrappers all have actual call/ret structure. Trap6 is a
 * tail jump and is excluded: its pid return probe can fire before the callee.
 * Only calls inside an outermost CGXUpdateDisplay (real return verified in
 * matching CoreGraphics) contribute. Inclusive times are NOT additive across
 * callers or a presented FPS/native speed ratio. Selector numbers belong to
 * each actual connection class: never equate native and VIM method numbers.
 * No caller buffers, object layouts, pixels or writes; finite observation.
 */
#pragma D option quiet
#pragma D option aggsize=16m
#pragma D option dynvarsize=8m
BEGIN {detail_budget=128;printf("READY display-update IOKit cost survey target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{self->display_depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
/self->display_depth==1/
{
    self->display_began=timestamp;self->trap_ns=0;self->method_ns=0;
    self->stream_ns=0;self->prepared_ns=0;self->trap_calls=0;self->method_calls=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->display_depth==1 && self->display_began && timestamp-self->display_began>=16000000 && detail_budget>0/
{
    printf("UPDATE_DETAIL wall_us=%lu trap_us=%lu method_us=%lu stream28_us=%lu prepared31_us=%lu traps=%u methods=%u\n",
        (unsigned long)((timestamp-self->display_began)/1000),
        (unsigned long)(self->trap_ns/1000),(unsigned long)(self->method_ns/1000),
        (unsigned long)(self->stream_ns/1000),(unsigned long)(self->prepared_ns/1000),
        self->trap_calls,self->method_calls);detail_budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->display_depth/
{self->display_depth--;}
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectTrap?:entry
/self->display_depth && probefunc!="IOConnectTrap6"/
{self->begin[probefunc]=timestamp;self->selector[probefunc]=(uint32_t)arg1;}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectTrap?:return
/self->begin[probefunc]/
{
    this->us=(timestamp-self->begin[probefunc])/1000;
    @calls[probefunc,self->selector[probefunc]]=count();
    @sum[probefunc,self->selector[probefunc]]=sum(this->us);
    @max[probefunc,self->selector[probefunc]]=max(this->us);
    @results[probefunc,self->selector[probefunc],(uint32_t)arg1]=count();
    self->trap_ns+=probefunc=="IOConnectCallMethod"?0:timestamp-self->begin[probefunc];
    self->method_ns+=probefunc=="IOConnectCallMethod"?timestamp-self->begin[probefunc]:0;
    self->trap_calls+=probefunc=="IOConnectCallMethod"?0:1;
    self->method_calls+=probefunc=="IOConnectCallMethod"?1:0;
    self->stream_ns+=probefunc=="IOConnectCallMethod" && self->selector[probefunc]==28?timestamp-self->begin[probefunc]:0;
    self->prepared_ns+=probefunc=="IOConnectCallMethod" && self->selector[probefunc]==31?timestamp-self->begin[probefunc]:0;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectTrap?:return
/self->begin[probefunc] && timestamp-self->begin[probefunc]>=1000000/
{@slow[probefunc,self->selector[probefunc],ustack(12)]=count();}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectTrap?:return
/self->begin[probefunc]/
{self->begin[probefunc]=0;}
tick-30s {exit(0);}
END {
    printf("function selector calls inclusive_us max_us\n");
    printa("%s %u %@d %@d %@d\n",@calls,@sum,@max);
    printf("native return codes\n");printa(@results);
    printf("callers for at-least-1-ms inclusive observations\n");printa(@slow);
}
