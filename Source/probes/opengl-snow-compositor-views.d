/* Metadata-only trace of OUR source-defined x64 drawable. The offsets are
 * compile-time asserted in cgs-drawable.c. No Apple private struct decoding,
 * pixel capture, injected code, permanent tracing or production logging.
 * Start against the actual guest WindowServer, wait for READY, open a stack.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY Snow compositor view survey pid=%d\n",$target);}
pid$target:GLEngine:VIMImportAcceleratorBacking:entry
{
    this->drawing=(int32_t *)copyin(arg1+20,8);
    this->flip=*(uint32_t *)copyin(arg1+108,4);
    this->view=(int32_t *)copyin(arg1+120,12);
    @views[this->drawing[0],this->drawing[1],this->flip,
        this->view[0],this->view[1],this->view[2]]=count();
}
pid$target:GLEngine:gliSetInteger:entry
/arg1==0x3e5 || arg1==0x3e6 || arg1==0x3e7/
{
    this->a=(uint32_t *)copyin(arg2,20);
    @imports[arg1,this->a[0],this->a[2],this->a[3],this->a[4]]=count();
}
pid$target:GLEngine:VIMDispatch_copy_tex_image2D:entry,
pid$target:GLEngine:VIMDispatch_copy_tex_sub_image2D:entry
{
    @copies[probefunc,(int32_t)arg0,(int32_t)arg1,(int32_t)arg2,
        (int32_t)arg3,(int32_t)arg4,(int32_t)arg5]=count();
}
tick-30s {exit(0);}
END {
    printf("VIEW width height unflipped is-region origin-x origin-y calls\n");printa(@views);
    printf("IMPORT key surface internal width height calls\n");printa(@imports);
    printf("COPY function and first six native dispatch arguments\n");printa(@copies);
}
