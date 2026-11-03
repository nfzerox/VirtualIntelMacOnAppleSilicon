/* Native Intel 10.8 GL-context API outputs, not guessed structure fields.
 * Hold the physical Mac in integrated mode for the whole trace/workload.
 * Pass WindowServer as -p; only four returned dimensions are retained.
 */
#pragma D option quiet
BEGIN { printf("READY native Intel drawable dimensions\n"); }
fbt:com.apple.driver.AppleIntelHD3000Graphics:*IOIntelGLContext16get_surface_size*:entry
/pid==$target/
{
    self->w=arg1;self->h=arg2;self->bw=arg3;self->bh=arg4;
}
fbt:com.apple.driver.AppleIntelHD3000Graphics:*IOIntelGLContext16get_surface_size*:return
/self->w && self->h && self->bw && self->bh/
{
    @sizes[arg1,*(int32_t *)self->w,*(int32_t *)self->h,
        *(int32_t *)self->bw,*(int32_t *)self->bh]=count();
    self->w=0;self->h=0;self->bw=0;self->bh=0;
}
tick-12sec { exit(0); }
END { printf("status logical_w/h backing_w/h calls\n");printa(@sizes); }
