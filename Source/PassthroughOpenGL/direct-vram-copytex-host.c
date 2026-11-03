/* Fixture-only alternative to the private-context textured publication.
 * CopyTexSubImage may avoid a second native command queue for an unflipped
 * transfer. This is NOT enabled in the normal executor: Intel's coherent
 * CPU-visible storage must be tested, not inferred from GL success/fences.
 * Existing source exclusion and real post-copy completion remain mandatory.
 */
static int dg_vram_copytex;
static int DGVRAMCopyTexPublish(struct DGHostContext *c,const struct DGMessage *m,unsigned count)
{
    GLint texture=0;
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,c->vram->texture);
    const struct DGVRAMRect *rects=(const void *)(m+1);
    for(unsigned i=0;i<count;++i){
        const struct DGVRAMRect *r=rects+i;
        glCopyTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,
            (GLint)m->args[5]+r->x,(GLint)m->args[6]+r->y,
            r->x,r->y,r->width,r->height);
    }
    GLenum error=glGetError();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);
    DGRestoreColorTransfer(&transfer);
    return !error && !glGetError();
}
