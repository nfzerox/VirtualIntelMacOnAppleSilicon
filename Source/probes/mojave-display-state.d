#pragma D option quiet
/* Donor 20G1426 offsets verified in start/getDisplayPipe/enableVBLInterrupt.
 * Trigger with ioreg. Read-only object/shared RAM, not interrupt MMIO. */
fbt:com.apple.driver.AppleParavirtGPU:_ZNK24AppleParavirtAccelerator12getMetaClassEv:entry
/!captured/
{
    this->machine=*(uintptr_t *)(arg0+0x378);
    this->pipe=this->machine ? *(uintptr_t *)(this->machine+0x88) : 0;
    this->shared=this->pipe ? *(uintptr_t *)(this->pipe+0x378) : 0;
    printf("accelerator=%p machine=%p pipe=%p shared=%p\n",arg0,this->machine,this->pipe,this->shared);
    captured=1;
}
tick-5s { exit(0); }
