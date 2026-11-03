/* Bounded IOAccel shape/backing survey. Numeric metadata only, no pixels.
 * Selector interpretation must be checked against the captured caller stack:
 * other user-client types can reuse the same selector numbers.
 * sudo dtrace -q -s opengl-primary-backing.d -p WINDOWSERVER_PID
 * Run separately from timing tests; tracing perturbs the workload.
 */
#pragma D option quiet
#pragma D option aggsize=8m
pid$target:IOKit:IOConnectCallMethod:entry
/(arg1==6 && arg3==4) || (arg1==17 && arg3==5)/
{
    this->s=(uint64_t *)copyin(arg2,32);
    @backing[arg0,arg1,this->s[0],this->s[2],this->s[3],ustack(6)]=count();
}
pid$target:IOKit:IOConnectCallMethod:entry
/arg1==9 && arg3==2/
{
    this->s=(uint64_t *)copyin(arg2,16);
    @shape[arg0,this->s[0],this->s[1],ustack(6)]=count();
}
pid$target:IOKit:IOConnectCallMethod:entry
/((arg1==0 || arg1==1 || arg1==3 || arg1==4) && arg3==1) ||
 ((arg1>=12 && arg1<=15) && arg3==0)/
{
    @locks[arg0,arg1,ustack(6)]=count();
}
tick-15s { exit(0); }
END
{
    printf("BACKING connection selector options address rowbytes stack count\n");printa(@backing);
    printf("SHAPE connection options framebuffer stack count\n");printa(@shape);
    printf("LOCKS connection selector stack count\n");printa(@locks);
}
