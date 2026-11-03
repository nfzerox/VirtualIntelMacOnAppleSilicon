/* Finite low-rate on-CPU attribution. Unlike sample/task suspension this
 * does not stop WindowServer threads or suspend the VM process. No output
 * during motion, tracing policy change, pixel read or unbounded recorder.
 * Counts are statistical CPU observations, not latency/FPS or GPU time.
 * Use interrupted user PCs rather than unwinding user stacks: Darwin10 can
 * fault while unwinding its idle shared-cache stacks, invalidating a trial.
 */
#pragma D option quiet
#pragma D option aggsize=8m
BEGIN {printf("READY Snow finite on-CPU PCs\n");}
profile-97 /pid==$target && arg1/ {@user[ufunc(arg1)]=count();}
profile-97 /pid==$target && !arg1/ {@kernel[stack(16)]=count();}
tick-12s {exit(0);}
ERROR {printf("INVALID: on-CPU probe error\n");exit(2);}
END {printf("USER samples\n");printa(@user);printf("KERNEL samples\n");printa(@kernel);}
