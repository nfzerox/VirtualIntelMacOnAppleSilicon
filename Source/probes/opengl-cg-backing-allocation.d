/* Finite, read-only allocation-interface survey. Verify the actual period
 * CoreGraphics UUID/symbols first. CGXBackingStoreCreate's width/height/format
 * and CGXCreateShmem's byte count/tag come from actual native disassembly,
 * not guessed object offsets. Wall times are inclusive, not additive CPU
 * time. No pixels, app data, private-structure reads or allocator mutation.
 * Pass the actual WindowServer PID as positional argument1, so dtrace -c
 * may start the real Finder workload AFTER enabling probes without replacing
 * the observed PID with its child. Run on physical Intel AND the guest.
 */
#pragma D option quiet
#pragma D option bufsize=4m
#pragma D option dynvarsize=8m
dtrace:::BEGIN { survey_started=timestamp;printf("READY CG backing allocation target=%d\n",$1); }
pid$1:CoreGraphics:*CGXBackingStoreCreate:entry
{
    ++self->backing_depth;
    self->backing_at[self->backing_depth]=timestamp;
    @backing_geometry[arg1,arg2,arg3]=count();
}
pid$1:CoreGraphics:*CGXCreateShmem:entry
/self->backing_depth/
{
    ++self->shmem_depth;
    self->shmem_at[self->shmem_depth]=timestamp;
    @backing_shmem_calls[arg4]=count();
    @backing_shmem_bytes[arg4]=sum(arg0);
    @backing_shmem_sizes[arg4]=quantize(arg0);
}
pid$1:CoreGraphics:*CGXCreateShmem:return
/self->shmem_depth/
{
    @backing_shmem_results[arg1]=count();
    @backing_shmem_us=quantize((timestamp-self->shmem_at[self->shmem_depth])/1000);
    self->shmem_at[self->shmem_depth]=0;--self->shmem_depth;
}
pid$1:CoreGraphics:*CGXBackingStoreCreate:return
/self->backing_depth/
{
    @backing_create_us=quantize((timestamp-self->backing_at[self->backing_depth])/1000);
    @backing_created[arg1!=0]=count();
    self->backing_at[self->backing_depth]=0;--self->backing_depth;
}
pid$1:CoreGraphics:*allocate_backing_surface:entry
{
    ++self->surface_depth;
    self->surface_at[self->surface_depth]=timestamp;
    @surface_requests=count();
    /* Only the first four actual calls capture stacks; no per-frame log. */
    ++survey_stacks;
}
pid$1:CoreGraphics:*allocate_backing_surface:entry
/survey_stacks<=4/
{ ustack(10); }
pid$1:CoreGraphics:*allocate_backing_surface:return
/self->surface_depth/
{
    @surface_results[arg1]=count();
    @surface_create_us=quantize((timestamp-self->surface_at[self->surface_depth])/1000);
    self->surface_at[self->surface_depth]=0;--self->surface_depth;
}
pid$1:AppleIntelHD5000GraphicsGLDriver:*glrFlushContextToken*:entry
{ @intel_flushes=count(); }
tick-1sec /timestamp-survey_started>=12000000000/ { exit(0); }
dtrace:::END
{
    printf("BACKING_GEOMETRY width height format count\n");printa(@backing_geometry);
    printf("BACKING_SHMEM tag calls bytes\n");printa("%d %@d %@d\n",@backing_shmem_calls,@backing_shmem_bytes);
    printf("BACKING_SHMEM_SIZES\n");printa(@backing_shmem_sizes);
    printf("BACKING_SHMEM_RESULTS\n");printa(@backing_shmem_results);
    printf("BACKING_SHMEM_INCLUSIVE_US\n");printa(@backing_shmem_us);
    printf("BACKING_CREATED success count\n");printa(@backing_created);
    printf("BACKING_CREATE_INCLUSIVE_US\n");printa(@backing_create_us);
    printf("SURFACE_REQUESTS\n");printa(@surface_requests);
    printf("SURFACE_RESULTS\n");printa(@surface_results);
    printf("SURFACE_CREATE_INCLUSIVE_US\n");printa(@surface_create_us);
    printf("ACTUAL_INTEL_DRIVER_FLUSHES\n");printa(@intel_flushes);
}
