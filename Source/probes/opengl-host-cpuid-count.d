/* Finite actual CPU-model call counts, not clean FPS or VM-exit durations.
 * x86_64 CPU-model ABI: env in RDI, leaf in ESI, subleaf in EDX. No process
 * memory reads, guest state changes, scheduler changes or persistent observer.
 * With the existing fast-leaf0 option OFF, actual HVF CPUID exits reach this
 * function. With it ON, entry caching ALSO calls it, so do not compare those
 * two counts as exit counts. Verify launch environment before interpretation.
 */
#pragma D option quiet
#pragma D option aggsize=1m
BEGIN {began=timestamp;printf("READY host CPUID count target=%d\n",$target);}
pid$target::cpu_x86_cpuid:entry
{@leaves[(uint32_t)uregs[R_RSI],(uint32_t)uregs[R_RDX]]=count();}
tick-1s /timestamp-began>=12000000000/ {exit(0);}
ERROR {printf("INVALID host CPUID count: probe error\n");exit(2);}
END {printf("LEAVES leaf subleaf calls\n");printa(@leaves);printf("END host CPUID count\n");}
