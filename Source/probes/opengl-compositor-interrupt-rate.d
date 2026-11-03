/* Finite IRQ-storm check during REAL compositor transitions. The studied
 * xnu-2422 IOInterruptEventSource::registerInterruptHandler chooses normal
 * versus disableInterruptOccurred from the provider's actual interrupt type.
 * Count those real filter entries, rather than assuming PCI delivery is level
 * or assigning a sampled post-OUT instruction's entire delay to native GL.
 * No private object reads, guessed offsets, kernel writes or pixel collection.
 * Counts/phase times include instrumentation and are NOT animation FPS.
 */
#pragma D option quiet
#pragma D option aggsize=8m
#pragma D option dynvarsize=4m
BEGIN {began=timestamp;printf("READY compositor actual interrupt rate target=%d\n",$target);}
fbt:mach_kernel:*IOFilterInterruptEventSource*normalInterruptOccurred*:entry,
fbt:mach_kernel:*IOFilterInterruptEventSource*disableInterruptOccurred*:entry
{
    @interrupts[probefunc,(uint64_t)arg0,cpu]=count();
    @rate[(timestamp-began)/1000000000,probefunc]=count();
}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->depth==1/
{self->root=++roots;self->at=timestamp;self->finishes=0;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->root/ {self->finishes++;}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth==1 && self->root/
{
    @updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,self->finishes]=count();
    self->root=0;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->depth/ {self->depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID compositor interrupt-rate probe error\n");exit(2);}
END {
    printf("FILTER_ENTRIES actual_function event_source cpu count\n");printa("%s %lu %d %@d\n",@interrupts);
    printf("FILTER_RATE second actual_function count\n");printa("%u %s %@d\n",@rate);
    printf("UPDATES start_us root wall_us finishes count\n");printa("%lu %u %lu %u %@d\n",@updates);
    printf("END compositor actual interrupt rate roots=%u\n",roots);
}
