#pragma D option quiet

fbt:com.apple.iokit.IOAcceleratorFamily2:_ZN29IOAccelDisplayPipeUserClient217s_transaction_endEPS_PvP25IOExternalMethodArguments:entry
{
    this->arguments = (uintptr_t)arg2;
    this->input = *(uintptr_t *)(this->arguments + 0x30);
    this->size = *(uint32_t *)(this->arguments + 0x38);
    printf("arguments=%p input=%p size=0x%x\n",
        (void *)this->arguments, (void *)this->input, this->size);
    tracemem((void *)this->input, 0x108);
    exit(0);
}
