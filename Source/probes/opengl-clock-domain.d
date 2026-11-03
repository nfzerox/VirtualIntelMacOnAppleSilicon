/* Three-second, 20-Hz-per-CPU metadata-only guest clock consistency check.
 * No process memory, writes, scheduling changes or graphics workload.
 * Sort offline: per-CPU output buffers are not globally ordered. Compare
 * walltimestamp-timestamp offsets and per-CPU deltas, not absolute origins.
 */
#pragma D option quiet
#pragma D option bufsize=1m
BEGIN {budget=512;printf("READY clock-domain survey\n");}
profile-20hz
/budget>0/
{
    printf("CLOCK cpu=%u monotonic=%llu wall=%llu\n",cpu,
        (unsigned long long)timestamp,(unsigned long long)walltimestamp);budget--;
}
tick-3s {exit(0);}
