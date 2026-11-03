/* Bounded publication provenance, paired by exact logical epoch with the
 * optional immutable-source journal, not by guessed guest/host clock offsets.
 * Manually verify installed GLEngine UUID
 * 947A47CF-7AE9-3E54-BC82-3D46D9402625 first. Own-adapter offsets are from
 * that binary's VIMCGSDrawable DWARF, NOT Apple object layouts. No writes.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN {budget=256;printf("READY exact family publication epochs\n");}
pid$target:GLEngine:VIMGLIFlushMode:entry
/arg0 && *(uint64_t *)copyin(arg0+0x1e90,8)/
{
    this->format=(int32_t *)copyin(*(uint64_t *)copyin(arg0+0x1e90,8),24);
    @formats[this->format[1],this->format[2],this->format[4],this->format[5]]=count();
}
pid$target:GLEngine:VIMCommitGPUFamily:entry
/arg0 && arg1 && budget>0/
{
    this->size=(int32_t *)copyin(arg0+0x14,8);
    this->geometry=(int32_t *)copyin(arg0+0x7c,12);
    this->state=(uint64_t *)copyin(arg1,32);
    printf("%lu COMMIT epoch=%lu source-epoch=%lu object=%lx surface=%x view=%dx%d origin=%d,%d family=%d unflipped=%d\n",
        (unsigned long)timestamp,(unsigned long)arg2,(unsigned long)this->state[3],
        (unsigned long)this->state[1],*(uint32_t *)copyin(arg0+8,4),
        this->size[0],this->size[1],this->geometry[0],this->geometry[1],this->geometry[2],
        *(int32_t *)copyin(arg0+0x6c,4));
    ustack(9);budget--;
}
pid$target:GLEngine:VIMCommitGPUFamily:entry
/arg0 && budget>0 && *(uint64_t *)copyin(arg0+0xa0,8)/
{
    this->clip=*(uint64_t *)copyin(arg0+0xa0,8);
    this->header=(int32_t *)copyin(this->clip,16);
    printf("CLIP epoch=%lu version=%d count=%d origin=%d,%d\n",
        (unsigned long)arg2,this->header[0],this->header[1],this->header[2],this->header[3]);
}
pid$target:GLEngine:VIMCommitGPUFamily:entry
/arg0 && budget>0 && *(uint64_t *)copyin(arg0+0xa0,8) &&
 *(uint32_t *)copyin(*(uint64_t *)copyin(arg0+0xa0,8)+4,4)>0 &&
 *(uint32_t *)copyin(*(uint64_t *)copyin(arg0+0xa0,8)+4,4)<=3/
{
    this->clip=*(uint64_t *)copyin(arg0+0xa0,8);
    this->count=*(uint32_t *)copyin(this->clip+4,4);
    this->r=(int16_t *)copyin(this->clip+16,this->count*8);
    printf("RECT epoch=%lu index=0 rect=%d,%d,%d,%d\n",(unsigned long)arg2,
        this->r[0],this->r[1],this->r[2],this->r[3]);
}
pid$target:GLEngine:VIMCommitGPUFamily:entry
/arg0 && budget>0 && *(uint64_t *)copyin(arg0+0xa0,8) &&
 *(uint32_t *)copyin(*(uint64_t *)copyin(arg0+0xa0,8)+4,4)>=2 &&
 *(uint32_t *)copyin(*(uint64_t *)copyin(arg0+0xa0,8)+4,4)<=3/
{
    this->clip=*(uint64_t *)copyin(arg0+0xa0,8);
    this->r=(int16_t *)copyin(this->clip+24,8);
    printf("RECT epoch=%lu index=1 rect=%d,%d,%d,%d\n",(unsigned long)arg2,
        this->r[0],this->r[1],this->r[2],this->r[3]);
}
pid$target:GLEngine:VIMCommitGPUFamily:entry
/arg0 && budget>0 && *(uint64_t *)copyin(arg0+0xa0,8) &&
 *(uint32_t *)copyin(*(uint64_t *)copyin(arg0+0xa0,8)+4,4)==3/
{
    this->clip=*(uint64_t *)copyin(arg0+0xa0,8);
    this->r=(int16_t *)copyin(this->clip+32,8);
    printf("RECT epoch=%lu index=2 rect=%d,%d,%d,%d\n",(unsigned long)arg2,
        this->r[0],this->r[1],this->r[2],this->r[3]);
}
pid$target:IOKit:IOAccelFlushSurfaceOnFramebuffers:entry
/budget>0/
{printf("%lu PRESENT connection=%x options=%x mask=%x\n",(unsigned long)timestamp,
    (uint32_t)arg0,(uint32_t)arg1,(uint32_t)arg2);budget--;}
tick-12s {exit(0);}
END {printf("remaining metadata budget=%d\n",budget);
    printf("format depth stencil double-buffer profile\n");printa(@formats);}
