/* Metadata-only cost attribution for the installed CroppedAliases engine:
 * UUID 947A47CF-7AE9-3E54-BC82-3D46D9402625. Check it before running.
 * These are OUR VIMContext/VIMCGSDrawable DWARF offsets, never Apple layouts.
 * Attach to WindowServer; run real transitions after READY. No pixels/writes.
 * DGReadDrawable has a real return after submitting/freeing its packet.
 */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option aggsize=8m
BEGIN {printf("READY animation readback geometry\n");}
pid$target:GLEngine:VIMPresentDrawableKnown:entry
{self->drawable=arg0 ? *(uint64_t *)copyin(arg0+0x1fa0,8) : 0;}
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawable:entry
/self->drawable/
{
    this->d=self->drawable;
    self->window=*(int32_t *)copyin(this->d+4,4);
    self->surface=*(uint32_t *)copyin(this->d+8,4);
    self->family=*(uint32_t *)copyin(this->d+0x84,4);
    self->width=(uint32_t)arg1;self->height=(uint32_t)arg2;
    self->began=timestamp;
    @stacks[self->surface,self->width,self->height,ustack(7)]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawable:return
/self->began/
{
    @reads[self->window,self->surface,self->family,self->width,self->height]=count();
    @us[self->window,self->surface,self->family,self->width,self->height]=sum((timestamp-self->began)/1000);
    self->began=0;
}
pid$target:GLEngine:VIMPresentDrawableKnown:return
{self->drawable=0;}
tick-12s {exit(0);}
END {printf("window surface family width height calls inclusive_us\n");
    printa("%d %x %x %d %d %@d %@d\n",@reads,@us);printa(@stacks);}
