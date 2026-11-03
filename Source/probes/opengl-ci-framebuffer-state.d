/* Finite owned Core Image fixture only; identical entry-point observations
 * on native Intel Mavericks and the guest. No desktop pixels or global trace.
 * Use dtrace -Z -s FILE -c 'opengl-ci-effect FILTER NEW_DIRECTORY'.
 * Float argument registers are deliberately not interpreted as integers.
 */
#pragma D option quiet
#pragma D option bufsize=4m
pid$target:libGL.dylib:glClear:entry,
pid$target:libGL.dylib:glBindFramebuffer*:entry,
pid$target:libGL.dylib:glBindTexture:entry,
pid$target:libGL.dylib:glScissor:entry,
pid$target:libGL.dylib:glViewport:entry,
pid$target:libGL.dylib:glDrawBuffer:entry,
pid$target:libGL.dylib:glReadBuffer:entry,
pid$target:libGL.dylib:glEnable:entry,
pid$target:libGL.dylib:glDisable:entry,
pid$target:libGL.dylib:glColorMask:entry,
pid$target:libGL.dylib:glPushAttrib:entry,
pid$target:libGL.dylib:glPopAttrib:entry,
pid$target:libGL.dylib:glDrawArrays:entry,
pid$target:libGL.dylib:glBegin:entry
{ printf("%s %x %x %x %x\n",probefunc,arg0,arg1,arg2,arg3); }
pid$target:libVIMPassthroughOpenGL.dylib:glClear:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glBindFramebuffer*:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glScissor:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glViewport:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glDrawBuffer:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glReadBuffer:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glEnable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glDisable:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glColorMask:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glPushAttrib:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glPopAttrib:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glDrawArrays:entry,
pid$target:libVIMPassthroughOpenGL.dylib:glBegin:entry
{ printf("BRIDGE %s %x %x %x %x\n",probefunc,arg0,arg1,arg2,arg3); }
pid$target:libGL.dylib:glClearColor:entry
{ printf("glClearColor\n"); }
pid$target:libGL.dylib:glClear:entry
{ ustack(8); }
pid$target:libVIMPassthroughOpenGL.dylib:glClear:entry
{ ustack(12); }
pid$target:libVIMPassthroughOpenGL.dylib:glBegin:entry,
pid$target:GLEngine:glBegin_Exec:entry
{ ustack(12); }
pid$target:GLEngine:glClear_Exec:entry,
pid$target:GLEngine:glScissor_Exec:entry,
pid$target:GLEngine:glViewport_Exec:entry,
pid$target:GLEngine:glDrawBuffer_Exec:entry,
pid$target:GLEngine:glReadBuffer_Exec:entry,
pid$target:GLEngine:glEnable_Exec:entry,
pid$target:GLEngine:glDisable_Exec:entry,
pid$target:GLEngine:glColorMask_Exec:entry,
pid$target:GLEngine:glPushAttrib_Exec:entry,
pid$target:GLEngine:glPopAttrib_Exec:entry,
pid$target:GLEngine:glBegin_Exec:entry
{ printf("NATIVE %s context=%x args=%x %x %x %x\n",probefunc,arg0,arg1,arg2,arg3,arg4); }
