/* Finite observation of real application GL errors. No state mutation,
 * injection or object-layout assumptions. Both guest client ABIs supported.
 * Run as guest root with -Z -p PID; use for attribution, never FPS timing. */
#pragma D option quiet
#pragma D option bufsize=1m
BEGIN { printf("READY 5-second GL error trace pid=%d\n",$target); }
pid$target:libGL.dylib:glGetError:return
{ @errors[(unsigned)arg1]=count(); }
pid$target:libGL.dylib:glGetError:return
/arg1 && records<12/
{ records++;printf("GL error=%x\n",(unsigned)arg1);ustack(10); }
pid$target:AGL:aglSwapBuffers:return,
pid$target:OpenGL:CGLFlushDrawable:return
{ @presentation[probemod,probefunc,(unsigned)arg1]=count(); }
pid$target:GLEngine:gliAttachDrawableWithOptions:entry
{ self->attach_context=arg0;self->attach_type=arg1; }
pid$target:GLEngine:gliAttachDrawableWithOptions:return
{ @attachments[self->attach_context,self->attach_type,(unsigned)arg1]=count(); }
/* Our source-defined context pointer ties attachment to actual publication.
 * A successful fullscreen chooser/attachment alone does not prove that the
 * application's final pixels are drawn or flushed through that context. */
/* These table callbacks retain the public GLI calling convention. Do not
 * read arg0 from their internal FlushMode helper: optimized i386 code may
 * use a compiler-private register convention for that direct-call helper. */
pid$target:GLEngine:VIMGLIFlush:entry,
pid$target:GLEngine:VIMGLIFinish:entry,
pid$target:GLEngine:VIMGLISwap:entry,
pid$target:GLEngine:VIMGLIFlushRender:entry,
pid$target:GLEngine:VIMGLIFinishRender:entry
{ self->flushing_context=arg0;@flushes[arg0,probefunc]=count(); }
pid$target:GLEngine:VIMGLIFlush:return,
pid$target:GLEngine:VIMGLIFinish:return,
pid$target:GLEngine:VIMGLISwap:return,
pid$target:GLEngine:VIMGLIFlushRender:return,
pid$target:GLEngine:VIMGLIFinishRender:return
{ self->flushing_context=0; }
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawableRect:entry,
pid$target:libVIMPassthroughOpenGL.dylib:DGReadDrawableRectRows:entry
{ @readbacks[self->flushing_context,probefunc]=count(); }
tick-5sec { exit(0); }
END {
    printa("error=%x count=%@d\n",@errors);
    printa("%s %s result=%x count=%@d\n",@presentation);
    printa("context=%p attachment=%x result=%x count=%@d\n",@attachments);
    printa("context=%p %s flushes=%@d\n",@flushes);
    printa("context=%p %s readbacks=%@d\n",@readbacks);
}
