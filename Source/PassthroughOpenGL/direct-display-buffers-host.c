/* Included after DGDrawableStorage. Dedicated GPU-backed display resources;
 * no guest CPU mapping or readback. The owner context serializes these calls.
 * This endpoint is not yet advertised by the guest protocol.
 */
#include "display-buffer-state.h"
struct DGDisplayBuffers {
    struct VIMDisplayBufferState state;
    IOSurfaceRef surfaces[VIM_DISPLAY_BUFFERS];
    GLuint textures[VIM_DISPLAY_BUFFERS],fbos[VIM_DISPLAY_BUFFERS];
    int width,height;
};
static void DGDisplayBuffersDestroy(struct DGDisplayBuffers *d)
{
    for(unsigned i=0;i<VIM_DISPLAY_BUFFERS;++i){
        if(d->fbos[i])glDeleteFramebuffersEXT(1,&d->fbos[i]);
        if(d->textures[i])glDeleteTextures(1,&d->textures[i]);
        if(d->surfaces[i])CFRelease(d->surfaces[i]);
    }
    memset(d,0,sizeof(*d));
}
static int DGDisplayBuffersCreate(struct DGHostContext *c,struct DGDisplayBuffers *d,
    int width,int height,uint64_t generation)
{
    if(width<1 || height<1 || width>16384 || height>16384 ||
       (uint64_t)width*height*4*VIM_DISPLAY_BUFFERS>DG_SURFACE_BYTE_LIMIT)return 0;
    GLint read=0,draw=0,texture=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    memset(d,0,sizeof(*d));d->width=width;d->height=height;
    int good=1;size_t total=0;
    for(unsigned i=0;i<VIM_DISPLAY_BUFFERS && good;++i){
        d->surfaces[i]=DGDrawableStorage(c,width,height,&d->textures[i]);
        if(!d->surfaces[i]){good=0;break;}
        size_t bytes=IOSurfaceGetAllocSize(d->surfaces[i]);
        if(bytes>DG_SURFACE_BYTE_LIMIT-total){good=0;break;}
        total+=bytes;
        glGenFramebuffersEXT(1,&d->fbos[i]);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,d->fbos[i]);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB,d->textures[i],0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        good=d->fbos[i] && glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT;
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    if(!good){DGDisplayBuffersDestroy(d);return 0;}
    VIMDisplayBuffersInit(&d->state,generation);return 1;
}
/* Preserve from a completed retained front. This is deliberately an explicit
 * operation: native damage-history/copy-forward will select what to preserve.
 * Never substitute the latest queued image for a completed front implicitly.
 */
static int DGDisplayBuffersCopyFront(struct DGDisplayBuffers *d)
{
    uint32_t from=d->state.front,to=d->state.draw;
    if(from==VIM_DISPLAY_NO_BUFFER || to==VIM_DISPLAY_NO_BUFFER || from==to)return 0;
    GLint read=0,draw=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,d->fbos[from]);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,d->fbos[to]);
    glBlitFramebufferEXT(0,0,d->width,d->height,0,0,d->width,d->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
    if(scissor)glEnable(GL_SCISSOR_TEST);
    return 1;
}
static uint64_t DGDisplayBuffersSubmit(struct DGDisplayBuffers *d,uint32_t *index)
{
    /* Producer submission is not completion. Resource ownership stays queued
     * until the presenter supplies its real completion/release callback. */
    glFlush();return VIMDisplayBuffersSubmit(&d->state,index);
}
