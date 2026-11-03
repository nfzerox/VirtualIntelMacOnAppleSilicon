/* Finite actual Preview GLI observation. No images, host tracing or writes.
 * Run dtrace -Z -p PREVIEW_PID -s FILE, then make one real image selection.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN {budget=1024;stacks=8;printf("READY Preview selection GLI\n");}
pid$target:GLEngine:VIMDispatch_*:entry
/budget>0/
{
    printf("GLI %s ctx=%p a=%llx b=%llx c=%llx d=%llx\n",probefunc,arg0,arg1,arg2,arg3,arg4);
    budget--;
}
pid$target:GLEngine:VIMDispatch_draw_arrays:entry,
pid$target:GLEngine:VIMDispatch_draw_elements:entry,
pid$target:GLEngine:VIMDispatch_begin:entry,
pid$target:GLEngine:VIMDispatch_clear:entry
/stacks>0/
{ustack(12);stacks--;}
tick-10s {exit(0);}
END {printf("END Preview selection remaining=%u\n",budget);}
