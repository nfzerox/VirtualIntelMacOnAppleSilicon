/* Read-only, bounded native IOAccel page-flip contract survey.
 * Run against WindowServer separately from performance measurements.
 * Records numeric control values and stacks, never surface pixels.
 */
#pragma D option quiet
#pragma D option aggsize=8m
pid$target:IOKit:IOAccelSurfaceControl:entry
{
    self->control=1; self->connection=arg0; self->selector=arg1;
    self->argument=arg2; self->output=arg3;
}
pid$target:IOKit:IOAccelSurfaceControl:return
/self->control/
{
    this->value=(arg1==0 && self->output)?
        *(uint32_t *)copyin(self->output,4):0;
    @control[self->connection,self->selector,self->argument,
        (uint32_t)arg1,this->value,ustack(5)]=count();
    self->control=0; self->output=0;
}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:entry
{ @flush[arg0,arg1,arg2,ustack(5)]=count(); }
pid$target:IOKit:IOAccelCreateSurface:entry
{ @create[arg1,arg2,ustack(5)]=count(); }
tick-15s {exit(0);}
END {
    printf("CONTROL connection selector argument status result stack count\n");
    printa(@control);
    printf("FLUSH connection options framebuffer-mask stack count\n");printa(@flush);
    printf("CREATE surface-id mode stack count\n");printa(@create);
}
