/* Actual native PBO state owns offset interpretation and buffer lifetime.
 * Do not bind by a cached name: deleted names can remain bound/retained. */
static uint64_t DGPBOCurrentBindings(void)
{
    GLint pack=0,unpack=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
    return (uint32_t)pack|((uint64_t)(uint32_t)unpack<<32);
}
static void DGNativePixelState(struct DGPixelState *state)
{
    for(int pack=0;pack<2;++pack) {
        glGetIntegerv(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,&state->alignment[pack]);
        glGetIntegerv(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,&state->row_length[pack]);
        glGetIntegerv(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,&state->skip_rows[pack]);
        glGetIntegerv(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,&state->skip_pixels[pack]);
        glGetIntegerv(pack?GL_PACK_ROW_BYTES_APPLE:GL_UNPACK_ROW_BYTES_APPLE,&state->row_bytes[pack]);
    }
    uint64_t bindings=DGPBOCurrentBindings();state->buffers[1]=(uint32_t)bindings;state->buffers[0]=(uint32_t)(bindings>>32);
}
static int DGNativeClientVertexState(struct DGHostContext *c,struct DGClientStackState *state,
                                    const struct DGClientStackStatePoint *extended)
{
    if(state->client_texture>=8 || state->reserved ||
       ((state->enabled&(1u<<DG_VERTEX_WEIGHT_SLOT)) && !DGHostWeightUnits(c)))return -1;
    if(extended && (extended->enabled>1 || extended->reserved || !DGHostPointSizes(c)))return -1;
    unsigned slots=extended?DG_VERTEX_SLOTS:DG_VERTEX_LEGACY_SLOTS;
    for(unsigned i=0;i<slots;++i) {
        const struct DGVertexArray *a=i==DG_VERTEX_POINT_SLOT?&extended->point:&state->arrays[i];
        if(a->slot!=i || a->bytes || (a->size && !DGVertexArrayModeValid(a)) || a->size>4 ||
           (a->normalized==DG_VERTEX_ARRAY_DOUBLE &&
            (c->profile==kCGLOGLPVersion_Legacy || !dg_core_api.vertex_attrib_lpointer)) ||
           (a->size && !DGHostArrayProfileValid(c,a)) ||
           ((i==DG_VERTEX_WEIGHT_SLOT || i==DG_VERTEX_POINT_SLOT) && !a->size &&
            (a->buffer||a->pointer||a->type||a->stride||a->normalized)) ||
           (!a->buffer&&a->pointer) || (a->size&&!DGVertexBytes(a->type)) || a->stride>INT32_MAX)return -1;
    }
    DGBindHostVertexArray(c,state->vertex_array);
    for(unsigned i=0;i<slots;++i) {
        const struct DGVertexArray *a=i==DG_VERTEX_POINT_SLOT?&extended->point:&state->arrays[i];
        if(!a->size)continue;
        // Pointer only records metadata; no array is read or drawn here.
        // A CPU array uses native NULL, never its inaccessible guest address.
        glBindBuffer(GL_ARRAY_BUFFER,a->buffer);DGHostArrayPointer(a,(const void *)(uintptr_t)a->pointer);
    }
    for(unsigned i=0;i<DG_VERTEX_SLOTS;++i)DGHostArrayEnable(c,i,0);
    uint64_t enabled=state->enabled|(extended && extended->enabled?DGVertexBit(DG_VERTEX_POINT_SLOT):0);
    for(unsigned i=0;i<slots;++i)if(enabled&DGVertexBit(i))DGHostArrayEnable(c,i,1);
    glBindBuffer(GL_ARRAY_BUFFER,state->array_buffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,state->element_buffer);
    glClientActiveTexture(GL_TEXTURE0+state->client_texture);return 0;
}
static int DGHostPBOState(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    int point=m->op==DG_PBO_CLIENT_STACK && m->bytes==sizeof(struct DGClientStackStatePoint);
    int vertex=point || (m->op==DG_PBO_CLIENT_STACK && m->bytes==sizeof(struct DGClientStackState));
    if((m->op==DG_PBO_BIND && m->bytes) ||
       (m->op==DG_PBO_CLIENT_STACK && ((m->bytes!=sizeof(struct DGPixelState) &&
          m->bytes!=sizeof(struct DGPixelState3D) && !vertex) || m->args[0]>1)) ||
       (m->op==DG_PBO_DELETE && (m->args[0]>INT32_MAX || m->bytes!=m->args[0]*sizeof(GLuint))))return -1;
    if(vertex && (!m->args[0] || m->args[1]>UINT32_MAX || !(m->args[1]&GL_CLIENT_VERTEX_ARRAY_BIT) || m->flags!=DG_REPLY))return -1;
    if(c->inside_begin) { reply->error=GL_INVALID_OPERATION;return 0; }
    if(vertex && c->profile!=kCGLOGLPVersion_Legacy) { reply->error=GL_INVALID_OPERATION;return 0; }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    if(vertex) {
        int error=DGNativeClientVertexState(c,(void *)(m+1),point?(void *)(m+1):NULL);if(error)return error;
        reply->error=glGetError();if(reply->error)return 0;
    }
    if(m->op==DG_PBO_BIND) {
        if(m->args[0]!=GL_PIXEL_PACK_BUFFER && m->args[0]!=GL_PIXEL_UNPACK_BUFFER)return -1;
        glBindBuffer((GLenum)m->args[0],(GLuint)m->args[1]);
    } else if(m->op==DG_PBO_DELETE) {
        const GLuint *names=m->args[0]?DGHostPointer(m,1,m->bytes):NULL;if(m->args[0] && !names)return -1;
        glDeleteBuffers((GLsizei)m->args[0],names);
    } else {
        c->array_enable_valid=0; // native stack can restore another VAO/enable set
        if(m->args[0])glPushClientAttrib((GLbitfield)m->args[1]);else glPopClientAttrib();
    }
    reply->error=glGetError();if(reply->error)return 0;
    if(m->op==DG_PBO_CLIENT_STACK) {
        DGNativePixelState((void *)(m+1));
        if(m->bytes==sizeof(struct DGPixelState3D) || vertex) {
            struct DGPixelState3D *state=(void *)(m+1);
            for(int pack=0;pack<2;++pack) {
                glGetIntegerv(pack?GL_PACK_IMAGE_HEIGHT:GL_UNPACK_IMAGE_HEIGHT,&state->image_height[pack]);
                glGetIntegerv(pack?GL_PACK_SKIP_IMAGES:GL_UNPACK_SKIP_IMAGES,&state->skip_images[pack]);
            }
        }
    } else reply->result=DGPBOCurrentBindings();
    reply->error=glGetError();return 0;
}
static int DGHostPBOPixels(struct DGHostContext *c,struct DGMessage *m)
{
    int volume=m->op==DG_PBO_PIXEL_3D;
    if(m->bytes || m->args[0]>(volume?1u:4u))return -1;
    if(c->inside_begin) { c->error=GL_INVALID_OPERATION;return 0; }
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    if(DGCompressedProxy((GLenum)m->args[1]) && (!m->args[0]||(!volume&&m->args[0]==3))) {
        GLenum target=(GLenum)m->args[1],format=(GLenum)m->args[9],type=(GLenum)m->args[10];
        GLint level=(GLint)(int64_t)m->args[2],internal=(GLint)(int64_t)m->args[3],border=(GLint)(int64_t)m->args[8];
        GLsizei width=(GLsizei)(int64_t)m->args[6],height=(GLsizei)(int64_t)m->args[7];
        const void *offset=(const void *)(uintptr_t)m->args[11];
        if(volume)glTexImage3D(target,level,internal,width,height,(GLsizei)(int64_t)m->args[12],border,format,type,offset);
        else if(m->args[0]==3)glTexImage1D(target,level,internal,width,border,format,type,offset);
        else glTexImage2D(target,level,internal,width,height,border,format,type,offset);
        return 0;
    }
    int reading=!volume && m->args[0]==2;GLenum target=reading?GL_PIXEL_PACK_BUFFER:GL_PIXEL_UNPACK_BUFFER;
    GLint binding=0,mapped=0,size32=0;GLint64 size=0;
    glGetIntegerv(reading?GL_PIXEL_PACK_BUFFER_BINDING:GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
    if(!binding) { c->error=GL_INVALID_OPERATION;return 0; }
    glGetBufferParameteriv(target,GL_BUFFER_MAPPED,&mapped);
    if(c->profile==kCGLOGLPVersion_Legacy) { glGetBufferParameteriv(target,GL_BUFFER_SIZE,&size32);size=size32; }
    else dg_core_api.buffer_parameter64(target,GL_BUFFER_SIZE,&size);
    struct DGPixelState state={0};DGNativePixelState(&state);GLenum error=glGetError();
    if(error) { c->error=error;return 0; }
    GLsizei width=(GLsizei)(int64_t)m->args[6],height=(GLsizei)(int64_t)m->args[7];
    GLsizei depth=volume?(GLsizei)(int64_t)m->args[12]:1;
    /* Rejected texture calls do not read a buffer. Preserve native list
     * compilation/playback errors instead of raising a codec error now. */
    int invalid_input=!reading && (width<0 || height<0 || depth<0 ||
        !DGRasterPixelBits((GLenum)m->args[9],(GLenum)m->args[10]));
    if(reading && (width<0 || height<0)) { c->error=GL_INVALID_VALUE;return 0; }
    int one_dimension=!volume && m->args[0]>=3;
    if(one_dimension && height!=1)return -1;
    uint64_t span=invalid_input?0:DGPixelSpanWide(width,height,(GLenum)m->args[9],(GLenum)m->args[10],state.row_length[reading],
        state.alignment[reading],state.skip_rows[reading],state.skip_pixels[reading],state.row_bytes[reading]);
    if(one_dimension)span=invalid_input?0:DGPixelSpanWide(width,1,(GLenum)m->args[9],(GLenum)m->args[10],
        0,1,0,state.skip_pixels[0],0);
    if(volume) {
        GLint imageheight=0,skipimages=0;
        glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT,&imageheight);glGetIntegerv(GL_UNPACK_SKIP_IMAGES,&skipimages);
        span=invalid_input?0:DGPixelSpan3DWide(width,height,depth,(GLenum)m->args[9],(GLenum)m->args[10],state.row_length[0],
            state.alignment[0],state.skip_rows[0],state.skip_pixels[0],state.row_bytes[0],imageheight,skipimages);
    }
    if(!invalid_input && (mapped || size<0 || span==UINT64_MAX || m->args[11]>(uint64_t)size || span>(uint64_t)size-m->args[11])) {
        c->error=GL_INVALID_OPERATION;return 0;
    }
    /* Native M4 GL-on-Metal consumes stale PBO bytes after BufferSubData:
     * reproduced without this bridge, while NVIDIA Mavericks is correct.
     * Flush and server-side fences do NOT repair it; native Finish or a
     * client-side fence does. Synchronize only at the first unpack after
     * buffer mutation, never on every upload and never read pixels to CPU.
     * Conservative per-context tracking also covers different bind targets.
     * Cross-context app synchronization remains the application's contract. */
#if defined(__aarch64__) || defined(__arm64__)
    if(!reading && !invalid_input && c->pbo_writes_pending) { glFinish();c->pbo_writes_pending=0; }
#endif
    const void *offset=(const void *)(uintptr_t)m->args[11];
    if(reading && c->profile==kCGLOGLPVersion_Legacy && width && height &&
       m->args[10]==GL_BITMAP && (m->args[9]==GL_STENCIL_INDEX || m->args[9]==GL_COLOR_INDEX)){
        error=DGHostBitmapPBO(c,(GLint)(int64_t)m->args[4],(GLint)(int64_t)m->args[5],
            width,height,(GLenum)m->args[9],m->args[11],(GLuint)binding,&state);
        if(error&&!c->error)c->error=error;
        if(!error)DGPBOWritten(c);return 0;
    }
    if(!invalid_input && !volume && m->args[0]==1 && !DGPrepareScopedTextureCPUWrite(c,(GLenum)m->args[1]))return 0;
    struct DGAutomaticMipmapScope automatic={0};
    if(!reading && !invalid_input){
        automatic=DGBeginAutomaticMipmap((GLenum)m->args[1],(GLint)(int64_t)m->args[2],
            c->profile==kCGLOGLPVersion_Legacy,c->inside_begin,c->list_mode,
            &c->error,0,DGNonzeroMipmapBasePossible(c));
    }
    if(volume && invalid_input){
        if(m->args[0])glTexSubImage3D((GLenum)m->args[1],(GLint)(int64_t)m->args[2],
            (GLint)(int64_t)m->args[4],(GLint)(int64_t)m->args[5],(GLint)(int64_t)m->args[13],width,height,depth,
            (GLenum)m->args[9],(GLenum)m->args[10],offset);
        else glTexImage3D((GLenum)m->args[1],(GLint)(int64_t)m->args[2],(GLint)(int64_t)m->args[3],
            width,height,depth,(GLint)(int64_t)m->args[8],(GLenum)m->args[9],(GLenum)m->args[10],offset);
    }else if(volume)DGHostTexture3D(c,!!m->args[0],(GLenum)m->args[1],(GLint)(int64_t)m->args[2],
        (GLint)(int64_t)m->args[3],(GLint)(int64_t)m->args[4],(GLint)(int64_t)m->args[5],
        (GLint)(int64_t)m->args[13],width,height,depth,(GLint)(int64_t)m->args[8],
        (GLenum)m->args[9],(GLenum)m->args[10],offset,1);
    else if(reading)glReadPixels((GLint)(int64_t)m->args[4],(GLint)(int64_t)m->args[5],width,height,
        (GLenum)m->args[9],(GLenum)m->args[10],(void *)offset);
    else if(m->args[0]==4)glTexSubImage1D((GLenum)m->args[1],(GLint)(int64_t)m->args[2],
        (GLint)(int64_t)m->args[4],width,(GLenum)m->args[9],(GLenum)m->args[10],offset);
    else if(m->args[0]==3)glTexImage1D((GLenum)m->args[1],(GLint)(int64_t)m->args[2],
        (GLint)(int64_t)m->args[3],width,(GLint)(int64_t)m->args[8],(GLenum)m->args[9],(GLenum)m->args[10],offset);
    else if(m->args[0]==1)glTexSubImage2D((GLenum)m->args[1],(GLint)(int64_t)m->args[2],(GLint)(int64_t)m->args[4],
        (GLint)(int64_t)m->args[5],width,height,(GLenum)m->args[9],(GLenum)m->args[10],offset);
    else {
        GLuint alias=invalid_input?0:DGBeginAliasRedefinition(c,(GLenum)m->args[1],(GLint)(int64_t)m->args[2]);
        glTexImage2D((GLenum)m->args[1],(GLint)(int64_t)m->args[2],(GLint)(int64_t)m->args[3],width,height,
            (GLint)(int64_t)m->args[8],(GLenum)m->args[9],(GLenum)m->args[10],offset);
        DGEndAliasRedefinition(c,alias);
    }
    error=glGetError();if(error && !c->error)c->error=error;
    DGEndAutomaticMipmap(&automatic,NULL,&c->error);
    if(reading && !error)DGPBOWritten(c);return 0;
}
