/* Finite native/guest WebContent context-creation survey. No pixel reads,
 * forced return values, renderer spoofing or browser-policy changes. */
#pragma D option quiet
#pragma D option dynvarsize=4m
#pragma D option bufsize=4m
BEGIN { printf("READY WebGL context survey\n"); }
#ifdef VIM_WEBKIT_9537_78_2
/* Enable ONLY after checking WebCore x64 UUID
 * 56C3D4BF-2495-3FD2-8212-91AF7DF693B8. Actual getContext disassembly:
 * at +0xf6, RAX is Document::settings(); byte e9 bit 1 = webGLEnabled,
 * byte dc bit 2 = acceleratedCompositingEnabled. Read-only, never bypass. */
pid$target:WebCore:*Core??HTMLCanvasElement*getContext*:f6
{
    this->settings=uregs[R_RAX];
    this->webgl=*(uint8_t *)copyin(this->settings+0xe9,1);
    this->compositing=*(uint8_t *)copyin(this->settings+0xdc,1);
    printf("canvas settings webgl=%u accelerated=%u\n",
        (this->webgl>>1)&1,(this->compositing>>2)&1);
}
#endif
pid$target:OpenGL:CGLChoosePixelFormat:entry
{ self->choose=1; printf("CGL choose\n"); }
pid$target:OpenGL:CGLChoosePixelFormat:return
/self->choose/
{ printf("CGL choose result=%d\n",(int)arg1);self->choose=0; }
pid$target:GLEngine:*gliChoosePixelFormat:entry
{ self->gliChoose=1;printf("GLI choose\n"); }
pid$target:GLEngine:*gliChoosePixelFormat:return
/self->gliChoose/
{ printf("GLI choose result=%d\n",(int)arg1);self->gliChoose=0; }
pid$target:OpenGL:CGLCreateContext:entry
{ self->create=1;printf("CGL create\n"); }
pid$target:OpenGL:CGLCreateContext:return
/self->create/
{ printf("CGL create result=%d\n",(int)arg1);self->create=0; }
pid$target:GLEngine:*gliCreateContext:entry,
pid$target:GLEngine:*gliCreateContextWithShared:entry
{ printf("GLI create flags=%x\n",(unsigned)arg5); }
pid$target:GLEngine:*gliCreateContext:return,
pid$target:GLEngine:*gliCreateContextWithShared:return
{ printf("GLI create result=%d\n",(int)arg1); }
pid$target:WebCore:*GraphicsContext3D*create*Attributes*:entry
{ printf("WebCore create\n"); }
tick-15sec { exit(0); }
