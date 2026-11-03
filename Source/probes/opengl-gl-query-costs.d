/* Actual public GL query arguments and inclusive wall time in WindowServer.
 * Bounded metadata aggregates only; never read caller buffers or pixels.
 * Wait for READY before moving a real Finder window. Not an FPS measurement.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
BEGIN { began=timestamp; printf("READY public GL query costs\n"); }
pid$target:libVIMPassthroughOpenGL.dylib:glGetFloatv:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glGetIntegerv:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glGetBooleanv:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glGetDoublev:entry
{ self->pname[probefunc]=arg0;self->started[probefunc]=timestamp; }
pid$target:libVIMPassthroughOpenGL.dylib:glGetFloatv:return,
pid$target:libVIMPassthroughOpenGL.dylib:glGetIntegerv:return,
pid$target:libVIMPassthroughOpenGL.dylib:glGetBooleanv:return,
pid$target:libVIMPassthroughOpenGL.dylib:glGetDoublev:return
/self->started[probefunc]/
{
    this->us=(timestamp-self->started[probefunc])/1000;
    @calls[probefunc,self->pname[probefunc]]=count();
    @us[probefunc,self->pname[probefunc]]=sum(this->us);
    @maximum[probefunc,self->pname[probefunc]]=max(this->us);
    self->started[probefunc]=0;
}
tick-1sec /timestamp-began>=8000000000/ { exit(0); }
END { printa("%s pname=0x%x calls=%@d inclusive_us=%@d max_us=%@d\n",@calls,@us,@maximum); }
