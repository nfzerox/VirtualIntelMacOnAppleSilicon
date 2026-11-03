/* Finite GLI survey of actual menu-height drawing, on native Intel or guest.
 * Context/viewport arguments are verified in both measured Snow dispatches.
 * Excludes unrelated large compositor views, which exhausted the older
 * transaction probe's budget before the native menu transaction arrived.
 * Use dtrace -Z (the two renderer implementations have different symbols).
 * Trigger a wallpaper change after READY; restore the wallpaper afterward.
 * Pointer parameters are metadata except the bounded five-word import record.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN {budget=4096;printf("READY Snow short-view transaction\n");}
pid$target:GLEngine:*VIMDispatch_viewport:entry,
pid$target:GLEngine:*glViewport_Exec:entry
{shortview[arg0]=(arg3>=320 && arg3<=8192 && arg4>0 && arg4<=64);}
pid$target:GLEngine:*VIMDispatch*:entry,
pid$target:GLEngine:*gl*Exec*:entry,
pid$target:GLEngine:gliSetInteger:entry
/shortview[arg0] && budget>0/
{budget--;printf("%s %x,%x,%x,%x,%x,%x\n",probefunc,arg0,arg1,arg2,arg3,arg4,arg5);}
pid$target:GLEngine:gliSetInteger:entry
/shortview[arg0] && arg1>=0x3e5 && arg1<=0x3e7 && budget>0/
{tracemem(copyin(arg2,20),20);self->importing=1;}
pid$target:GLEngine:gliSetInteger:return /self->importing/
{printf("IMPORT_RESULT %x\n",arg1);self->importing=0;}
tick-15s {exit(0);}
