#pragma D option quiet
/* Run before enabling WindowServer; bounded, read-only kernel tracing. */
fbt:com.apple.driver.AppleParavirtGPU::entry
{
    @calls[probefunc] = count();
}
fbt:com.apple.driver.AppleParavirtGPU:*AppleParavirtDisplayPipe*:entry,
fbt:com.apple.driver.AppleParavirtGPU:*AppleParavirtFramebuffer*Interrupt*:entry,
fbt:com.apple.driver.AppleParavirtGPU:*setVBLTimer*:entry
/++events < 300/
{
    printf("%llu %s this=%p arg1=%p arg2=%p\n",timestamp,probefunc,arg0,arg1,arg2);
}
tick-30s
{
    exit(0);
}
