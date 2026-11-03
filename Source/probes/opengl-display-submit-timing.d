/* One native queue boundary only, to avoid tracing thousands of GL calls.
 * Finite metadata timestamps/status, no image reads. Wait for READY.
 * This is diagnostic producer timing, not uninstrumented presented FPS.
 */
#pragma D option quiet
BEGIN { printf("READY native display-submit timing\n"); }
pid$target::DGDisplaySubmitSealed:return
{ printf("SUBMIT %llu %llu %d\n",timestamp,(uint64_t)walltimestamp,arg1); }
tick-10sec { exit(0); }
