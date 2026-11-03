/* Thirty-second ordered metadata trace for a real menu/Spotlight open and
 * dismissal. No pixels or guessed Apple structures. Distinguish render-only
 * submission from the actual primary backing publication boundary. */
#pragma D option quiet
#pragma D option bufsize=2m
BEGIN {budget=256;printf("READY Leopard effect boundaries pid=%d\n",$target);}
pid$target:CoreGraphics:CGXGLAccelFinish:entry
/budget>0/
{budget--;printf("%llu CGX_FINISH ctx=%x publish=%x\n",timestamp,arg0,arg1);}
/* These wrappers escape through the native dispatch table, so retain the
 * native C ABI. Internal static helpers can use compiler-private register
 * conventions: do NOT interpret their pid argN values as C arguments. */
pid$target:GLEngine:VIMGLIFlush:entry,
pid$target:GLEngine:VIMGLIFinish:entry,
pid$target:GLEngine:VIMGLIFlushRender:entry,
pid$target:GLEngine:VIMGLIFinishRender:entry
/budget>0/
{budget--;printf("%llu %s ctx=%x\n",timestamp,probefunc,arg0);}
pid$target:GLEngine:VIMPresentDrawableKnown:entry
/budget>0/
{budget--;printf("%llu PRESENT\n",timestamp);}
pid$target:libVIMPassthroughOpenGL.dylib:DGFlushDrawableState:entry
/budget>0/
{budget--;self->draw_fbo=arg1;printf("%llu BACKEND_FLUSH finish=%x\n",timestamp,arg0);}
pid$target:libVIMPassthroughOpenGL.dylib:DGFlushDrawableState:return
/self->draw_fbo/
{printf("%llu BACKEND_RESULT=%d draw_fbo=%x\n",timestamp,(int32_t)arg1,*(uint32_t *)copyin(self->draw_fbo,4));self->draw_fbo=0;}
pid$target:GLEngine:gliSetInteger:entry
/budget>0 && (arg1==0x3e5 || arg1==0x3e6 || arg1==0x3e7 || arg1==0x1f96)/
{budget--;printf("%llu SET ctx=%x key=%x\n",timestamp,arg0,arg1);}
pid$target:GLEngine:VIMDispatch_get_error:return
/budget>0 && (uint32_t)arg1/
{budget--;printf("%llu QUERIED_GL_ERROR=%x\n",timestamp,(uint32_t)arg1);}
tick-30s {exit(0);}
