/* Finite live WebKit tile handoff survey. Records public/owned entry points
 * and argument metadata only; never pixels, guessed object layouts or GL
 * state mutations. Require real draws/flushes before interpreting silence. */
#pragma D option quiet
#pragma D option aggsize=4m
BEGIN {printf("READY WebKit publication target=%d\n",$target);}
pid$target:GLEngine:VIMGLIFlushMode:entry
{@flushes=count();@flush_callers[ustack(9)]=count();}
pid$target:GLEngine:VIMPublishIOSurfaceWritesLocked:entry
{@publications[arg2]=count();}
pid$target:GLEngine:VIMDispatch_*draw*:entry,
pid$target:GLEngine:VIMDispatch_clear:entry,
pid$target:GLEngine:VIMDispatch_end:entry
{@draws[probefunc]=count();}
pid$target:GLEngine:gliSetInteger:entry
/arg1==300/
{@barriers[ustack(10)]=count();}
pid$target:GLEngine:gliSetInteger:entry
/arg1==0x38e && arg2/
{this->a=(uint32_t *)copyin(arg2,32);@imports[this->a[1],this->a[2],this->a[3],this->a[4]]=count();}
pid$target:GLEngine:VIMDispatch_get_error:return
/(uint32_t)arg1/
{@errors[(uint32_t)arg1]=count();}
tick-15s {exit(0);}
END {printf("DRAWS\n");printa(@draws);printf("FLUSH entries (unused optimized arguments are not inspected)\n");printa(@flushes);
 printf("PUBLISH dirty-import count\n");printa(@publications);printf("BARRIER callers\n");printa(@barriers);
 printf("FLUSH callers\n");printa(@flush_callers);printf("IMPORT target format width height\n");printa(@imports);
 printf("ERRORS\n");printa(@errors);}
