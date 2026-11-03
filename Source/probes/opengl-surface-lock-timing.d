/* Bounded native/guest WindowServer IOKit boundary survey.
 * sudo dtrace -q -s opengl-surface-lock-timing.d -p WINDOWSERVER_PID
 * Aggregate only connection, selector, scalar count, duration and call stacks.
 * No pixel contents, user buffers or pointers are read. A selector alone does
 * not identify a surface method: interpret alongside its native caller stack.
 * Run separately from frame-pacing measurements: pid probes perturb timing.
 */
#pragma D option quiet
#pragma D option aggsize=8m

BEGIN { printf("BEGIN wall-ns=%lld\n",walltimestamp); }

pid$target:IOKit:IOConnectCallMethod:entry
{
    self->started=timestamp;
    self->connection=arg0;
    self->selector=arg1;
    self->scalars=arg3;
    @calls[arg0,arg1,arg3,ustack(5)]=count();
}

pid$target:IOKit:IOConnectCallMethod:return
/self->started/
{
    @microseconds[self->connection,self->selector,self->scalars]=sum((timestamp-self->started)/1000);
    @returns[self->connection,self->selector,arg1]=count();
    self->started=0;
}

tick-12s
{
    exit(0);
}

END
{
    printf("END wall-ns=%lld\nCALLS: connection selector scalar-count stack count\n",walltimestamp);
    printa(@calls);
    printf("TIME: connection selector scalar-count total-microseconds\n");
    printa(@microseconds);
    printf("RETURNS: connection selector status count\n");
    printa(@returns);
}
