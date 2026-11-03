/* Read-only research-build ABI, NOT an OS offset or production patch.
 * Verify CroppedAliases/GLEngine UUID 947A47CF-7AE9-3E54-BC82-3D46D9402625
 * before running. Offsets
 * are from that binary's VIMContext/VIMCGSDrawable DWARF. A bounded survey
 * distinguishes a full-allocation self-import from a cropped active view. */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN { printf("READY current research view/import geometry\n"); }
pid$target:GLEngine:gliSetInteger:entry
{ self->view=0;self->record=0;@keys[arg1]=count(); }
pid$target:GLEngine:gliSetInteger:entry
/(arg1==0x3e5 || arg1==0x3e7) && arg0 && arg2/
{ self->view=*(uint64_t *)copyin(arg0+0x1fa0,8);self->record=arg2;self->key=arg1;
  @sources[arg1,*(uint32_t *)copyin(arg2,4),self->view]=count(); }
pid$target:GLEngine:gliSetInteger:entry
/self->view && self->record/
{
    this->a=(uint32_t *)copyin(self->record,20);
    this->surface=*(uint32_t *)copyin(self->view+8,4);
    this->size=(int32_t *)copyin(self->view+0x14,8);
    this->geometry=(int32_t *)copyin(self->view+0x7c,12);
    @views[self->key,this->a[0],this->surface,this->size[0],this->size[1],
        this->geometry[0],this->geometry[1],this->geometry[2],this->a[3],this->a[4]]=count();
    self->view=0;self->record=0;
}
tick-12sec { exit(0); }
END { printf("all keys\n");printa(@keys);printf("key source-ID active-view-pointer calls\n");printa(@sources);
      printf("key import-ID drawable-ID view-width/height origin-x/y family import-width/height calls\n");printa(@views); }
