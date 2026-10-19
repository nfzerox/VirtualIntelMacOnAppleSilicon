#pragma D option quiet

fbt:com.apple.driver.AppleParavirtGPU:_ZN24AppleParavirtDisplayPipe17submitTransactionEP30IOAccelDisplayPipeTransaction2:entry
{
    this->owner = *(uintptr_t *)(arg0 + 0x328);
    this->task = this->owner ? *(uintptr_t *)(this->owner + 0x150) : 0;
    printf("pipe=%p transaction=%p owner=%p task=%p heapID=%u stamp=%u gamma=%u\n",
        (void *)arg0, (void *)arg1, (void *)this->owner, (void *)this->task,
        this->task ? *(uint32_t *)(this->task + 0x268) : 0,
        *(uint32_t *)(arg0 + 0x380), *(uint32_t *)(arg0 + 0x384));
    exit(0);
}
