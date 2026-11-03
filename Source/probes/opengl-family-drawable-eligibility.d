/* Guest-only, read-only eligibility survey of OUR drawable structures.
 * Verify GLEngine UUID 947A47CF-7AE9-3E54-BC82-3D46D9402625 first.
 * Offsets are from that exact executable's DWARF, not Apple structures.
 * Copies geometry/clip metadata only; no pixels, payloads or process names.
 * A full-size import alone does not prove a full-coverage native clip.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY family drawable coverage target=%d\n",$target);}
pid$target:GLEngine:VIMImportAcceleratorBacking:entry
/arg1 && *(uint32_t *)copyin(arg1+0x84,4) && *(uint64_t *)copyin(arg1+0xa0,8)/
{
    this->size=(int32_t *)copyin(arg1+0x14,8);
    this->geometry=(int32_t *)copyin(arg1+0x78,12);
    this->unflipped=*(int32_t *)copyin(arg1+0x6c,4);
    this->clip=*(uint64_t *)copyin(arg1+0xa0,8);
    this->header=(int32_t *)copyin(this->clip,16);
    @views[this->size[0],this->size[1],this->geometry[1],this->geometry[2],
        this->unflipped,this->header[1]]=count();
}
pid$target:GLEngine:VIMImportAcceleratorBacking:entry
/arg1 && *(uint32_t *)copyin(arg1+0x84,4) &&
 *(uint64_t *)copyin(arg1+0xa0,8) &&
 *(uint32_t *)copyin(*(uint64_t *)copyin(arg1+0xa0,8)+4,4)==1/
{
    this->size=(int32_t *)copyin(arg1+0x14,8);
    this->geometry=(int32_t *)copyin(arg1+0x78,12);
    this->clip=*(uint64_t *)copyin(arg1+0xa0,8);
    this->header=(int32_t *)copyin(this->clip,16);
    this->rect=(int16_t *)copyin(this->clip+16,8);
    this->x=(int64_t)this->rect[0]-this->header[2]-this->geometry[1];
    this->y=(int64_t)this->rect[1]-this->header[3]-this->geometry[2];
    this->covers=this->geometry[0] && this->rect[2]>0 && this->rect[3]>0 &&
        this->x<=0 && this->y<=0 && this->x+this->rect[2]>=this->size[0] &&
        this->y+this->rect[3]>=this->size[1];
    @one_rect[this->covers]=count();
}
tick-30s {exit(0);}
END {
    printf("width height origin_x origin_y unflipped clip_count calls\n");
    printa("%d %d %d %d %d %d %@d\n",@views);
    printf("single-rectangle full coverage (0=no 1=yes) calls\n");
    printa("%d %@d\n",@one_rect);
}
