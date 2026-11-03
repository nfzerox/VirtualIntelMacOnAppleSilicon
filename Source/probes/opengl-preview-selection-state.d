/* Finite real-Preview observation, guest OR physical Intel. Scalar GL state
 * and call stacks only. No image data or process writes. Trace both dispatch
 * seams because native Core Animation calls GLI directly, not public gl stubs.
 */
#pragma D option quiet
#pragma D option bufsize=8m
BEGIN {budget=4096;stacks=12;printf("READY Preview state trace\n");}
pid$target:GLEngine:VIMDispatch_*:entry
/budget>0 && (probefunc=="VIMDispatch_bind_framebuffer_EXT" ||
 probefunc=="VIMDispatch_read_buffer" || probefunc=="VIMDispatch_draw_buffer" ||
 probefunc=="VIMDispatch_copy_tex_sub_image2D" || probefunc=="VIMDispatch_copy_tex_image2D" ||
 probefunc=="VIMDispatch_bind_texture" || probefunc=="VIMDispatch_use_program_object_ARB" ||
 probefunc=="VIMDispatch_blend_func" || probefunc=="VIMDispatch_clear" ||
 probefunc=="VIMDispatch_draw_range_elements" || probefunc=="VIMDispatch_draw_arrays")/
{printf("STATE %s ctx=%p a=%llx b=%llx c=%llx d=%llx e=%llx\n",probefunc,arg0,arg1,arg2,arg3,arg4,arg5);budget--;}
pid$target:GLEngine:VIMDispatch_copy_tex_sub_image2D*:entry,
pid$target:GLEngine:glCopyTexSubImage2D_Exec*:entry,
pid$target:AppleIntelHD5000GraphicsGLDriver:*CopyTexSubImage*:entry
/stacks>0/
{printf("COPY stack %s\n",probefunc);ustack(16);stacks--;}
pid$target:GLEngine:glCopyTexSubImage2D_Exec*:entry,
pid$target:GLEngine:glReadBuffer_Exec*:entry,
pid$target:GLEngine:glDrawBuffer_Exec*:entry,
pid$target:GLEngine:glBlendFunc_Exec*:entry,
pid$target:GLEngine:glBindFramebufferEXT_Exec*:entry
/budget>0/
{printf("NATIVE %s ctx=%p a=%llx b=%llx c=%llx d=%llx e=%llx\n",probefunc,arg0,arg1,arg2,arg3,arg4,arg5);budget--;}
pid$target:AppleIntelHD5000GraphicsGLDriver:*glrIntelBeginPrimitiveBuffer:entry
{@intel_submissions=count();}
pid$target:GLEngine:VIMDispatch_get_error*:return
/arg1!=0 && budget>0/
{printf("ERROR %x\n",arg1);budget--;}
tick-10s {exit(0);}
END {printf("END remaining=%u actual Intel driver submissions\n",budget);printa(@intel_submissions);}
