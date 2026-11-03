/* Finite event-order survey, not an FPS benchmark. No native argument ABI
 * guesses: native functions are timestamped only. DGSubmit metadata comes
 * from our fixed wire header; no pixels or process pointers are printed.
 * Wait for READY before a held-step input workload. */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { started=timestamp;printf("READY isolated update ordering\n"); }
pid$target:CoreGraphics:dragWindow:entry
{ printf("%llu DRAG\n",(unsigned long long)((timestamp-started)/1000)); }
pid$target:CoreGraphics:CGXUpdateDisplay:entry
{ printf("%llu UPDATE\n",(unsigned long long)((timestamp-started)/1000)); }
pid$target:GLEngine:*VIMGLINotifyCPUSurfaceCopy*:entry
{ printf("%llu CPU-COPY\n",(unsigned long long)((timestamp-started)/1000)); }
pid$target:GLEngine:*VIMPresentDrawable*:entry
{ printf("%llu PRESENT\n",(unsigned long long)((timestamp-started)/1000)); }
pid$target:*libVIMPassthroughOpenGL*:DGSubmit:entry
/ *(uint32_t *)copyin(arg1,4)==0x10039 /
{
    this->m=(uint64_t *)copyin(arg1,144);
    printf("%llu PUBLISH view=%llux%llu epoch=%llu\n",(unsigned long long)((timestamp-started)/1000),
        (unsigned long long)this->m[9],(unsigned long long)this->m[10],(unsigned long long)this->m[14]);
}
tick-14sec { exit(0); }
