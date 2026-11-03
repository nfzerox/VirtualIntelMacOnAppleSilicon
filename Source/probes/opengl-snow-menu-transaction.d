/* Finite native/guest Snow menu regeneration survey. Trace WindowServer,
 * change and restore its wallpaper after READY. No private Apple offsets,
 * floating-point registers, pixels, injected code or production logging.
 * Arm on the actual CGXUpdateMagicMenuBar call; queued MP GL can execute on
 * another thread after that call returns, so a thread-local predicate alone
 * would incorrectly omit native work. Keep the desktop otherwise idle.
 */
#pragma D option quiet
#pragma D option bufsize=4m
BEGIN {printf("READY Snow menu transaction\n");}
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:entry {menu=1;}
pid$target:GLEngine:*VIMDispatch*:entry,
pid$target:GLEngine:*gl*Exec*:entry,
pid$target:GLEngine:*gliAttachDrawable*:entry,
pid$target:GLEngine:gliSetInteger:entry
/menu && events++<2000/
{printf("%s %x,%x,%x,%x,%x,%x\n",probefunc,arg0,arg1,arg2,arg3,arg4,arg5);}
pid$target:GLEngine:gliSetInteger:entry
/menu && arg1>=0x3e5 && arg1<=0x3e7/
{tracemem(copyin(arg2,20),20);}
pid$target:GLEngine:*VIMDispatch_get_error:return,
pid$target:GLEngine:*glGetError*Exec*:return
/menu && arg1/
{printf("GL_ERROR %s %x\n",probefunc,arg1);}
pid$target:CoreGraphics:CGXBackingStoreDataLock:entry,
pid$target:CoreGraphics:CGXCoreImageDrawImageWithTransform:entry,
pid$target:GLEngine:*VIMPresentDrawableKnown:entry
/menu && events++<2000/
{printf("BOUNDARY %s\n",probefunc);}
tick-20s {exit(0);}
