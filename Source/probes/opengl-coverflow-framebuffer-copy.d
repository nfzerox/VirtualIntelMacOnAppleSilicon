/* Native/bridge GLI ABI comparison for the ImageKit text shader's destination
 * texture. Function arguments include GLI context at arg0 on both routes.
 * No source pixels are read. Read/copy buffer coordinates and caller stacks
 * identify the actual API path rather than guessing a texture orientation. */
#pragma D option quiet
#pragma D option dynvarsize=16m
#pragma D option aggsize=8m
#pragma D option bufsize=4m
BEGIN { printf("READY Cover Flow framebuffer-copy survey\n"); }
pid$target:GLEngine:*gliSetInteger:entry
{ @keys[arg1]=count(); }
pid$target:GLEngine:*gliSetInteger:entry
/(arg1==0x3e5 || arg1==0x38e) && arg2/
{
    this->a=(uint32_t *)copyin(arg2,32);
    @imports[arg1,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4],this->a[5],this->a[6],this->a[7],ustack(8)]=count();
}
pid$target:GLEngine:*gliSetInteger:entry
/arg1==0x3e7 && arg2/
{
    /* Matching native GLEngine consumes FIVE words for 3e7. Adjacent stack
     * values are not texture names, flags or mipmap arguments. */
    this->a=(uint32_t *)copyin(arg2,20);
    @imports[arg1,this->a[0],this->a[1],this->a[2],this->a[3],this->a[4],0,0,0,ustack(8)]=count();
}
pid$target:GLEngine:*glCopyTexImage2D_Exec:entry,
pid$target:GLEngine:*glCopyTexSubImage2D_Exec:entry,
pid$target:GLEngine:*glReadPixels_Exec:entry,
pid$target:GLEngine:*VIMDispatch_copy_tex_image2D:entry,
pid$target:GLEngine:*VIMDispatch_copy_tex_sub_image2D:entry,
pid$target:GLEngine:*VIMDispatch_read_pixels:entry
{
    printf("%s args=%x,%x,%x,%x,%x,%x,%x\n",probefunc,
        (uint32_t)arg1,(uint32_t)arg2,(uint32_t)arg3,(uint32_t)arg4,
        (uint32_t)arg5,(uint32_t)arg6,(uint32_t)arg7);
    @calls[probefunc,ustack(12)]=count();
}
tick-10sec { exit(0); }
END { printa(@calls);printf("GLI KEYS\n");printa(@keys);printf("IMPORTS\n");printa(@imports); }
