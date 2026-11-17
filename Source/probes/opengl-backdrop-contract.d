/* Finite native/guest backdrop-input survey. Use -Z -p WINDOWSERVER_OR_DOCK.
 * Public GLI call arguments only; no private object offsets or image pixels.
 * Run the real stack/sheet operation after READY. Diagnostics are not timing
 * evidence. Both the native Intel and owned guest entry names are accepted.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN { printf("READY backdrop contract pid=%d\n",$target); }
pid$target:GLEngine:gliSetInteger:entry
/arg1==0x3e5 && arg2 && events++<1500/
{
    printf("IMPORT context=%p key=%x\n",arg0,arg1);
    tracemem(copyin(arg2,32),32);
}
pid$target:GLEngine:gliSetInteger:entry
/(arg1==0x3e6 || arg1==0x3e7) && arg2 && events++<1500/
{
    printf("IMPORT context=%p key=%x\n",arg0,arg1);
    tracemem(copyin(arg2,20),20);
}
pid$target:GLEngine:*glViewport_Exec:entry,
pid$target:GLEngine:*VIMDispatch_viewport:entry,
pid$target:GLEngine:*glCopyTexImage2D_Exec:entry,
pid$target:GLEngine:*VIMDispatch_copy_tex_image2D:entry,
pid$target:GLEngine:*glCopyTexSubImage2D_Exec:entry,
pid$target:GLEngine:*VIMDispatch_copy_tex_sub_image2D:entry,
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
/events++<1500/
{
    printf("%s context=%p %x,%x,%x,%x,%x,%x,%x,%x\n",
        probefunc,arg0,arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8);
}
tick-15s { exit(0); }
