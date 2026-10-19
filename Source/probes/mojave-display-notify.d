#pragma D option quiet
fbt:com.apple.driver.AppleParavirtGPU:_ZN24AppleParavirtDisplayPipe13signalDisplayEv:entry
{
    this->shared=*(uintptr_t *)((uintptr_t)arg0+0x378);
    printf("%llu signalDisplay pending=%x enabled=%x\n",timestamp,
        *(uint32_t *)(this->shared+0x100),*(uint32_t *)(this->shared+0x104));
}
fbt:com.apple.iokit.IOAcceleratorFamily2:*signalTransactionInterrupt*:entry,
fbt:com.apple.iokit.IOAcceleratorFamily2:*transaction_interrupt_gated*:entry,
fbt:com.apple.iokit.IOAcceleratorFamily2:*completeTransaction*:entry,
fbt:com.apple.driver.AppleParavirtGPU:*submitTransaction*:entry
{
    printf("%llu %s pipe=%p a1=%p\n",timestamp,probefunc,arg0,arg1);
}
tick-30s { exit(0); }
