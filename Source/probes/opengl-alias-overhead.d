/* Bounded executor-only metadata survey; no pixels, state mutation or guest
 * injection. Instrumentation perturbs timing: counts/caller costs are not FPS.
 * sudo dtrace -q -p RENDERER_PID -s this-file
 */
#pragma D option quiet
#pragma D option dynvarsize=8m
BEGIN { printf("READY alias overhead, 12 seconds\n"); }
pid$target::DGPrepareDrawableAliases:entry
{ self->alias=timestamp;@alias_calls=count(); }
pid$target::glGetIntegerv:entry /self->alias/
{ @alias_integer_queries[arg0]=count(); }
pid$target::glActiveTexture:entry /self->alias/
{ @alias_active_texture=count(); }
pid$target::glBindTexture:entry /self->alias/
{ @alias_bind_texture=count(); }
pid$target::DGPrepareDrawableAliases:return /self->alias/
{ @alias_microseconds=sum((timestamp-self->alias)/1000);self->alias=0; }
tick-12sec { exit(0); }
END {
    printf("alias calls\n");printa("%@d\n",@alias_calls);
    printf("integer queries by enum\n");printa("%x %@d\n",@alias_integer_queries);
    printf("active texture calls\n");printa("%@d\n",@alias_active_texture);
    printf("texture binds\n");printa("%@d\n",@alias_bind_texture);
    printf("instrumented inclusive microseconds\n");printa("%@d\n",@alias_microseconds);
}
