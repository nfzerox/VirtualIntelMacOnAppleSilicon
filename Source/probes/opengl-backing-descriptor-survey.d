/* Read-only, bounded actual WindowServer descriptor creation survey.
 * Use -p ACTUAL_WS_PID. Constructor is static: args are address,length,options,
 * task, not a guessed class layout. Kernel stack identifies our wrapping path.
 * Filter kIODirectionInOut(3), used by real surface backing wraps. Ordinary
 * stream staging uses direction1/2 and would drown this bounded survey.
 * Records may be retired before vmmap: a map match is diagnostic, NOT proof
 * of a live backing, object isolation or permission to exclude CPU accesses.
 */
#pragma D option quiet
#pragma D option bufsize=4m
dtrace:::BEGIN { printf("READY descriptor-creation survey target=%d\n",$target); }
fbt:mach_kernel:_ZN18IOMemoryDescriptor16withAddressRangeEyyjP4task:entry
/(pid==$target || execname=="Finder") && arg0 && arg1 && (arg2&3)==3 && survey_count<96/
{
    ++survey_count;
    printf("RANGE %llx %llx %x %llx pid=%d process=%s\n",arg0,arg1,arg2,arg3,pid,execname);
    stack(8);
}
tick-12sec { exit(0); }
dtrace:::END { printf("DONE bounded descriptor survey count=%d\n",survey_count); }
