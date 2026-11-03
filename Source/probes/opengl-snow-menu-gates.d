/* Read the actual Snow compositor's filter decision, not inferred firmware
 * capabilities. Trace WindowServer; change/restore wallpaper after READY.
 * Finite metadata only: no code injection, Apple object offsets or pixels. */
#pragma D option quiet
BEGIN { budget=96;printf("READY Snow menu capability gates\n"); }
pid$target:CoreGraphics:CGXUpdateMagicMenuBar:entry {menu=1;}
pid$target:CoreGraphics:CGXGLDisplayDeviceAccelerator:entry /menu/
{self->filtered=arg1;}
pid$target:CoreGraphics:CGXGLDisplayDeviceAccelerator:return /menu && self->filtered && budget>0/
{
    budget--;printf("ACCELERATOR result=%x filtered=%d\n",arg1,*(uint8_t *)copyin(self->filtered,1));
    self->filtered=0;
}
pid$target:CoreGraphics:CGXBackingStoreAccelerate:return,
pid$target:CoreGraphics:CGXConstructVirtualFramebufferFromBackingStore:return,
pid$target:CoreGraphics:CGXBackingStoreAcceleratable:return
/menu && budget>0/
{budget--;printf("%s result=%x\n",probefunc,arg1);}
pid$target:CoreGraphics:CGXCoreImageDrawImageWithTransform:entry /menu && budget>0/
{budget--;printf("CORE_IMAGE_MENU\n");}
tick-10s {exit(0);}
