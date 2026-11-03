/* Bounded on-CPU stack attribution for an explicitly selected owned process.
 * Profile interrupts sample RUNNING threads, not a time-weighted inventory
 * of all sleeping threads. HVF samples can attribute guest execution only to
 * its host vCPU-run boundary. This is NOT GPU time, presented FPS, or a ratio
 * comparable across differently loaded/virtualized/native workloads.
 */
#pragma D option quiet
#pragma D option aggsize=16m
BEGIN { began=timestamp;printf("READY on-CPU user stacks target=%d\n",$target); }
profile-499 /pid==$target/ { @stacks[ustack(24)]=count();@samples=count(); }
tick-1s /timestamp-began>=16000000000/ { exit(0); }
END {printf("ONCPU samples and stacks\n");printa(@samples);printa(@stacks);}
