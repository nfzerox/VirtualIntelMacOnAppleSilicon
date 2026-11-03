/* Run with -c on the finite owned CI fixture. Stacks distinguish effect
 * execution from the fixture's own FBO clear; a renderer string alone is not
 * proof that Core Image actually submitted GPU geometry. */
#pragma D option quiet
pid$target:AppleIntelHD5000GraphicsGLDriver:*glrIntelBeginPrimitiveBuffer:entry
{ @intel[ustack(16)]=count(); }
END { printa(@intel); }
