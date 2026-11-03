/* Finite owned-wire survey. A repeated error query with no intervening
 * packet is a candidate only: a nonzero error may leave other flags pending.
 * No Apple structures/pixels, mutation, per-call output or FPS inference.
 */
#pragma D option quiet
#pragma D option aggsize=2m
#pragma D option dynvarsize=2m
BEGIN {last[0]=0;printf("READY owned error-query adjacency\n");}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:entry
{
    this->op=*(uint32_t *)copyin(arg1,4);
    this->error=this->op==0x10005 || this->op==0x10046;
    self->query_ctx=this->error?arg0:0;
    self->query_op=this->op;
    self->adjacent=last[arg0]==0x10005 || last[arg0]==0x10046;
    last[arg0]=this->op;
}
pid$target:libVIMPassthroughOpenGL.dylib:DGSubmit:return /self->query_ctx/
{
    @queries[self->query_op,self->adjacent]=count();self->query_ctx=0;
}
tick-12s {exit(0);}
ERROR {printf("INVALID: query adjacency probe error\n");exit(2);}
END {printa("QUERY op=%x previous_was_error=%d calls=%@d\n",@queries);}
