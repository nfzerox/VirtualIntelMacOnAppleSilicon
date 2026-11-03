/* Actual guest compositor synchronous API mix, not clean frame pacing.
 * Only OUR fixed packet opcode is inspected, never payload or Apple fields.
 * All Submit entries balance depth, including recursive/asynchronous ones;
 * only synchronous spans are timed. Nested spans are not additive.
 * Finite, buffered output; no per-command logging or rendering-policy change.
 */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=16m
BEGIN {began=timestamp;printf("READY compositor synchronous API mix target=%d\n",$target);}
pid$target:CoreGraphics:CGXUpdateDisplay:entry {self->root_depth++;}
pid$target:CoreGraphics:CGXUpdateDisplay:entry /self->root_depth==1/
{self->root=++roots;self->at=timestamp;self->finishes=1;}
pid$target:CoreGraphics:CGXGLAccelFinish:entry /self->root_depth/ {self->finishes++;}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
{self->submit_depth++;}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry /self->root_depth && arg2/
{
    this->op=*(uint32_t *)copyin(arg1,4);
    self->op[self->submit_depth]=this->op;
    self->at_submit[self->submit_depth]=timestamp;
    self->timed[self->submit_depth]=2;
    @entered[this->op]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return /self->timed[self->submit_depth]==2/
{
    this->op=self->op[self->submit_depth];
    this->us=(timestamp-self->at_submit[self->submit_depth])/1000;
    @returned[this->op]=count();@wall[this->op]=sum(this->us);
    @maximum[this->op]=max(this->us);
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return
/self->timed[self->submit_depth]==2 && timestamp-self->at_submit[self->submit_depth]>=1000000/
{@slow[self->root,self->op[self->submit_depth],ustack(14)]=count();}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return /self->submit_depth/
{
    self->timed[self->submit_depth]=1;self->op[self->submit_depth]=0;
    self->at_submit[self->submit_depth]=0;self->submit_depth--;
}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->root_depth==1 && self->finishes>1/
{@updates[(self->at-began)/1000,self->root,(timestamp-self->at)/1000,self->finishes-1]=count();}
pid$target:CoreGraphics:CGXUpdateDisplay:return /self->root_depth/ {self->root_depth--;}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID synchronous API mix: probe error\n");exit(2);}
END {
    printf("UPDATES start_us root wall_us finishes count\n");printa("%lu %u %lu %u %@d\n",@updates);
    printf("SYNC op entered returned inclusive_us maximum_us (nested, NOT additive)\n");
    printa("%x %@d %@d %@d %@d\n",@entered,@returned,@wall,@maximum);
    printf("SLOW sync >=1ms actual root/op/caller\n");printa(@slow);
    printf("END compositor synchronous API mix roots=%u\n",roots);
}
