/* Bounded owned-protocol attribution, not an FPS measurement. Packet opcode
 * is the first uint32_t in OUR DGPacket, not an Apple/private object layout.
 * phase=1 is ordinary GLI Flush, phase=2 render-only Flush, phase=0 other.
 * No pixel/input reads, per-call printing, code changes or permanent tracing.
 * Pair with the real Finder drag fixture after READY. Nested wall costs must
 * not be summed as independent frame stages. */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=4m
BEGIN {printf("READY Snow synchronous-submit attribution\n");}
pid$target:GLEngine:VIMGLIFlushMode:entry {self->phase=arg2?2:1;}
pid$target:GLEngine:VIMGLIFlushMode:return {self->phase=0;}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
{self->depth++;}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /arg2/
{
    self->op[self->depth]=*(uint32_t*)copyin(arg1,4);
    self->phase_at[self->depth]=self->phase;
    self->at[self->depth]=timestamp;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return /self->at[self->depth]/
{
    this->us=(timestamp-self->at[self->depth])/1000;
    @calls[self->phase_at[self->depth],self->op[self->depth]]=count();
    @wall[self->phase_at[self->depth],self->op[self->depth]]=sum(this->us);
    @maximum[self->phase_at[self->depth],self->op[self->depth]]=max(this->us);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return /self->depth/
{self->at[self->depth]=0;self->depth--;}
tick-12s {exit(0);}
ERROR {printf("INVALID: submit probe error\n");exit(2);}
END {printa("phase=%d op=%x calls=%@d wall_us=%@d max_us=%@d\n",@calls,@wall,@maximum);}
