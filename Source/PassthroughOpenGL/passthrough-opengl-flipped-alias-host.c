/* Ordinary CGL drawable pixels and private CGS texture coordinates have
 * opposite row origins. Keep a bounded native IOSurface view, refreshed by
 * ordered GPU blits, rather than reimporting the stale guest CPU shadow.
 * This is independent of Finder, ImageKit, WindowServer and shader contents.
 * No CPU wait/readback, pixel IPC, polling or per-unrelated-draw GL query.
 */
static int DGCopyFlippedAlias(struct DGHostContext *c,struct DGAlias *alias)
{
    int back=c->back_storage && alias->source_storage==IOSurfaceGetID(c->back_storage);
    if(!c->drawable || !c->color_storage || !alias->owned_storage ||
       (!back && alias->source_storage!=IOSurfaceGetID(c->color_storage)) ||
       alias->width>c->drawable->width || alias->height>c->drawable->height)return 0;
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint read,draw,source_read;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST),srgb=glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT);
    if(!c->alias_framebuffer)glGenFramebuffersEXT(1,&c->alias_framebuffer);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c->framebuffer);
    glGetIntegerv(GL_READ_BUFFER,&source_read);glReadBuffer(back?GL_COLOR_ATTACHMENT1_EXT:GL_COLOR_ATTACHMENT0_EXT);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c->alias_framebuffer);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB,alias->texture,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    int good=c->alias_framebuffer &&
        glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    DGSetBlitScissor(c,GL_FALSE);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
    // Native Intel cropped private views start at the full drawable's
    // upper row, not at the upper row of a bottom-left subrectangle.
    if(good)glBlitFramebufferEXT(0,c->drawable->height-alias->height,alias->width,c->drawable->height,
        0,alias->height,alias->width,0,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    // The scratch FBO must not extend deleted/redefined guest texture lifetime.
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB,0,0);
    glReadBuffer((GLenum)source_read);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)draw);
    if(scissor)DGSetBlitScissor(c,GL_TRUE);if(srgb)glEnable(GL_FRAMEBUFFER_SRGB_EXT);
    GLenum error=glGetError();if(error){if(!c->error)c->error=error;good=0;}
    if(good)alias->copied_serial=c->drawable->write_serial;
    return good;
}
static void DGRefreshFlippedAliases(struct DGHostContext *c,int sampled_only)
{
    if(c->inside_begin || !c->drawable || !c->color_storage || !c->alias_group)return;
    GLuint source=IOSurfaceGetID(c->color_storage);
    GLuint back=c->back_storage?IOSurfaceGetID(c->back_storage):0;
    for(struct DGAlias *alias=c->alias_group->aliases;alias;alias=alias->next){
        if((alias->source_storage!=source && (!back || alias->source_storage!=back)) ||
           alias->copied_serial==c->drawable->write_serial)continue;
        int selected=!sampled_only;
        for(GLint unit=0;!selected && unit<c->texture_units;++unit)
            selected=c->sampled_textures[unit]==alias->texture;
        if(selected && !DGCopyFlippedAlias(c,alias) && !c->error)c->error=GL_INVALID_OPERATION;
    }
}
static int DGBindFlippedAlias(struct DGHostContext *c,GLuint texture,GLenum internal,
    int width,int height,int back,struct DGReply *reply)
{
    struct DGAliasGroup *g=c->alias_group;
    if(!g || width>c->drawable->width || height>c->drawable->height ||
       (back?(!c->double_buffer || !c->back_storage):c->double_buffer)){
        reply->error=GL_INVALID_OPERATION;return 0;
    }
    struct DGAlias *alias=g->aliases;while(alias && alias->texture!=texture)alias=alias->next;
    IOSurfaceRef storage=back?c->back_storage:c->color_storage;
    GLuint source=IOSurfaceGetID(storage);
    if(alias && alias->source_storage==source && alias->width==width && alias->height==height &&
       alias->internal==internal){
        if(!DGCopyFlippedAlias(c,alias))reply->error=GL_INVALID_OPERATION;
        return 0;
    }
    size_t estimate=((size_t)width+127)/128*128*((size_t)height+127)/128*128*4;
    if(estimate>DG_MAX_BYTES || estimate>DG_IOSURFACE_GROUP_BYTES-g->owned_surface_bytes ||
       (!(c->client_features&DG_FEATURE_LARGE_IOSURFACE_WRITES)&&g->owned_surface_count>=DG_IOSURFACE_INLINE_WRITES)){
        reply->error=GL_OUT_OF_MEMORY;return 0;
    }
    GLuint temporary=0;IOSurfaceRef surface=DGDrawableStorage(c,width,height,&temporary);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    if(!surface || IOSurfaceGetAllocSize(surface)>DG_IOSURFACE_GROUP_BYTES-g->owned_surface_bytes){
        if(surface)CFRelease(surface);if(temporary)glDeleteTextures(1,&temporary);
        reply->error=GL_OUT_OF_MEMORY;return 0;
    }
    CGLError result=CGLTexImageIOSurface2D(c->cgl,GL_TEXTURE_RECTANGLE_ARB,internal,width,height,
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,surface,0);
    if(temporary)glDeleteTextures(1,&temporary);
    GLenum error=glGetError();
    if(result || error || !DGRegisterAlias(c,texture,surface)){
        CFRelease(surface);reply->error=error?error:GL_INVALID_OPERATION;return 0;
    }
    alias=g->aliases;while(alias->texture!=texture)alias=alias->next;
    alias->owned_storage=surface;g->owned_surface_bytes+=IOSurfaceGetAllocSize(surface);++g->owned_surface_count;
    alias->producer_storage=(IOSurfaceRef)CFRetain(storage);
    alias->source_storage=source;alias->width=width;alias->height=height;alias->internal=internal;
    if(!DGCopyFlippedAlias(c,alias))reply->error=GL_INVALID_OPERATION;
    return 0;
}
