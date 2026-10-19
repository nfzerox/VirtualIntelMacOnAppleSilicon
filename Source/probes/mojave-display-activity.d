#pragma D option quiet
/* Bounded live census: no writes to guest memory. */
fbt:com.apple.driver.AppleParavirtGPU::entry
{
    @calls[probefunc] = count();
}
tick-5s
{
    exit(0);
}
