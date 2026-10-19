#!/usr/sbin/dtrace -s

#pragma D option quiet

/*
 * Correlate IOAccel's user command list, the APV 0x37 packet placed on the
 * Exec FIFO, and the eventual host fault.  All dumps are bounded so this can
 * be left around a short compositor reproduction without flooding the guest.
 */

fbt:com.apple.driver.AppleParavirtGPU:_ZN25AppleParavirtCommandQueue19processExecIndirectEP26IOAccelSegmentResourceListPK32AppleParavirtCommandExecIndirectPKv:entry
{
    this->exec_len = arg3 > arg2 ? arg3 - arg2 : 0;
    printf("PROCESS wall=%Y pid=%d exec=%s queue=%p resources=%p command=%p len=%lld\n",
        walltimestamp, pid, execname, arg0, arg1, arg2, this->exec_len);
    tracemem(arg2, 256);
}

fbt:com.apple.driver.AppleParavirtGPU:_ZN29AppleParavirtCommandAllocator15submitOnChannelEP20AppleParavirtChannelP24IOAccelCommandDescriptor:entry
/* AppleParavirtCommandAllocator::fCommand is at +0x400 in 20G1443. */
/(this->packet = *(uintptr_t *)(arg0 + 0x400)) != 0 &&
    *(uint16_t *)this->packet == 0x37/
{
    this->packet_len = *(uint32_t *)(this->packet + 4);
    printf("SUBMIT wall=%Y pid=%d exec=%s allocator=%p channel=%p descriptor=%p packet=%p len=%u\n",
        walltimestamp, pid, execname, arg0, arg1, arg2, this->packet,
        this->packet_len);
    tracemem(this->packet, 256);
}

fbt:com.apple.driver.AppleParavirtGPU:_ZN24AppleParavirtAccelerator20handleFaultInterruptEv:entry
{
    printf("FAULT wall=%Y pid=%d exec=%s accelerator=%p\n",
        walltimestamp, pid, execname, arg0);
}

/* A launch daemon starts this before login; cover the full first desktop pass. */
tick-180sec
{
    exit(0);
}
