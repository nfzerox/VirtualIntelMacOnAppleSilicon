/* Bounded metadata only. CGXUpdateDisplay's real return was checked in the
 * matched 13F1911 disassembly. Count boundary entries INSIDE each outermost
 * display update, not all-process idle time or nested-inclusive sums. Native
 * Intel remains the oracle: this does not count its private GPU submission
 * traps as VIM exchanges and must not be used as a native/guest speed ratio.
 * Diagnostic instrumentation can alter cadence; these are NOT FPS results.
 */
#pragma D option quiet
#pragma D option bufsize=4m
self uint64_t last_begin;
BEGIN {budget=256;printf("READY display-update bridge boundary counts\n");}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry
/self->depth==1/
{
    self->gap=self->last_begin?(timestamp-self->last_begin)/1000:0;
    self->last_begin=timestamp;self->began=timestamp;
    self->traps=0;self->methods=0;self->errors=0;
}
pid$target:IOKit:IOConnectTrap4:entry
/self->depth/
{self->traps++;}
pid$target:IOKit:IOConnectCallStructMethod:entry
/self->depth/
{self->methods++;}
pid$target:libVIMPassthroughOpenGL.dylib:glGetError:entry
/self->depth/
{self->errors++;}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->depth==1 && self->began && (self->traps || self->methods) && budget>0/
{
    printf("%lu UPDATE tid=%d wall_us=%lu update_gap_us=%lu trap4=%u struct_methods=%u public_error_queries=%u\n",
        (unsigned long)timestamp,(int)tid,(unsigned long)((timestamp-self->began)/1000),
        (unsigned long)self->gap,
        self->traps,self->methods,self->errors);budget--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return
/self->depth/
{self->depth--;}
tick-30s {exit(0);}
END {printf("remaining record budget=%d\n",budget);}
