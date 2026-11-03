/* OUR installed DirectFamily GLEngine, UUID
 * 585A6AED-2592-31D4-B9F3-2285C354C84D. Verify before tracing.
 * DWARF offsets: family 0x84, clip pointer 0xa0, direct flag 0xd8.
 * Observe the result of actual imports/attachments, not just eligibility.
 * Metadata only; finite observation, no pixels or writes, not an FPS test.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY actual family drawable bindings target=%d\n",$target);}
pid$target:GLEngine:VIMImportAcceleratorBacking:entry
{self->view=arg1;}
pid$target:GLEngine:VIMImportAcceleratorBacking:return
/self->view && *(uint32_t *)copyin(self->view+0x84,4) &&
 *(uint64_t *)copyin(self->view+0xa0,8)/
{
    this->clip=*(uint64_t *)copyin(self->view+0xa0,8);
    this->count=*(uint32_t *)copyin(this->clip+4,4);
    this->direct=*(int32_t *)copyin(self->view+0xd8,4);
    @bindings[(uint32_t)arg1,this->direct,this->count]=count();
}
pid$target:GLEngine:VIMImportAcceleratorBacking:return
{self->view=0;}
tick-30s {exit(0);}
END {
    printf("CGL_status explicit_shared clip_rectangles calls\n");
    printa("%u %d %u %@d\n",@bindings);
}
