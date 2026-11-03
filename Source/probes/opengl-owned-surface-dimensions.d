/* Finite WindowServer/IOKit boundary survey. API arguments only; no class
 * offsets, pixel reads or altered return values. Release-injected kexts need
 * not expose FBT symbols. Trigger the temporary behind-menu GL window after
 * READY to compare with the physical Intel size trace.
 */
#pragma D option quiet
BEGIN { printf("READY owned surface dimensions\n"); }
pid$target:IOKit:IOAccelSetSurfaceScale:entry
/arg2 && arg3>=12/
{
    this->a=(int16_t *)copyin(arg2,12);
    printf("scale port=%x flags=%x buffer=%d,%d,%d,%d source=%d,%d\n",
        arg0,arg1,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4],this->a[5]);
}
pid$target:IOKit:IOAccelSetSurfaceFramebufferShape:entry
/arg3 && arg4>=12/
{
    this->a=(int16_t *)copyin(arg3,12);
    printf("shape port=%x flags=%x bounds=%d,%d,%d,%d\n",
        arg0,arg1,this->a[2],this->a[3],this->a[4],this->a[5]);
}
pid$target:IOKit:IOConnectCallMethod:entry
/arg1==4 && arg2 && arg3==3/
{
    this->values64=(uint64_t *)copyin(arg2,24);
    printf("context-scale port=%x flags=%x width=%u height=%u\n",arg0,this->values64[0],this->values64[1],this->values64[2]);
}
pid$target:IOKit:IOConnectCallMethod:entry
/( (arg1==9 && arg3==2) || (arg1==6 && arg3==4) || (arg1==17 && arg3==5) ) && arg2 && arg4 && arg5>=12/
{
    this->region=(int16_t *)copyin(arg4,arg5>=20?20:12);
    printf("shape-method port=%x selector=%u bytes=%u count=%u bounds=%d,%d,%d,%d\n",
        arg0,arg1,arg5,*(uint32_t *)this->region,this->region[2],this->region[3],this->region[4],this->region[5]);
}
pid$target:IOKit:IOConnectCallMethod:entry
/((arg1==6 && arg3==4) || (arg1==17 && arg3==5)) && arg2 && arg4 && arg5>=20/
{
    this->backed=(uint64_t *)copyin(arg2,24);
    this->shape=(int16_t *)copyin(arg4,20);
    printf("client-shape port=%x backed=%u count=%u bounds=%d,%d,%d,%d first=%d,%d,%d,%d\n",
        arg0,this->backed[2]!=0,*(uint32_t *)this->shape,
        this->shape[2],this->shape[3],this->shape[4],this->shape[5],
        this->shape[6],this->shape[7],this->shape[8],this->shape[9]);
}
tick-12sec { exit(0); }
