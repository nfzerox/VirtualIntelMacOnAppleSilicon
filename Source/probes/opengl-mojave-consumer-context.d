/* Bounded attribution of our GLI's synchronous consumer callback. Use real
 * non-inlined symbols from the packaged renderer, not inlined helper names.
 * Only the documented GLI five-word texture-import record is read.
 * A context match is eligibility evidence, not proof of surface alias safety.
 */
#pragma D option quiet
#pragma D option bufsize=1m
BEGIN { budget=12;printf("READY PVG consumer contexts pid=%u\n",$target); }
pid$target:GLEngine*:VIMPresentDrawableKnown:entry { self->depth++; }
pid$target:GLEngine*:VIMPresentDrawableKnown:entry /self->depth==1/
{ self->producer=arg0;@presentations=count(); }
pid$target:GLEngine*:gliSetInteger:entry
/self->producer && (arg1==0x3e5 || arg1==0x3e6 || arg1==0x3e7)/
{
    self->texture_words=(uint32_t *)copyin(arg2,5*sizeof(uint32_t));
    @contexts[arg0==self->producer,arg1]=count();
}
pid$target:GLEngine*:gliSetInteger:entry
/self->producer && budget>0 && (arg1==0x3e5 || arg1==0x3e6 || arg1==0x3e7)/
{
    printf("CONSUMER same-context=%d surface=%u op=%x size=%ux%u\n",
        arg0==self->producer,self->texture_words[0],arg1,
        self->texture_words[3],self->texture_words[4]);
    budget--;
}
pid$target:GLEngine*:CGLTexImageIOSurface2D:entry /self->producer/
{ @surface_contexts[arg0==self->producer]=count(); }
/* Established imports are refreshed at texture/FBO binds; their initial
 * TexImage call need not occur during the measured gesture. The GLI wrapper
 * selects its real context before invoking this non-inlined refresh. */
pid$target:GLEngine*:VIMBindGLI:entry /self->producer/
{ self->bound_context=arg0; }
pid$target:GLEngine*:VIMRefreshSurface:entry /self->producer && self->bound_context/
{ @refresh_contexts[self->bound_context==self->producer]=count(); }
pid$target:GLEngine*:VIMPresentDrawableKnown:return /self->depth==1/
{ self->producer=0;self->texture_words=0; }
pid$target:GLEngine*:VIMPresentDrawableKnown:return /self->depth/ { self->depth--; }
tick-30s { exit(0); }
END {
    printf("END PVG consumer contexts\n");
    printa("PRESENTATIONS count=%@d\n",@presentations);
    printa("GLI same-context=%d operation=%x count=%@d\n",@contexts);
    printa("IOSURFACE same-context=%d count=%@d\n",@surface_contexts);
    printa("REFRESH same-context=%d count=%@d\n",@refresh_contexts);
}
