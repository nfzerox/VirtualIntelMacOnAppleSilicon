/* OUR AuxShared GLEngine x64 B36ADC06-D019-340C-9919-3D23FABD80F3.
 * Verify UUID first. Owned DWARF: VIMContext.drawable 0x1fa0;
 * VIMCGSDrawable surface 8, size 0x14, family 0x84, shared flag 0xdc.
 * Source API geometry/results only; no client pixels, names or payloads.
 * Separates actual successful shared auxiliary writes from eligibility.
 * Matched client 770120C8-0399-382F-8448-0FB27006157E: the import/publish
 * wrappers have genuine call/ret sequences, verified before return probes.
 */
#pragma D option quiet
#pragma D option aggsize=8m
BEGIN {printf("READY auxiliary shared-memory result survey target=%d\n",$target);}
pid$target:GLEngine:VIMImportAcceleratorBacking:entry
{self->binding=arg1;}
pid$target:libVIMPassthroughOpenGL.dylib:DGImportRAMDrawable:return,
pid$target:libVIMPassthroughOpenGL.dylib:DGImportVRAMDrawable:return
/self->binding/
{
    this->size=(int32_t *)copyin(self->binding+0x14,8);
    this->surface=*(uint32_t *)copyin(self->binding+8,4);
    @imports[probefunc,this->surface,this->size[0],this->size[1],(int32_t)arg1]=count();
}
pid$target:GLEngine:VIMImportAcceleratorBacking:return
/self->binding && !*(uint32_t *)copyin(self->binding+0x84,4)/
{
    this->size=(int32_t *)copyin(self->binding+0x14,8);
    @selected[*(uint32_t *)copyin(self->binding+8,4),this->size[0],this->size[1],
        *(int32_t *)copyin(self->binding+0xdc,4),(uint32_t)arg1]=count();
}
pid$target:GLEngine:VIMImportAcceleratorBacking:return {self->binding=0;}
pid$target:GLEngine:VIMPresentDrawableKnown:entry
{self->presenting=arg0?*(uint64_t *)copyin(arg0+0x1fa0,8):0;}
pid$target:libVIMPassthroughOpenGL.dylib:DGPublishVRAMDrawable:return
/self->presenting/
{
    this->size=(int32_t *)copyin(self->presenting+0x14,8);
    @writes[*(uint32_t *)copyin(self->presenting+8,4),this->size[0],this->size[1],(int32_t)arg1]=count();
}
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawableRect:entry
/self->presenting/
{
    this->size=(int32_t *)copyin(self->presenting+0x14,8);
    @downloads[probefunc,*(uint32_t *)copyin(self->presenting+8,4),this->size[0],this->size[1]]=count();
}
pid$target:GLEngine:VIMPresentDrawableKnown:return {self->presenting=0;}
tick-30s {exit(0);}
END {
    printf("import API surface width height result calls\n");printa("%s %u %d %d %d %@d\n",@imports);
    printf("binding surface width height shared CGL_status calls\n");printa("%u %d %d %d %u %@d\n",@selected);
    printf("publish surface width height result calls\n");printa("%u %d %d %d %@d\n",@writes);
    printf("fallback API surface width height calls\n");printa("%s %u %d %d %@d\n",@downloads);
}
