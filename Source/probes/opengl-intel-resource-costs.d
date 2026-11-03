/* Physical Intel Mavericks only. Finite nested wall-time attribution, NOT
 * FPS/CPU-only evidence. Count mode/kind metadata, never resource addresses,
 * pixel contents or object payloads. The native Mercury object wrappers use
 * indirect jump tables: Apple's pid provider exposes entries but no returns.
 * Time the verified Intel submission and IOAccelerator event bodies instead;
 * the GLContext aliases and Mercury FlushContext are also tail jumps. */
#pragma D option quiet
#pragma D option dynvarsize=8m
#pragma D option aggsize=4m
BEGIN {began=timestamp;printf("READY physical Intel resource costs\n");}
pid$target:*Intel*:glrFlushContextToken:entry,
pid$target:IOAccelerator:IOAccelResourceFinishEvent:entry
{
    self->depth[probefunc]++;
    self->at[probefunc,self->depth[probefunc]]=timestamp;
    @entered[probefunc]=count();
}
pid$target:IOKit:IOConnectCallMethod:entry
/self->depth["IOAccelResourceFinishEvent"] && arg1==3/
{@kernel_resource_waits=count();}
pid$target:*Intel*:glrFlushContextToken:return,
pid$target:IOAccelerator:IOAccelResourceFinishEvent:return
/self->depth[probefunc] && self->at[probefunc,self->depth[probefunc]]/
{
    this->us=(timestamp-self->at[probefunc,self->depth[probefunc]])/1000;
    @calls[probefunc]=count();
    @sum[probefunc]=sum(this->us);
    @max[probefunc]=max(this->us);
    @returned[probefunc]=count();@latency[probefunc]=quantize(this->us);
    self->at[probefunc,self->depth[probefunc]]=0;self->depth[probefunc]--;
}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
END {
    printf("ENTERED RETURNED (require actual glrFlushContextToken and balanced exits)\n");
    printa("%s %@d %@d\n",@entered,@returned);
    printf("FUNCTION calls inclusive_us max_us\n");
    printa("%s %@d %@d %@d\n",@calls,@sum,@max);printa(@latency);
    printf("Resource FinishEvent kernel method3 waits (not total GPU waits)\n");printa(@kernel_resource_waits);
}
