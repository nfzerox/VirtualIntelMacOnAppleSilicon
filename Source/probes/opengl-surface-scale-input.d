/* Actual IOAccelSurfaceSetScale (selector 8, 44-byte public record) inputs.
 * User-space IOKit boundary, not an assumed kernel FBT module/symbol name.
 * Track ports created by IOAccelCreateSurface before interpreting results. */
#pragma D option quiet
BEGIN { printf("READY surface scale inputs pid=%d\n",$target); }
pid$target:IOKit:IOAccelSetSurfaceScale:entry
{ @api[arg1,arg3]=count(); }
pid$target:IOKit:IOConnectCallMethod:entry
{ @methods[arg1,arg3,arg5]=count(); }
pid$target:IOKit:IOConnectCallMethod:entry
/arg1==8 && arg2 && arg3==1 && arg4 && arg5==44/
{
    this->a=(int16_t *)copyin(arg4,12);
    this->options=*(uint64_t *)copyin(arg2,8);
    @scales[arg0,this->options,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4],this->a[5]]=count();
}
tick-12sec { exit(0); }
END { printf("scale API options record_bytes calls\n");printa(@api);
    printf("method selector scalar_count record_bytes calls\n");printa(@methods);
    printf("port options buffer_x/y/w/h source_w/h calls\n");printa(@scales); }
