/* Check whether a real Core Image effect reaches currently unimplemented
 * EXT bulk assembly-program parameters. Trace address-taken dispatch wrappers:
 * VIMUnsupported itself is inlined and has no pid probe in optimized builds.
 * No hits only excludes these calls in the observed workload, not all GL gaps.
 */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN { printf("READY bulk-program-parameter survey\n"); }
pid$target:GLEngine:*VIMDispatch_program_env_parameters4fv_EXT:entry,
pid$target:GLEngine:*VIMDispatch_program_local_parameters4fv_EXT:entry
{ @calls[probefunc,ustack(12)]=count(); }
tick-10sec { exit(0); }
END { printa(@calls); }
