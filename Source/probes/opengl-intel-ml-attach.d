/* Physical Intel 10.8 reference only. These read-only context fields are
 * verified in libGPUSupport-IntelReference.x86_64 gldAttachDrawable (0xacea):
 * orientation +0x49; native returned width/height +0x99c/+0x9a0; output flags
 * +0x9e0. NOT +0x50/+0x54 (those are format/identity, not dimensions).
 * Requalify against actual loaded binary before using on another build. */
#pragma D option quiet
BEGIN { printf("READY Intel Mountain Lion drawable orientation\n"); }
pid$target:libGPUSupport.dylib:gldAttachDrawable:entry
{ self->context=arg0;self->type=arg1; }
pid$target:libGPUSupport.dylib:gldAttachDrawable:return
/self->context/
{
    this->width=*(uint32_t *)copyin(self->context+0x99c,4);
    this->height=*(uint32_t *)copyin(self->context+0x9a0,4);
    this->orientation=*(uint8_t *)copyin(self->context+0x49,1);
    this->flags=*(uint32_t *)copyin(self->context+0x9e0,4);
    @attachments[self->type,(uint32_t)arg1,this->width,this->height,this->orientation,this->flags&0xc00]=count();
    self->context=0;self->type=0;
}
tick-12sec { exit(0); }
END { printf("type result width height orientation flags calls\n");printa(@attachments); }
