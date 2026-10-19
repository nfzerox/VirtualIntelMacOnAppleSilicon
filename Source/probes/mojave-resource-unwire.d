/* Read-only resource lifetime diagnostics. Donor 442.10 / PVG 11.5 fields
 * verified in initClientShared/free/synchronizeForUnwire disassembly.
 * No event, fence, or resource state is changed. Bounded to 60 seconds. */
#pragma D option quiet
#pragma D option switchrate=10hz
fbt:com.apple.driver.AppleParavirtGPU:*synchronizeForUnwire*:entry
{
    this->r = (unsigned char *)arg0;
    printf("unwire t=%llu r=%p flags=%x type=%x index=%u shared=%p private=%p map=%p gpu=%x\n",
        timestamp, arg0, *(unsigned int *)(this->r+0xc), this->r[0x14],
        this->r[0x16], *(uintptr_t *)(this->r+0x38),
        *(uintptr_t *)(this->r+0x90), *(uintptr_t *)(this->r+0x40),
        *(unsigned int *)(this->r+0x180));
}
tick-60s { exit(0); }
