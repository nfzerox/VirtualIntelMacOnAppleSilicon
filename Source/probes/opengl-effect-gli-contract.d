/* Bounded metadata-only GLI contract survey. No structure offsets, pixels,
 * setting changes or hot-path logging in the production renderer. Sample
 * Dock AND WindowServer separately: cached compositor textures do not prove
 * the client generating an effect made no private calls or errors. */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN { printf("READY effect GLI contract pid=%d\n",$target); }
pid$target:GLEngine:VIMDispatch_flush:entry,
pid$target:GLEngine:VIMDispatch_finish:entry,
pid$target:GLEngine:VIMDispatch_draw_arrays:entry,
pid$target:GLEngine:VIMDispatch_draw_elements:entry,
pid$target:GLEngine:VIMDispatch_get_error:entry
{ @activity[probefunc]=count(); }
pid$target:GLEngine:gliSetInteger:entry
{ @keys[arg1]=count(); }
pid$target:GLEngine:gliSetInteger:entry
/(arg1==0x3e5 || arg1==0x3e7) && arg2/
{ this->a=(uint32_t *)copyin(arg2,20);@imports[arg1,this->a[0],this->a[2],this->a[3],this->a[4]]=count(); }
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
{ @attachments[arg1]=count(); }
/* The exported attach/set functions have optimized tail jumps: their pid
 * return probe can run BEFORE the callee and report a stack-canary/pointer,
 * not CGLError. Disassembly of the qualified research engine verifies this
 * helper instead has an actual ret after assigning the 32-bit result. */
pid$target:GLEngine:VIMCreateAcceleratorDrawable:return
{ @accelerator_results[(uint32_t)arg1]=count(); }
pid$target:GLEngine:VIMDispatch_get_error:return
/(uint32_t)arg1/
{ @gl_errors[(uint32_t)arg1,ustack(5)]=count(); }
tick-12s { exit(0); }
END {
    printf("activity (require actual calls before interpreting empty errors)\n");printa(@activity);
    printf("set keys\n");printa(@keys);printf("private import key surface format width height\n");printa(@imports);
    printf("attachment types\n");printa(@attachments);
    printf("accelerator creation actual results\n");printa(@accelerator_results);
    printf("queried GL errors\n");printa(@gl_errors);
}
