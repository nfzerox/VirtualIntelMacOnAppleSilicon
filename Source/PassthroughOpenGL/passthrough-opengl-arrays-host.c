/* Draw-time client-memory ownership and VBO range validation. Client bytes
 * remain valid until the native draw returns, when OpenGL has consumed them.
 * They are not registered as APPLE retained/client-storage allocations. */
static int DGBufferRange(GLenum target,GLuint buffer,uint64_t offset,size_t bytes)
{
    if(!glIsBuffer(buffer) || bytes==SIZE_MAX) return 0;
    glBindBuffer(target,buffer);GLint size=0,mapped=0;
    glGetBufferParameteriv(target,GL_BUFFER_SIZE,&size);glGetBufferParameteriv(target,GL_BUFFER_MAPPED,&mapped);
    return !mapped && size>=0 && offset<=(uint64_t)size && bytes<=(uint64_t)size-offset;
}
static void DGHostArrayEnable(unsigned slot,int enabled)
{
    GLenum cap=0;
    switch(slot) {
    case 0:cap=GL_VERTEX_ARRAY;break;
    case 1:cap=GL_NORMAL_ARRAY;break;
    case 2:cap=GL_COLOR_ARRAY;break;
    case 3:cap=GL_SECONDARY_COLOR_ARRAY;break;
    case 4:cap=GL_FOG_COORDINATE_ARRAY;break;
    default:
        if(slot>=8 && slot<16) { glClientActiveTexture(GL_TEXTURE0+slot-8);cap=GL_TEXTURE_COORD_ARRAY; }
        else if(slot>=16 && slot<32) {
            if(enabled) glEnableVertexAttribArray(slot-16);else glDisableVertexAttribArray(slot-16);
            return;
        } else return;
    }
    if(enabled) glEnableClientState(cap);else glDisableClientState(cap);
}
static void DGHostArrayPointer(const struct DGVertexArray *a,const void *pointer)
{
    switch(a->slot) {
    case 0:glVertexPointer(a->size,a->type,a->stride,pointer);break;
    case 1:glNormalPointer(a->type,a->stride,pointer);break;
    case 2:glColorPointer(a->size,a->type,a->stride,pointer);break;
    case 3:glSecondaryColorPointer(a->size,a->type,a->stride,pointer);break;
    case 4:glFogCoordPointer(a->type,a->stride,pointer);break;
    default:
        if(a->slot>=8 && a->slot<16) {
            glClientActiveTexture(GL_TEXTURE0+a->slot-8);glTexCoordPointer(a->size,a->type,a->stride,pointer);
        } else glVertexAttribPointer(a->slot-16,a->size,a->type,a->normalized,a->stride,pointer);
    }
}
static volatile int dg_array_enable_reuse=1; // same-process qualification only
static void DGHostArrayEnables(struct DGHostContext *c,uint32_t enabled)
{
    unsigned core=c->profile!=kCGLOGLPVersion_Legacy;
    if(!dg_array_enable_reuse || !c->array_enable_valid || c->inside_begin){
        // Establish the original ordered baseline after unknown native state.
        for(unsigned slot=core?16:0;slot<DG_VERTEX_SLOTS;++slot)DGHostArrayEnable(slot,0);
        for(unsigned slot=0;slot<DG_VERTEX_SLOTS;++slot)
            if(enabled&(UINT32_C(1)<<slot))DGHostArrayEnable(slot,1);
    }else{
        uint32_t removed=c->array_enable_mask&~enabled,added=enabled&~c->array_enable_mask;
        // Intel compatibility position and generic attribute zero can alias. A
        // disable of either must be followed by re-enabling the surviving
        // requested alias, even when its logical mask bit did not change.
        const uint32_t aliases=UINT32_C(1)|(UINT32_C(1)<<16);
        if(!core && (removed&aliases))added|=enabled&aliases;
        for(unsigned slot=core?16:0;slot<DG_VERTEX_SLOTS;++slot)
            if(removed&(UINT32_C(1)<<slot))DGHostArrayEnable(slot,0);
        for(unsigned slot=0;slot<DG_VERTEX_SLOTS;++slot)
            if(added&(UINT32_C(1)<<slot))DGHostArrayEnable(slot,1);
    }
    c->array_enable_mask=enabled;
    c->array_enable_valid=dg_array_enable_reuse && !c->inside_begin;
}
#if defined(__aarch64__)
static int DGNormalizeLegacyGradient(struct DGHostContext *c,const struct DGVertexArray *a,const void *pointer)
{
    if(a->buffer||a->slot<16||a->type!=GL_FLOAT||a->size!=2||a->normalized)return 0;
    size_t stride=a->stride?(size_t)a->stride:2*sizeof(GLfloat);
    for(unsigned i=0;i<4;++i){
        GLfloat uv[2];memcpy(uv,(const char *)pointer+i*stride,sizeof(uv));
        if(!isnan(uv[0])||uv[1]!=0.5f)return 0;
    }
    // CoreAnimation's degenerate one-row mask gradient supplies NaN in
    // every S coordinate. Measured Iris Pro rectangle sampling selects the
    // first texel; Apple Silicon selects the last, erasing the mask's alpha.
    // Inspect linked state only for this exceptional all-NaN client quad.
    GLint program=0;glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    if(!program)return 0;
    GLint coordinate=glGetAttribLocation((GLuint)program,"vertex_texcoord0");
    GLint second=glGetUniformLocation((GLuint)program,"texture1");
    if(coordinate!=(GLint)a->slot-16||second>=0)return 0;
    GLint sampler=glGetUniformLocation((GLuint)program,"texture0");
    GLint matrix=glGetUniformLocation((GLuint)program,"texmat0");
    if(sampler<0||matrix<0)return 0;
    struct DGAliasSamplers *layout=DGAliasSamplerLayout(c,(GLuint)program);
    if(!layout||layout->count!=1||layout->locations[0]!=sampler)return 0;
    GLfloat transform[16]={0};glGetUniformfv((GLuint)program,matrix,transform);
    if(transform[0]!=1||transform[1]!=1||transform[2]!=0||transform[3]!=0)return 0;
    GLint unit=-1,active=0,texture=0,width=0,height=0,wrap=0;
    glGetUniformiv((GLuint)program,sampler,&unit);
    if(unit<0||unit>=c->texture_units)return 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0+unit);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
    if(texture){
        glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_HEIGHT,&height);
        glGetTexParameteriv(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_WRAP_S,&wrap);
    }
    glActiveTexture((GLenum)active);
    return width==512&&height==1&&wrap==GL_CLAMP_TO_EDGE;
}
#endif
static int DGHostDraw(struct DGHostContext *context,struct DGMessage *m)
{
    if(!DGDrawableWritable(context))return 0;
    uint64_t n=m->args[8],first=m->args[1],count=m->args[2];
    GLenum type=(GLenum)m->args[3];GLuint elements=(GLuint)m->args[4];
    int big_endian=(int)m->args[13];
    if(m->args[13]>1 || (big_endian && context->profile!=kCGLOGLPVersion_Legacy) ||
       n>DG_VERTEX_SLOTS || first>INT32_MAX || count>INT32_MAX ||
       (type && type!=GL_UNSIGNED_BYTE && type!=GL_UNSIGNED_SHORT && type!=GL_UNSIGNED_INT)) return -1;
    struct DGVertexArray *arrays=n ? DGHostPointer(m,6,n*sizeof(*arrays)) : NULL;
    if(n && !arrays) return -1;
    int extended=m->op==DG_DRAW_INSTANCED_ARRAYS,instanced=extended && m->args[12];
    uint64_t instances=extended?m->args[11]:1;
    if(extended && (m->args[12]>1 || instances>INT32_MAX || (!instanced && instances!=1)))return -1;
    uint32_t *divisors=extended && n?DGHostPointer(m,10,n*sizeof(*divisors)):NULL;
    if(extended && n && !divisors)return -1;
    if(extended && (!dg_core_api.divisor || !dg_core_api.arrays_instanced || !dg_core_api.elements_instanced)) {
        context->error=GL_INVALID_OPERATION;return 0;
    }
    GLint array_buffer=0,element_buffer=0,client_texture=0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_buffer);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&element_buffer);
    int core=context->profile!=kCGLOGLPVersion_Legacy;
    if(!core)glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&client_texture);
    uint64_t vertices=first+count;const void *indices=NULL;void *index_readback=NULL;int good=1;
    void *converted_arrays[DG_VERTEX_SLOTS]={0};size_t converted_bytes=0;
#if defined(__aarch64__)
    const GLfloat gradient_first_texel[4][2]={{.5f,.5f},{.5f,.5f},{.5f,.5f},{.5f,.5f}};
    int gradient_quad=!core&&!context->inside_begin&&!big_endian&&!instanced&&count==4&&m->args[0]==GL_QUADS;
#endif
    if(type) {
        size_t bytes=DGArrayBytes((int64_t)count,DGVertexBytes(type));
        if(elements) {
            if(!DGBufferRange(GL_ELEMENT_ARRAY_BUFFER,elements,m->args[5],bytes)) good=0;
            else {
                /* Initial correctness path for EBO min/max. A cached upload
                 * index range can replace this readback after qualification. */
                index_readback=calloc(1,bytes ? bytes : 1);
                if(!index_readback) good=0;
                else {
                    glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,(GLintptr)m->args[5],bytes,index_readback);
                    vertices=DGMaxIndex(index_readback,(uint32_t)count,type);
                    indices=(const void *)(uintptr_t)m->args[5];
                }
            }
        } else {
            indices=DGHostPointer(m,5,bytes);
            if(!indices) good=0;else vertices=DGMaxIndex(indices,(uint32_t)count,type);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
        }
        if(good && big_endian && DGVertexBytes(type)>1) {
            if(!index_readback) {
                index_readback=malloc(bytes?bytes:1);
                if(!index_readback)good=0;else memcpy(index_readback,indices,bytes);
            }
            if(good) {
                DGSwapVertexComponents(index_readback,index_readback,count,1,DGVertexBytes(type),0);
                indices=index_readback;vertices=DGMaxIndex(indices,(uint32_t)count,type);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
            }
        }
    }
    uint32_t enabled=0;
    for(unsigned i=0;good && i<n;++i) {
        struct DGVertexArray *a=&arrays[i];
        uint32_t divisor=divisors?divisors[i]:0;
        if(a->slot>=DG_VERTEX_SLOTS || (a->slot>4 && a->slot<8) || (core && (a->slot<16 || !a->buffer)) ||
           (enabled&(UINT32_C(1)<<a->slot)) || a->normalized>1 || (divisor && a->slot<16)) { good=0;break; }
        enabled|=UINT32_C(1)<<a->slot;
        uint64_t attribute_vertices=divisor?(instances?(instances-1)/divisor+1:0):vertices;
        size_t span=DGVertexSpan(a,attribute_vertices);const void *pointer=NULL;
        if(a->buffer) {
            if(!DGBufferRange(GL_ARRAY_BUFFER,a->buffer,a->pointer,span)) { good=0;break; }
            pointer=(const void *)(uintptr_t)a->pointer;
        } else {
            if(span==SIZE_MAX || span>a->bytes) { good=0;break; }
            uint64_t offset=a->pointer;
            if(!offset || offset-1>m->bytes || a->bytes>m->bytes-(offset-1)) { good=0;break; }
            pointer=(const char *)(m+1)+offset-1;glBindBuffer(GL_ARRAY_BUFFER,0);
        }
        struct DGVertexArray interpreted=*a;
        unsigned scalar=DGVertexBytes(a->type);
        if(big_endian && scalar>1 && attribute_vertices) {
            size_t packed=DGArrayBytes(attribute_vertices,(size_t)a->size*scalar);
            if(packed==SIZE_MAX || packed>DG_MAX_BYTES-converted_bytes){good=0;break;}
            void *converted=malloc(packed?packed:1),*raw=NULL;
            if(!converted){good=0;break;}
            if(a->buffer) {
                raw=malloc(span?span:1);
                if(!raw){free(converted);good=0;break;}
                glGetBufferSubData(GL_ARRAY_BUFFER,(GLintptr)a->pointer,(GLsizeiptr)span,raw);
                pointer=raw;
            }
            DGSwapVertexComponents(converted,pointer,attribute_vertices,a->size,scalar,a->stride);
            free(raw);converted_arrays[i]=converted;converted_bytes+=packed;
            pointer=converted;interpreted.stride=0;glBindBuffer(GL_ARRAY_BUFFER,0);
        }
#if defined(__aarch64__)
        if(gradient_quad&&vertices==4&&DGNormalizeLegacyGradient(context,a,pointer)){
            pointer=gradient_first_texel;interpreted.stride=0;
        }
#endif
        DGHostArrayPointer(&interpreted,pointer);
        if(extended && a->slot>=16)dg_core_api.divisor(a->slot-16,divisor);
    }
    if(good) {
        DGPrepareDrawableAliases(context,0);
        int submit_atlas=!core && m->args[0]==GL_QUADS && count>=4 && instances &&
            DGBackdropWritesSampledAtlas(context);
        if(!core)DGTraceProgram(context,m,arrays,(unsigned)n);
        if(dg_diagnostics && context->draw_debug_count++<16) {
            GLint viewport[4]={0},scissor[4]={0};glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);
            fprintf(stderr,"direct draw mode=%llx count=%llu index=%x ebo=%u arrays=%08x vertices=%llu viewport=%d,%d,%d,%d scissor=%d:%d,%d,%d,%d\n",m->args[0],count,type,elements,enabled,vertices,
                viewport[0],viewport[1],viewport[2],viewport[3],glIsEnabled(GL_SCISSOR_TEST),scissor[0],scissor[1],scissor[2],scissor[3]);
        }
        /* Compatibility vertex and generic attribute zero can alias. Disable first,
         * then enable the requested set so a later generic disable does not
         * accidentally turn the compatibility position array back off. */
        DGHostArrayEnables(context,enabled);
        if(instanced) {
            if(type)dg_core_api.elements_instanced((GLenum)m->args[0],(GLsizei)count,type,indices,(GLsizei)instances);
            else dg_core_api.arrays_instanced((GLenum)m->args[0],(GLint)first,(GLsizei)count,(GLsizei)instances);
        } else if(type) glDrawElements((GLenum)m->args[0],(GLsizei)count,type,indices);
        else glDrawArrays((GLenum)m->args[0],(GLint)first,(GLsizei)count);
        // Native Yosemite ping-pongs disjoint tiles of one rectangle atlas.
        // On the host, submit each recognized producer before the next pass
        // samples its result. A pre-draw flush or another texture barrier did
        // not fix the artifact. This does not wait or copy to CPU memory.
        if(submit_atlas)glFlush();
        if(count && instances) DGMarkDrawableDamage(context);
    } else {
        if(dg_diagnostics && context->draw_debug_count++<32)
            fprintf(stderr,"direct draw validation failed mode=%llx count=%llu type=%x ebo=%u arrays=%llu vertices=%llu\n",m->args[0],count,type,elements,n,vertices);
        context->error=GL_INVALID_OPERATION;
    }
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)array_buffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,(GLuint)element_buffer);
    if(!core)glClientActiveTexture((GLenum)client_texture);free(index_readback);
    for(unsigned i=0;i<n;++i)free(converted_arrays[i]);
    return 0;
}
