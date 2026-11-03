/* Cold native-CGL renderer transitions. CGL's single engine retains its own
 * objects/state; never translate private guest/host attribute blobs. Normal
 * drawing adds no native query, wait, thread or polling operation here. */
#include "direct-opcodes.h"
static void DGNativeRendererRestoreDrawable(struct DGHostContext *c)
{
    if(!c->native_drawable_deferred)return;
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c->native_deferred_draw);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c->native_deferred_read);
    c->native_drawable_deferred=0;
}
/* Compiler-only recovery must not realize an unused IOSurface render target
 * in Float. Keep native FBO0 only for commands independent of framebuffer
 * contents; the first other command restores the exact prior bindings.
 * Procedure IDs come from the same canonical wire as the interpreter. */
static int DGNativeRendererPrepareCommand(struct DGHostContext *c,const struct DGMessage *m)
{
    switch(m->op){
    case DG_NATIVE_RENDERERS:case DG_SELECT_NATIVE_RENDERER:case DG_STRING:
    case DG_ERROR:case DG_ERROR_STATE:case DG_COMMON_STATE:case DG_PROGRAM_STRING:
    case DG_FLUSH_DRAWABLE_STATE:case DG_FLUSH_IOSURFACE_STATE:case DG_DESTROY:
    case DG_OP_bind_program_ARB:case DG_OP_delete_programs_ARB:case DG_OP_gen_programs_ARB:
    case DG_OP_is_program_ARB:case DG_OP_program_string_ARB:case DG_OP_get_programiv_ARB:
    case DG_OP_flush:case DG_OP_finish:case DG_OP_get_error:return 1;
    /* Texture-object operations do not consume the default framebuffer.
     * Core Image allocates/queries test textures during software recovery;
     * realizing our unused double-buffered IOSurface here makes native Float
     * create invalid backing which can fault much later during deletion.
     * CopyTexImage/SubImage intentionally remain framebuffer-dependent. */
    case DG_OP_gen_textures:case DG_OP_delete_textures:case DG_OP_bind_texture:
    case DG_OP_is_texture:case DG_OP_tex_image1D:case DG_OP_tex_image2D:case DG_OP_tex_image3D:
    case DG_OP_tex_sub_image1D:case DG_OP_tex_sub_image2D:case DG_OP_tex_sub_image3D:
    case DG_OP_compressed_tex_image1D:case DG_OP_compressed_tex_image2D:case DG_OP_compressed_tex_image3D:
    case DG_OP_compressed_tex_sub_image1D:case DG_OP_compressed_tex_sub_image2D:case DG_OP_compressed_tex_sub_image3D:
    case DG_OP_get_tex_image:case DG_OP_get_compressed_tex_image:
    case DG_OP_get_tex_level_parameterfv:case DG_OP_get_tex_level_parameteriv:
    case DG_OP_get_tex_parameterfv:case DG_OP_get_tex_parameteriv:
    case DG_OP_tex_parameterf:case DG_OP_tex_parameterfv:case DG_OP_tex_parameteri:case DG_OP_tex_parameteriv:
    case DG_OP_pixel_storef:case DG_OP_pixel_storei:return 1;
    case DG_OP_get_integerv:
        switch((GLenum)m->args[0]){
        case GL_MAX_TEXTURE_COORDS_ARB:case GL_MAX_TEXTURE_IMAGE_UNITS_ARB:
        case GL_MAX_TEXTURE_SIZE:case GL_MAX_VIEWPORT_DIMS:case GL_PROGRAM_ERROR_POSITION_ARB:
        case GL_READ_FRAMEBUFFER_BINDING_EXT:case GL_DRAW_FRAMEBUFFER_BINDING_EXT:
        case GL_READ_BUFFER:case GL_DRAW_BUFFER:return 1;
        }
    }
    if(c->native_drawable_deferred && getenv("VIM_PASSTHROUGH_OPENGL_RETIRE_TRACE"))
        fprintf(stderr,"DG_RENDERER_REALIZE context=%p op=%u args=%llx,%llx,%llx,%llx\n",
            (void *)c,m->op,m->args[0],m->args[1],m->args[2],m->args[3]);
    DGNativeRendererRestoreDrawable(c);return 1;
}
static int DGNativeRendererState(struct DGHostContext *c,struct DGNativeRendererInfo *out,
    GLint *hardware,GLint *software)
{
    CGLPixelFormatObj format=CGLGetPixelFormat(c->cgl);GLint selected=-1,id=0,accelerated=0;
    if(!format||c->native_screen_count<1||c->native_screen_count>256||
       CGLGetVirtualScreen(c->cgl,&selected)||selected<0||selected>=c->native_screen_count||
       CGLDescribePixelFormat(format,selected,kCGLPFAAccelerated,&accelerated)||
       CGLGetParameter(c->cgl,kCGLCPCurrentRendererID,&id)||!id)return 0;
    GLint first_hardware=-1;*hardware=-1;*software=-1;
    for(GLint i=0;i<c->native_screen_count;++i){
        GLint gpu=0;if(CGLDescribePixelFormat(format,i,kCGLPFAAccelerated,&gpu))return 0;
        if(gpu){if(first_hardware<0)first_hardware=i;
            if(i==c->native_hardware_screen)*hardware=i;
        }else if(c->profile==kCGLOGLPVersion_Legacy&&*software<0)*software=i;
    }
    if(*hardware<0){*hardware=accelerated?selected:first_hardware;c->native_hardware_screen=*hardware;}
    out->flags=(*hardware>=0?DG_NATIVE_HARDWARE:0)|(*software>=0?DG_NATIVE_SOFTWARE:0)|
        (!accelerated?DG_NATIVE_CURRENT_SOFTWARE:0);
    out->renderer=(uint32_t)id;return 1;
}
static int DGNativeRendererCommand(struct DGHostContext *c,const struct DGMessage *m,struct DGReply *reply)
{
    if(m->bytes||m->flags!=DG_REPLY)return -1;
    for(unsigned i=1;i<sizeof(m->args)/sizeof(m->args[0]);++i)if(m->args[i])return -1;
    int select=m->op==DG_SELECT_NATIVE_RENDERER;
    if((!select&&m->args[0])||(select&&m->args[0]>1))return -1;
    struct DGNativeRendererInfo info={0};GLint hardware=-1,software=-1;
    if(!DGNativeRendererState(c,&info,&hardware,&software))return 0;
    if(select){
        GLint wanted=m->args[0]?software:hardware,current=-1;
        if(c->inside_begin||wanted<0||CGLGetVirtualScreen(c->cgl,&current))return 0;
        if(wanted!=current){
            /* Submit the real outgoing producer and its changed alias views.
             * This is a renderer transition, not a new per-frame flush/Finish. */
            if(!c->native_drawable_deferred){
                struct DGMessage flush={0};struct DGReply submitted={0};
                if(DGFlushDrawableState(c,&flush,&submitted)||submitted.error)return 0;
            }
            int deferred=0;
            if(m->args[0]&&c->framebuffer){
                GLint draw=0,read=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
                if(draw==(GLint)c->framebuffer&&read==draw){
                    c->native_deferred_draw=draw;c->native_deferred_read=read;
                    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);c->native_drawable_deferred=1;deferred=1;
                }
            }
            if(CGLSetVirtualScreen(c->cgl,wanted)){
                if(deferred)DGNativeRendererRestoreDrawable(c);
                return 0;
            }
            if(!m->args[0])DGNativeRendererRestoreDrawable(c);
            DGSubmitIOSurfaceWrites(c);
            if(CGLGetVirtualScreen(c->cgl,&current)||current!=wanted)return -1;
            c->alias_bindings_dirty=1;c->iosurface_draw_dirty=1;
            if(!DGNativeRendererState(c,&info,&hardware,&software))return -1;
            if(!!(info.flags&DG_NATIVE_CURRENT_SOFTWARE)!=!!m->args[0])return -1;
        }
    }
    reply->result=(uint64_t)info.flags<<32|info.renderer;return 0;
}
