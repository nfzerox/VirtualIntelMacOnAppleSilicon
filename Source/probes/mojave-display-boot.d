#pragma D option quiet
#pragma D option bufsize=8m
/* Temporary root LaunchDaemon captures display startup without delaying WS. */
fbt:com.apple.driver.AppleParavirtGPU:*AppleParavirtDisplayPipe*:entry,
fbt:com.apple.iokit.IOAcceleratorFamily2:*IOAccelDisplayPipe*:entry
/++events <= 6000/
{
    printf("%llu tid=%d %s this=%p a1=%p a2=%p\n",
        timestamp,tid,probefunc,arg0,arg1,arg2);
}
fbt:com.apple.iokit.IOAcceleratorFamily2:_ZN29IOAccelDisplayPipeUserClient217s_transaction_endEPS_PvP25IOExternalMethodArguments:entry
{
    this->input=*(uintptr_t *)((uintptr_t)arg2+0x30);
    printf("TRANSACTION\n");
    tracemem((void *)this->input,0x108);
}
tick-60s { exit(0); }
