/* Read-only 13F1911 x86_64 CoreGraphics contract probe. Offsets derived from
 * ED5E3C37-7E2F-3885-8091-DFC9B44B38E8, CGXActivateSurfaces @ 0x3f8622.
 * Do not use on another binary. No pointers/pixels are printed.
 */
#pragma D option quiet
pid$target:CoreGraphics:CGXActivateSurfaces:entry
{
    self->surface=arg0?*(uint64_t *)copyin(arg0+0x110,8):0;
    @present[self->surface!=0]=count();
}
pid$target:CoreGraphics:CGXActivateSurfaces:entry
/self->surface/
{
    this->s=(uint8_t *)copyin(self->surface,0xb0);
    @gate[this->s[0x35],*(uint32_t *)(this->s+0x1c),
        *(uint64_t *)(this->s+0xa8)!=0,*(uint32_t *)(this->s+0x3c),
        this->s[0x40],this->s[0x43]]=count();
}
pid$target:CoreGraphics:current_session_no_hardware_surface_visibility:return
{ @visibility_disabled[arg1 & 0xff]=count(); }
pid$target:CoreGraphics:MPHWCopyForwardRegion:entry
{ @copy_flags[*(uint32_t *)copyin(arg0+0x64,4)]=count(); }
tick-15s {exit(0);}
END {
    printf("SURFACE_PRESENT\n");printa(@present);
    printf("GATE ready type resource subtype visibility40 visibility43\n");printa(@gate);
    printf("SESSION_VISIBILITY_DISABLED\n");printa(@visibility_disabled);
    printf("COPY_FLAGS\n");printa(@copy_flags);
}
