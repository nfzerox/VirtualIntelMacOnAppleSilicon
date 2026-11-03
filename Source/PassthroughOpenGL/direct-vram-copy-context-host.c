/* GPU-only copy in a private shared context. Intel's framebuffer-blit path
 * can leave CPU-visible client storage stale despite a signalled fence;
 * the physical/current Intel qualification distinguishes that from this
 * textured-draw path. Do not borrow the application's program/matrix stacks.
 * The caller still owes its CPU ownership fence AFTER our cross-context wait.
 */
/* Wait for the copy's own fence, eliminating a second producer-queue round
 * trip while retaining CPU exclusion through real completion. Qualified on
 * both native Intel GPUs. The process-local switch is only for research A/B;
 * no environment lookup, polling or timing instrumentation in this path. */
static volatile int dg_vram_direct_copy_wait=1;
static int DGVRAMCopyDrawable(struct DGHostContext *c,const struct DGMessage *m,unsigned count,int *completed)
{
    if(completed)*completed=0;
    struct DGVRAMDrawableBacking *b=c->vram;
    int publish=m->op==DG_VRAM_PUBLISH_DRAWABLE,flip=(int)m->args[9];
    int x=(int)m->args[5],y=(int)m->args[6],w=(int)m->args[7],h=(int)m->args[8];
    int dw=publish?(int)(b->pitch/4):w,dh=publish?(int)b->height:h;
    GLuint texture=publish?c->color:b->texture,destination=publish?b->texture:c->color;
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLsync producer=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if(!producer)return 0;
    glFlush();
    if(!c->vram_copy_context && CGLCreateContext(CGLGetPixelFormat(c->cgl),c->cgl,&c->vram_copy_context)){
        glDeleteSync(producer);return 0;
    }
    if(CGLSetCurrentContext(c->vram_copy_context)){glDeleteSync(producer);return 0;}
    glWaitSync(producer,0,GL_TIMEOUT_IGNORED);glDeleteSync(producer);
    if(!c->vram_copy_framebuffer){
        glGenFramebuffersEXT(1,&c->vram_copy_framebuffer);
        glEnable(GL_TEXTURE_RECTANGLE_ARB);glDisable(GL_DITHER);
        glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    }
    int good=c->vram_copy_framebuffer!=0;
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c->vram_copy_framebuffer);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,destination,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    good=glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT && good;
    if(good){
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
        /* No public command executes in this private context. Reinstalling
         * the identical viewport/matrices on every clip publication dirties
         * native transform state without changing any pixels. Recompute on
         * actual backing geometry changes, including non-publish copies. */
        if(!dg_vram_private_copy_state || c->vram_copy_width!=dw || c->vram_copy_height!=dh){
            glViewport(0,0,dw,dh);
            glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,dw,0,dh,-1,1);
            glMatrixMode(GL_MODELVIEW);glLoadIdentity();
            c->vram_copy_width=dw;c->vram_copy_height=dh;
        }
        const struct DGVRAMRect *rects=(const void *)(m+1);
        glBegin(GL_QUADS);
        for(unsigned i=0;i<(publish?count:1);++i){
            int sx0=x,sx1=x+w,sy0=y,sy1=y+h,dx0=0,dx1=w,dy0=flip?h:0,dy1=flip?0:h;
            if(publish){
                const struct DGVRAMRect *r=rects+i;sx0=r->x;sx1=r->x+r->width;
                sy0=flip?h-r->y:r->y;sy1=flip?h-r->y-r->height:r->y+r->height;
                dx0=x+r->x;dx1=dx0+r->width;dy0=y+r->y;dy1=dy0+r->height;
            }
            glTexCoord2i(sx0,sy0);glVertex2i(dx0,dy0);glTexCoord2i(sx1,sy0);glVertex2i(dx1,dy0);
            glTexCoord2i(sx1,sy1);glVertex2i(dx1,dy1);glTexCoord2i(sx0,sy1);glVertex2i(dx0,dy1);
        }
        glEnd();
    }
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
    GLsync copied=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
    good=glGetError()==GL_NO_ERROR && good;
    int waited=0;
    if(copied && good && completed && dg_vram_direct_copy_wait){
        /* This fence belongs to the queue which actually writes CPU-visible
         * bytes. Do not replace completion with a server wait/socket ACK.
         * Keep both contexts and the caller's CPU lock until it has signalled.
         */
        waited=DGVRAMWaitForFence(copied);
        if(!waited){glFinish();good=0;}
    }
    // Even failure may have queued a prefix. If a cross-context completion
    // fence cannot be used, drain HERE before releasing the CPU source/target.
    if(!copied || !good)glFinish();
    if(CGLSetCurrentContext(c->cgl)){
        glFinish();if(copied)glDeleteSync(copied);return 0;
    }
    if(copied){
        if(!waited)glWaitSync(copied,0,GL_TIMEOUT_IGNORED);
        if(glGetError()){
            // A failed GPU dependency must not release CPU ownership while
            // the private context still writes the shared allocation.
            if(!CGLSetCurrentContext(c->vram_copy_context)){
                glFinish();CGLSetCurrentContext(c->cgl);
            }
            good=0;
        }
        glDeleteSync(copied);
    }else good=0;
    good=glGetError()==GL_NO_ERROR && good;
    if(completed && good)*completed=waited;
    return good;
}
