/* A CPU-to-GPU snapshot need not wait for the GPU to read guest pages when
 * native GL takes an ordinary (non-client-storage, non-PBO) upload instead.
 * Keep guest CPU exclusion through this call, then native GL owns the input.
 * This is deliberately a bounded CPU-copy tradeoff, NOT zero-copy rendering.
 * GPU-to-CPU publication retains its actual completion fence. Flipped views
 * take an explicit CPU row snapshot, not an unsupported negative GL stride.
 * Sparse patches and core contexts keep the existing coherent-copy route.
 */
static volatile int dg_vram_staged_restore=0; /* Candidate, not yet qualified. */
static volatile uint64_t dg_vram_staged_restores; /* explicit fixture/live experiment only */
static int DGVRAMStagedRestore(struct DGHostContext *c,const struct DGMessage *m)
{
    if(!dg_vram_staged_restore || c->profile!=kCGLOGLPVersion_Legacy ||
       m->op!=DG_VRAM_IMPORT_DRAWABLE || !c->color_storage)return 0;
    struct DGVRAMDrawableBacking *b=c->vram;
    const unsigned char *pixels=(const void *)(uintptr_t)
        ((b->kind?b->alias.address:dg_vram.address)+b->offset+
         m->args[6]*b->pitch+m->args[5]*4);
    unsigned char *flipped=NULL;
    if(m->args[9]){
        size_t row=(size_t)m->args[7]*4,height=(size_t)m->args[8];
        flipped=malloc(row*height);if(!flipped)return 0;
        for(size_t y=0;y<height;++y)memcpy(flipped+y*row,pixels+(height-1-y)*b->pitch,row);
        pixels=flipped;
    }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint texture,buffer;
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&buffer);
    const GLenum keys[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,
        GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ROW_BYTES_APPLE,GL_UNPACK_SWAP_BYTES,
        GL_UNPACK_LSB_FIRST,GL_UNPACK_CLIENT_STORAGE_APPLE};
    const GLint canonical[]={4,flipped?(GLint)m->args[7]:(GLint)(b->pitch/4),0,0,0,0,0,0};
    GLint saved[8];
    for(unsigned i=0;i<8;++i){glGetIntegerv(keys[i],saved+i);glPixelStorei(keys[i],canonical[i]);}
    struct DGColorTransfer transfer;DGCanonicalColorTransfer(c,&transfer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,c->color);
    glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,(GLsizei)m->args[7],(GLsizei)m->args[8],
        GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,pixels);
    free(flipped); // Ordinary native upload has consumed its CPU input.
    GLenum error=glGetError();
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,(GLuint)texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,(GLuint)buffer);
    for(unsigned i=0;i<8;++i)glPixelStorei(keys[i],saved[i]);
    DGRestoreColorTransfer(&transfer);
    GLenum cleanup=glGetError();if(!error)error=cleanup;
    /* Failure can have consumed input or changed a prefix. Never replay it
     * through a different transfer route as though no native work happened. */
    if(!error)__atomic_fetch_add(&dg_vram_staged_restores,1,__ATOMIC_RELAXED);
    return error?-1:1;
}
