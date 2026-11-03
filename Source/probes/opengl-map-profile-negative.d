/* Normal DTrace instrumentation changes redirect's entry bytes. The private
 * exact-method guard must REFUSE admission before blocking any scratch bytes.
 * Do not use this during ordinary rendering or count its timings as FPS. */
#pragma D option quiet
dtrace:::BEGIN { printf("READY redirect profile negative control\n"); }
fbt:mach_kernel:_ZN11IOMemoryMap8redirectEP18IOMemoryDescriptorjy:entry { ++redirect_calls; }
tick-6sec { exit(0); }
dtrace:::END { printf("DONE redirect profile negative control calls=%d\n",redirect_calls); }
