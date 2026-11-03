/* Finite actual WindowServer private-provider acquisition survey.
 * sudo dtrace -q -s opengl-surface-contention.d -p WINDOWSERVER_PID
 * Selector numbers are the append-only VMsvga22DContext research ABI:
 * 25 CPU try-lock, 36 metadata try-lock, 40 CPU event-wait acquisition.
 * Interpret a connection alongside its GLI caller; other connection classes
 * may reuse numbers. Only failed acquisitions print bounded stacks. No user
 * buffers, surface pixels, private object layouts or memory writes. Profiling
 * perturbs execution, so these counts/durations are NOT presented FPS.
 * Some IOKit Struct wrappers call Method internally: report them separately,
 * never add their counts or inclusive durations together.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN { budget=32;printf("READY finite surface acquisition survey target=%d\n",$target); }
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry
{ @observed[probefunc]=count(); }
pid$target:IOKit:IOConnectCallMethod:entry,
pid$target:IOKit:IOConnectCallStructMethod:entry
/arg1==25 || arg1==36 || arg1==40/
{ self->began[probefunc]=timestamp;self->connection[probefunc]=arg0;self->selector[probefunc]=arg1; }
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return
/self->began[probefunc]/
{
    @results[probefunc,self->selector[probefunc],(uint32_t)arg1]=count();
    @microseconds[probefunc,self->selector[probefunc],(uint32_t)arg1]=sum((timestamp-self->began[probefunc])/1000);
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return
/self->began[probefunc] && (uint32_t)arg1==0xe00002cc && budget>0/
{
    printf("CANNOT_LOCK function=%s connection=%x selector=%u duration_us=%lu\n",
        probefunc,self->connection[probefunc],self->selector[probefunc],
        (unsigned long)((timestamp-self->began[probefunc])/1000));
    ustack(12);budget--;
}
pid$target:IOKit:IOConnectCallMethod:return,
pid$target:IOKit:IOConnectCallStructMethod:return
/self->began[probefunc]/
{self->began[probefunc]=0;}
tick-18s {exit(0);}
END {
    printf("observed functions (reject an empty acquisition survey)\n");printa(@observed);
    printf("function selector return calls / inclusive_us; remaining detail budget=%u\n",budget);
    printa(@results);printa(@microseconds);
}
