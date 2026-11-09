/* Draw-time client-memory ownership and VBO range validation. Client bytes
 * remain valid until the native draw returns, when OpenGL has consumed them.
 * They are not registered as APPLE retained/client-storage allocations. */
static void DGHostListModeChanged(struct DGHostContext *c)
{
    if(c->profile!=kCGLOGLPVersion_Legacy || c->inside_begin)return;
    GLenum error=glGetError();if(error && !c->error)c->error=error;
    GLint index=0,mode=0;glGetIntegerv(GL_LIST_INDEX,&index);
    if(index)glGetIntegerv(GL_LIST_MODE,&mode);
    c->list_mode=index?(GLenum)mode:0;
}
static int DGBufferRange(GLenum target,GLuint buffer,uint64_t offset,size_t bytes)
{
    if(!glIsBuffer(buffer) || bytes==SIZE_MAX) return 0;
    glBindBuffer(target,buffer);GLint size=0,mapped=0;
    glGetBufferParameteriv(target,GL_BUFFER_SIZE,&size);glGetBufferParameteriv(target,GL_BUFFER_MAPPED,&mapped);
    return !mapped && size>=0 && offset<=(uint64_t)size && bytes<=(uint64_t)size-offset;
}
static int DGIndirectCommandRange(uint64_t offset,size_t bytes)
{
    const GLenum target=0x8f3f; /* DRAW_INDIRECT_BUFFER */
    if((offset&3) || offset>UINTPTR_MAX || !dg_core_api.buffer_parameter64)return 0;
    GLint bound=0,mapped=0;GLint64 size=-1;
    glGetIntegerv(0x8f43,&bound);
    if(!bound)return 0;
    glGetBufferParameteriv(target,GL_BUFFER_MAPPED,&mapped);
    dg_core_api.buffer_parameter64(target,GL_BUFFER_SIZE,&size);
    return !mapped && size>=0 && offset<=(uint64_t)size && bytes<=(uint64_t)size-offset;
}
static GLint DGHostWeightUnits(struct DGHostContext *c)
{
    if(c->profile!=kCGLOGLPVersion_Legacy)return 0;
    if(!c->weight_units && !c->inside_begin){
        GLint units=0;
        if(DGHostHasExtension((const char *)glGetString(GL_EXTENSIONS),"GL_ARB_vertex_blend"))
            glGetIntegerv(GL_MAX_VERTEX_UNITS_ARB,&units);
        c->weight_units=units>0 && units<=(GLint)DG_VERTEX_WEIGHT_MAX?units:-1;
    }
    return c->weight_units>0?c->weight_units:0;
}
static int DGHostWeightArrayValid(struct DGHostContext *c,const struct DGVertexArray *a)
{
    return a->slot!=DG_VERTEX_WEIGHT_SLOT ||
        (a->size<=(unsigned)DGHostWeightUnits(c) && DGWeightType(a->type) && !a->normalized);
}
static int DGHostPointSizes(struct DGHostContext *c)
{
    if(c->profile!=kCGLOGLPVersion_Legacy)return 0;
    if(!c->point_sizes && !c->inside_begin)
        c->point_sizes=DGHostHasExtension((const char *)glGetString(GL_EXTENSIONS),
            "GL_APPLE_vertex_point_size")?1:-1;
    return c->point_sizes>0;
}
static int DGHostArrayProfileValid(struct DGHostContext *c,const struct DGVertexArray *a)
{
    if(a->slot==DG_VERTEX_POINT_SLOT)return DGHostPointSizes(c);
    return DGHostWeightArrayValid(c,a);
}
static void DGHostArrayEnable(struct DGHostContext *c,unsigned slot,int enabled)
{
    GLenum cap=0;
    switch(slot) {
    case 0:cap=GL_VERTEX_ARRAY;break;
    case 1:cap=GL_NORMAL_ARRAY;break;
    case 2:cap=GL_COLOR_ARRAY;break;
    case 3:cap=GL_SECONDARY_COLOR_ARRAY;break;
    case 4:cap=GL_FOG_COORDINATE_ARRAY;break;
    case DG_VERTEX_EDGE_SLOT:cap=GL_EDGE_FLAG_ARRAY;break;
    case DG_VERTEX_INDEX_SLOT:cap=GL_INDEX_ARRAY;break;
    case DG_VERTEX_WEIGHT_SLOT:
        if(c->weight_units<=0 || (!enabled && !c->weight_array_seen))return;
        if(enabled)c->weight_array_seen=1;
        cap=GL_WEIGHT_ARRAY_ARB;break;
    case DG_VERTEX_POINT_SLOT:
        if(c->point_sizes<=0 || (!enabled && !c->point_array_seen))return;
        if(enabled)c->point_array_seen=1;
        cap=GL_POINT_SIZE_ARRAY_APPLE;break;
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
    case DG_VERTEX_EDGE_SLOT:glEdgeFlagPointer(a->stride,pointer);break;
    case DG_VERTEX_INDEX_SLOT:glIndexPointer(a->type,a->stride,pointer);break;
    case DG_VERTEX_WEIGHT_SLOT:glWeightPointerARB(a->size,a->type,a->stride,pointer);break;
    case DG_VERTEX_POINT_SLOT:glPointSizePointerAPPLE(a->type,a->stride,pointer);break;
    default:
        if(a->slot>=8 && a->slot<16) {
            glClientActiveTexture(GL_TEXTURE0+a->slot-8);glTexCoordPointer(a->size,a->type,a->stride,pointer);
        } else if(a->normalized==DG_VERTEX_ARRAY_DOUBLE)
            dg_core_api.vertex_attrib_lpointer(a->slot-16,a->size,a->type,a->stride,pointer);
        else if(a->normalized==DG_VERTEX_ARRAY_INTEGER)
            glVertexAttribIPointerEXT(a->slot-16,a->size,a->type,a->stride,pointer);
        else glVertexAttribPointer(a->slot-16,a->size,a->type,a->normalized,a->stride,pointer);
    }
}
static volatile int dg_array_enable_reuse=1; // same-process qualification only
static void DGHostArrayEnables(struct DGHostContext *c,uint64_t enabled)
{
    unsigned core=c->profile!=kCGLOGLPVersion_Legacy;
    unsigned slots=DG_VERTEX_LEGACY_SLOTS+
        !!(c->point_array_seen || (enabled&DGVertexBit(DG_VERTEX_POINT_SLOT)));
    if(!dg_array_enable_reuse || !c->array_enable_valid || c->inside_begin){
        // Establish the original ordered baseline after unknown native state.
        for(unsigned slot=core?16:0;slot<slots;++slot)DGHostArrayEnable(c,slot,0);
        for(unsigned slot=0;slot<slots;++slot)
            if(enabled&DGVertexBit(slot))DGHostArrayEnable(c,slot,1);
    }else{
        uint64_t removed=c->array_enable_mask&~enabled,added=enabled&~c->array_enable_mask;
        // Intel compatibility position and generic attribute zero can alias. A
        // disable of either must be followed by re-enabling the surviving
        // requested alias, even when its logical mask bit did not change.
        const uint32_t aliases=UINT32_C(1)|(UINT32_C(1)<<16);
        if(!core && (removed&aliases))added|=enabled&aliases;
        for(unsigned slot=core?16:0;slot<slots;++slot)
            if(removed&DGVertexBit(slot))DGHostArrayEnable(c,slot,0);
        for(unsigned slot=0;slot<slots;++slot)
            if(added&DGVertexBit(slot))DGHostArrayEnable(c,slot,1);
    }
    c->array_enable_mask=enabled;
    c->array_enable_valid=dg_array_enable_reuse && !c->inside_begin;
}
static void DGHostReleaseArrayElements(struct DGHostContext *c)
{
    for(unsigned i=0;i<c->element_snapshot_count;++i){
        free(c->element_snapshots[i].data);c->element_snapshots[i].data=NULL;
    }
    c->element_snapshot_count=0;c->element_vbo_limit=0;c->element_vbo_mask=0;c->element_snapshot_error=GL_NO_ERROR;
    c->element_snapshot_first=0;c->element_snapshot_vertices=0;
}
static int DGHostSnapshotArrayElements(struct DGHostContext *c,const struct DGVertexArray *arrays,
    unsigned count,uint32_t first,uint32_t vertices)
{
    c->element_snapshot_first=first;c->element_snapshot_vertices=vertices;
    for(unsigned i=0;i<count;++i){
        const struct DGVertexArray *a=arrays+i;
        size_t bytes=DGVertexBytes(a->type)*a->size;
        if(vertices>SIZE_MAX/bytes)goto exhausted;
        unsigned char *data=malloc((size_t)vertices*bytes);if(!data)goto exhausted;
        c->element_snapshots[i].array=*a;c->element_snapshots[i].data=data;++c->element_snapshot_count;
    }
    /* Map each distinct buffer once, copy just the enabled component bytes,
     * then unmap before executing Begin or recording any application value.
     * Padding and unrelated attributes are not part of the snapshot. */
    for(unsigned i=0;i<count;++i){
        int seen=0;for(unsigned j=0;j<i;++j)if(arrays[j].buffer==arrays[i].buffer)seen=1;
        if(seen)continue;
        glBindBuffer(GL_ARRAY_BUFFER,arrays[i].buffer);
        const unsigned char *source=glMapBuffer(GL_ARRAY_BUFFER,GL_READ_ONLY);
        if(!source){c->element_snapshot_error=glGetError();return 0;}
        for(unsigned j=i;j<count;++j)if(arrays[j].buffer==arrays[i].buffer){
            const struct DGVertexArray *a=arrays+j;size_t bytes=DGVertexBytes(a->type)*a->size;
            size_t stride=a->stride?a->stride:bytes;
            for(uint32_t vertex=0;vertex<vertices;++vertex)
                memcpy(c->element_snapshots[j].data+(size_t)vertex*bytes,
                    source+(size_t)a->pointer+((size_t)first+vertex)*stride,bytes);
        }
        if(!glUnmapBuffer(GL_ARRAY_BUFFER)){c->element_snapshot_error=GL_INVALID_OPERATION;return 0;}
    }
    return 1;
exhausted:
    c->element_snapshot_error=GL_OUT_OF_MEMORY;return 0;
}
static int DGHostPrepareArrayElement(struct DGHostContext *c,struct DGMessage *m)
{
    DGHostReleaseArrayElements(c);
    if(c->profile!=kCGLOGLPVersion_Legacy || c->inside_begin)return 0;
    if(!m->args[0] || m->args[0]>DG_VERTEX_SLOTS || m->args[1]!=1 || m->args[2]>1 ||
       m->args[3]>(uint64_t)INT32_MAX+1 || (m->args[3]&&!m->args[2]) ||
       m->bytes!=m->args[0]*sizeof(struct DGVertexArray))return -1;
    const struct DGVertexArray *arrays=(const void *)(m+1);
    unsigned count=(unsigned)m->args[0];
    uint64_t mask=0;uint32_t limit=UINT32_MAX;
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    GLint binding=0,unit=0;glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&binding);
    glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&unit);
    for(unsigned i=0;i<count;++i){
        const struct DGVertexArray *a=arrays+i;
        size_t bytes=DGVertexBytes(a->type)*a->size;
        if(a->slot>=DG_VERTEX_SLOTS || !DGHostArrayProfileValid(c,a) || !a->size || a->size>4 ||
           a->stride>INT32_MAX || !bytes || !a->buffer || a->bytes || !DGVertexArrayModeValid(a) ||
           a->normalized==DG_VERTEX_ARRAY_DOUBLE || (mask&DGVertexBit(a->slot)) ||
           !glIsBuffer(a->buffer))goto invalid;
        glBindBuffer(GL_ARRAY_BUFFER,a->buffer);
        GLint size=0,mapped=0;glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);
        glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_MAPPED,&mapped);
        if(mapped || size<0 || a->pointer>(uint64_t)size || bytes>(uint64_t)size-a->pointer)goto invalid;
        uint32_t available=1+(uint32_t)(((uint64_t)size-a->pointer-bytes)/(a->stride?a->stride:bytes));
        if(available<limit)limit=available;
        mask|=DGVertexBit(a->slot);
    }
    if((mask&UINT32_C(0x10001))==UINT32_C(0x10001))goto invalid;
    if(m->args[2]){
        uint32_t first=m->args[3]?(uint32_t)(m->args[3]-1):0;
        if(first>=limit || !DGHostSnapshotArrayElements(c,arrays,count,first,m->args[3]?1:limit))goto invalid;
    }else{
        for(unsigned i=0;i<count;++i){
            glBindBuffer(GL_ARRAY_BUFFER,arrays[i].buffer);
            DGHostArrayPointer(arrays+i,(const void *)(uintptr_t)arrays[i].pointer);
        }
        DGHostArrayEnables(c,mask);
    }
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)binding);glClientActiveTexture((GLenum)unit);
    GLenum error=glGetError();
    if(error){
        c->array_enable_valid=0;
    }else{
        c->element_vbo_mask=mask;c->element_vbo_limit=limit;
    }
    return 0;
invalid:
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)binding);
    {GLenum saved=c->element_snapshot_error;DGHostReleaseArrayElements(c);c->element_snapshot_error=saved;}
    /* A normal Begin/End can leave an unrelated mapped/empty VBO enabled.
     * Only a subsequent ArrayElement actually uses the prepared arrays. */
    (void)glGetError();
    return 0;
}
static double DGElementComponent(const unsigned char *p,GLenum type,int normalize)
{
#define DG_ELEMENT_INTEGER(tag,T,maximum,sign) case tag:{ T v;memcpy(&v,p,sizeof(v)); \
    if(!normalize)return (double)v; \
    return sign?fmax(-1.0,(double)v/(((maximum)-1.0)*0.5)):(double)v/(maximum); }
    switch(type){
    DG_ELEMENT_INTEGER(GL_BYTE,int8_t,255.0,1)
    DG_ELEMENT_INTEGER(GL_UNSIGNED_BYTE,uint8_t,255.0,0)
    DG_ELEMENT_INTEGER(GL_SHORT,int16_t,65535.0,1)
    DG_ELEMENT_INTEGER(GL_UNSIGNED_SHORT,uint16_t,65535.0,0)
    DG_ELEMENT_INTEGER(GL_INT,int32_t,4294967295.0,1)
    DG_ELEMENT_INTEGER(GL_UNSIGNED_INT,uint32_t,4294967295.0,0)
    case GL_FLOAT:{GLfloat v;memcpy(&v,p,sizeof(v));return v;}
    case GL_DOUBLE:{GLdouble v;memcpy(&v,p,sizeof(v));return v;}
    case 0x140b:{ /* HALF_FLOAT */
        uint16_t v;memcpy(&v,p,sizeof(v));
        int exponent=(v>>10)&31,frac=v&1023;
        double result=exponent==31?(frac?NAN:INFINITY):
            ldexp((double)(frac+(exponent?1024:0)),(exponent?exponent:1)-25);
        return (v&0x8000)?-result:result;
    }
    default:return 0;
    }
#undef DG_ELEMENT_INTEGER
}
static void DGHostEmitElementValue(const struct DGElementValue *a,int big_endian)
{
    unsigned bytes=DGVertexBytes(a->type);
    _Alignas(GLdouble) unsigned char swapped[32];const unsigned char *data=a->data;
    if(big_endian && bytes>1){
        DGSwapVertexComponents(swapped,data,1,a->size,bytes,0);data=swapped;
    }
    if(a->slot==DG_VERTEX_WEIGHT_SLOT){
        switch(a->type){
        case GL_BYTE:glWeightbvARB(a->size,(const GLbyte *)data);break;
        case GL_UNSIGNED_BYTE:glWeightubvARB(a->size,(const GLubyte *)data);break;
        case GL_SHORT:glWeightsvARB(a->size,(const GLshort *)data);break;
        case GL_UNSIGNED_SHORT:glWeightusvARB(a->size,(const GLushort *)data);break;
        case GL_INT:glWeightivARB(a->size,(const GLint *)data);break;
        case GL_UNSIGNED_INT:glWeightuivARB(a->size,(const GLuint *)data);break;
        case GL_FLOAT:glWeightfvARB(a->size,(const GLfloat *)data);break;
        case GL_DOUBLE:glWeightdvARB(a->size,(const GLdouble *)data);break;
        }
        return;
    }
    GLdouble v[4]={0,0,0,1};
    for(unsigned i=0;i<a->size;++i)
        v[i]=DGElementComponent(data+i*bytes,a->type,a->normalized==1 ||
            a->slot==1 || a->slot==2 || a->slot==3);
    switch(a->slot){
    case 0:glVertex4dv(v);break;
    case 1:glNormal3dv(v);break;
    case 2:glColor4dv(v);break;
    case 3:glSecondaryColor3dv(v);break;
    case 4:glFogCoordd(v[0]);break;
    case DG_VERTEX_EDGE_SLOT:glEdgeFlag(v[0]!=0);break;
    case DG_VERTEX_INDEX_SLOT:glIndexd(v[0]);break;
    case DG_VERTEX_POINT_SLOT:glVertexPointSizefAPPLE((GLfloat)v[0]);break;
    default:
        if(a->slot<16)glMultiTexCoord4dv(GL_TEXTURE0+a->slot-8,v);
        else if(a->normalized==DG_VERTEX_ARRAY_INTEGER){
            if(a->type==GL_UNSIGNED_BYTE || a->type==GL_UNSIGNED_SHORT || a->type==GL_UNSIGNED_INT){
                GLuint u[4]={(GLuint)v[0],(GLuint)v[1],(GLuint)v[2],(GLuint)v[3]};
                glVertexAttribI4uivEXT(a->slot-16,u);
            }else{
                GLint i[4]={(GLint)v[0],(GLint)v[1],(GLint)v[2],(GLint)v[3]};
                glVertexAttribI4ivEXT(a->slot-16,i);
            }
        }else glVertexAttrib4dv(a->slot-16,v);
    }
}
static int DGHostArrayElement(struct DGHostContext *c,struct DGMessage *m)
{
    if(m->args[0]>INT32_MAX || m->args[1]>DG_VERTEX_SLOTS || m->args[3]>1 || m->args[4]>1 ||
       m->args[2]!=(m->args[1]?1u:0u) || m->bytes!=m->args[1]*sizeof(struct DGElementValue))return -1;
    if(c->profile!=kCGLOGLPVersion_Legacy ||
       (m->args[3] && m->args[0]>=c->element_vbo_limit)){
        if(!c->error)c->error=c->element_snapshot_error?c->element_snapshot_error:GL_INVALID_OPERATION;return 0;
    }
    if(m->args[3] && !!c->element_snapshot_count!=!!m->args[4])return -1;
    if(m->args[3] && m->args[4] && (m->args[0]<c->element_snapshot_first ||
       m->args[0]-c->element_snapshot_first>=c->element_snapshot_vertices)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return 0;
    }
    const struct DGElementValue *values=(const void *)(m+1),*vertex=NULL;
    uint64_t mask=m->args[3]?c->element_vbo_mask:0;
    for(unsigned i=0;i<m->args[1];++i){
        const struct DGElementValue *a=values+i;
        struct DGVertexArray shape={.slot=a->slot,.size=a->size,.type=a->type,.normalized=a->normalized};
        if(a->slot>=DG_VERTEX_SLOTS || !DGHostArrayProfileValid(c,&shape) || !a->size || a->size>4 ||
           !DGVertexBytes(a->type) || !DGVertexArrayModeValid(&shape) || a->normalized==DG_VERTEX_ARRAY_DOUBLE ||
           (mask&DGVertexBit(a->slot)))return -1;
        mask|=DGVertexBit(a->slot);
        if(!a->slot || a->slot==16)vertex=a;
    }
    if((mask&UINT32_C(0x10001))==UINT32_C(0x10001))return -1;
    struct DGElementValue buffered_vertex;int has_buffered_vertex=0;
    if(m->args[3] && m->args[4])for(unsigned i=0;i<c->element_snapshot_count;++i){
        const struct DGVertexArray *a=&c->element_snapshots[i].array;
        struct DGElementValue element={.slot=a->slot,.size=a->size,.type=a->type,.normalized=a->normalized};
        size_t bytes=DGVertexBytes(a->type)*a->size;
        memcpy(element.data,c->element_snapshots[i].data+(size_t)(m->args[0]-c->element_snapshot_first)*bytes,bytes);
        if(!a->slot || a->slot==16){buffered_vertex=element;has_buffered_vertex=1;}
        else DGHostEmitElementValue(&element,1);
    }
    for(unsigned i=0;i<m->args[1];++i)
        if(values+i!=vertex)DGHostEmitElementValue(values+i,(int)m->args[4]);
    if(m->args[3] && !m->args[4])glArrayElement((GLint)m->args[0]);
    if(has_buffered_vertex)DGHostEmitElementValue(&buffered_vertex,1);
    if(vertex)DGHostEmitElementValue(vertex,(int)m->args[4]);
    return 0;
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
struct DGNativeDrawList {
    const void **indices;GLint *first_or_base;GLsizei *counts;
    unsigned char *converted;
};
static int DGHostDrawList(struct DGHostContext *c,struct DGMessage *m,unsigned kind,
    GLenum type,GLuint elements,int big_endian,int restart,uint32_t marker,int capture,
    struct DGNativeDrawList *out,uint64_t *vertices)
{
    size_t n=(size_t)m->args[2],total=0,max=0;
    unsigned size=DGVertexBytes(type);
    const struct DGDrawListItem *list=DGHostPointer(m,15,n*sizeof(*list));
    if(!list || ((m->args[15]-1)&7))return -1;
    for(size_t i=0;i<n;++i){
        const struct DGDrawListItem *item=list+i;
        if(item->count<0 || (!item->count && (item->index || item->first_or_base)) ||
           (kind==DG_MULTI_ARRAYS && (item->index || item->first_or_base<0)) ||
           (kind==DG_MULTI_ELEMENTS && item->first_or_base))return -1;
        size_t bytes=(size_t)item->count*size;
        if(bytes>DG_MAX_BYTES-total){if(!c->error)c->error=GL_OUT_OF_MEMORY;return 0;}
        total+=bytes;if(bytes>max)max=bytes;
    }
    out->indices=malloc(n*(sizeof(*out->indices)+sizeof(*out->first_or_base)+sizeof(*out->counts)));
    int swap=big_endian && size>1,retain=(swap || capture) && total;
    if(retain)out->converted=malloc(total);
    unsigned char *scratch=elements && !retain && max?malloc(max):NULL;
    if(!out->indices || (retain && !out->converted) || (elements && !retain && max && !scratch)){
        free(scratch);if(!c->error)c->error=GL_OUT_OF_MEMORY;return 0;
    }
    out->first_or_base=(void *)(out->indices+n);
    out->counts=(void *)(out->first_or_base+n);
    size_t offset=0;int good=1;
    *vertices=0;
    for(size_t i=0;i<n && good;++i){
        const struct DGDrawListItem *item=list+i;
        out->indices[i]=NULL;out->first_or_base[i]=item->first_or_base;out->counts[i]=item->count;
        if(!item->count)continue;
        uint64_t end;
        if(!type)end=(uint64_t)item->first_or_base+item->count;
        else {
            size_t bytes=(size_t)item->count*size;
            const void *data;
            if(elements){
                if(!DGBufferRange(GL_ELEMENT_ARRAY_BUFFER,elements,item->index,bytes)){good=0;break;}
                void *copy=retain?out->converted+offset:scratch;
                glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER,(GLintptr)item->index,bytes,copy);
                GLenum error=glGetError();
                if(error){if(!c->error)c->error=error;good=0;break;}
                data=copy;out->indices[i]=(const void *)(uintptr_t)item->index;
            }else {
                if(!item->index || item->index-1>m->bytes || bytes>m->bytes-(item->index-1)){
                    good=-1;break;
                }
                data=(const unsigned char *)(m+1)+item->index-1;out->indices[i]=data;
            }
            if(retain){
                unsigned char *copy=out->converted+offset;
                if(swap)DGSwapVertexComponents(copy,data,item->count,1,size,0);
                else if(data!=copy)memcpy(copy,data,bytes);
                data=copy;out->indices[i]=copy;offset+=bytes;
            }
            end=DGBaseVertexSpan(data,item->count,size,0,item->first_or_base,restart,marker);
            if(end==UINT64_MAX){good=0;break;}
        }
        if(end>*vertices)*vertices=end;
    }
    if(type && (!elements || retain))glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
    free(scratch);return good;
}
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
    unsigned base_kind=extended?(unsigned)m->args[14]:0;
    int multi=base_kind>=DG_MULTI_ARRAYS && base_kind<=DG_MULTI_BASE;
    int feedback=base_kind==DG_DRAW_FEEDBACK || base_kind==DG_DRAW_FEEDBACK_STREAM;
    int indirect=base_kind==DG_DRAW_ARRAYS_INDIRECT || base_kind==DG_DRAW_ELEMENTS_INDIRECT;
    GLint base=(GLint)m->args[15];
    GLuint start=(GLuint)m->args[7],end=(GLuint)(m->args[7]>>32);
    if(indirect){
        int indexed=base_kind==DG_DRAW_ELEMENTS_INDIRECT;
        if(m->args[14]!=base_kind || first || count || m->args[5] || m->args[7] ||
           m->args[0]>UINT32_MAX || m->args[3]>UINT32_MAX || m->args[4]>UINT32_MAX ||
           big_endian || instanced || (indexed?(!type || !elements):(type || elements)))return -1;
        if(context->profile==kCGLOGLPVersion_Legacy || !dg_core_api.draw_arrays_indirect ||
           !dg_core_api.draw_elements_indirect){
            if(!context->error)context->error=GL_INVALID_OPERATION;return 0;
        }
        /* Never hand a client pointer to host GL. Validate only the native
         * command span, not its contents or an EBO-derived CPU vertex count. */
        if(!DGIndirectCommandRange(m->args[15],indexed?20:16)){
            if(!context->error)context->error=GL_INVALID_OPERATION;return 0;
        }
    }else if(feedback){
        if(m->args[14]!=base_kind || first || count || type || m->args[4] || m->args[5] ||
           m->args[15] || big_endian || instanced || (base_kind==DG_DRAW_FEEDBACK && end))return -1;
        if(context->profile==kCGLOGLPVersion_Legacy || !dg_core_api.draw_feedback || !dg_core_api.draw_feedback_stream){
            if(!context->error)context->error=GL_INVALID_OPERATION;return 0;
        }
    }else if(multi){
        if(m->args[14]!=base_kind || first || instanced || m->args[5] || m->args[7] ||
           !count || count>DG_DRAW_LIST_LIMIT || (base_kind==DG_MULTI_ARRAYS?(type||elements):!type))return -1;
        if(context->inside_begin || !dg_core_api.multi_arrays || !dg_core_api.multi_elements || !dg_core_api.multi_base){
            if(!context->error)context->error=GL_INVALID_OPERATION;return 0;
        }
        GLenum prior=glGetError();if(prior && !context->error)context->error=prior;
    }else if(base_kind){
        if(m->args[14]>DG_BASE_INSTANCED || !type || first ||
            m->args[15]!=(uint64_t)(int64_t)base || instanced!=(base_kind==DG_BASE_INSTANCED) ||
            (base_kind==DG_BASE_RANGE?start>end:m->args[7]!=0))return -1;
        if(context->inside_begin || !dg_core_api.elements_base || !dg_core_api.range_elements_base ||
            !dg_core_api.instanced_elements_base){
            if(!context->error)context->error=GL_INVALID_OPERATION;
            return 0;
        }
        GLenum prior=glGetError();if(prior && !context->error)context->error=prior;
    }else if(m->args[14] || m->args[15] || m->args[7])return -1;
    uint32_t *divisors=extended && n?DGHostPointer(m,10,n*sizeof(*divisors)):NULL;
    if(extended && n && !divisors)return -1;
    if(extended && (!dg_core_api.divisor || !dg_core_api.arrays_instanced || !dg_core_api.elements_instanced)) {
        context->error=GL_INVALID_OPERATION;return 0;
    }
    GLint array_buffer=0,element_buffer=0,client_texture=0;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&array_buffer);glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING,&element_buffer);
    if(indirect && type && elements!=(GLuint)element_buffer){
        if(!context->error)context->error=GL_INVALID_OPERATION;return 0;
    }
    int core=context->profile!=kCGLOGLPVersion_Legacy;
    int list_indices=!core && context->list_mode && type && !instanced &&
        (!base_kind || base_kind==DG_BASE_ELEMENTS || base_kind==DG_BASE_RANGE ||
         base_kind==DG_MULTI_ELEMENTS || base_kind==DG_MULTI_BASE);
    if(!core)glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE,&client_texture);
    int restart=type && !indirect && core && context->primitive_restart;
    GLint marker=0;if(restart)glGetIntegerv(0x8f9e,&marker);
    uint64_t vertices=first+count;const void *indices=NULL;void *index_readback=NULL;int good=1;
    struct DGNativeDrawList list={0};
    void *converted_arrays[DG_VERTEX_SLOTS]={0};size_t converted_bytes=0;
#if defined(__aarch64__)
    const GLfloat gradient_first_texel[4][2]={{.5f,.5f},{.5f,.5f},{.5f,.5f},{.5f,.5f}};
    int gradient_quad=!core&&!context->inside_begin&&!big_endian&&!instanced&&!base_kind&&count==4&&m->args[0]==GL_QUADS;
#endif
    if(multi){
        if(core && type && !elements)good=0;
        else good=DGHostDrawList(context,m,base_kind,type,elements,big_endian,restart,(uint32_t)marker,list_indices,&list,&vertices);
    }else if(type && !indirect) {
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
                    if(!base_kind && !restart)vertices=DGMaxIndex(index_readback,(uint32_t)count,type);
                    else if(base_kind){GLenum error=glGetError();if(error){if(!context->error)context->error=error;good=0;}}
                    indices=(const void *)(uintptr_t)m->args[5];
                }
            }
        } else {
            indices=DGHostPointer(m,5,bytes);
            if(!indices) good=0;else if(!base_kind && !restart)vertices=DGMaxIndex(indices,(uint32_t)count,type);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
        }
        if(good && big_endian && DGVertexBytes(type)>1) {
            if(!index_readback) {
                index_readback=malloc(bytes?bytes:1);
                if(!index_readback)good=0;else memcpy(index_readback,indices,bytes);
            }
            if(good) {
                DGSwapVertexComponents(index_readback,index_readback,count,1,DGVertexBytes(type),0);
                indices=index_readback;if(!base_kind)vertices=DGMaxIndex(indices,(uint32_t)count,type);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
            }
        }
        if(good && (base_kind || restart)){
            vertices=DGBaseVertexSpan(index_readback?index_readback:indices,(uint32_t)count,DGVertexBytes(type),
                                      0,base_kind?base:0,restart,(uint32_t)marker);
            if(vertices==UINT64_MAX)good=0;
        }
    }
    /* Native legacy DrawElements list compilation mishandles wide indices;
     * the owned public control also fails with entirely CPU-backed inputs.
     * Record the same ordered vertices as DrawArrays instead, only while a
     * non-instanced legacy indexed draw is being compiled. EXT_gpu_shader4
     * explicitly leaves gl_VertexID undefined for display-list vertices.
     * Normal draws retain their indices and native vertex IDs unchanged. */
    uint32_t *list_vertex_map=NULL;uint64_t list_vertices=count;
    if(good>0 && list_indices){
        if(multi){list_vertices=0;for(uint64_t i=0;i<count;++i)list_vertices+=(uint32_t)list.counts[i];}
        size_t bytes=DGArrayBytes(list_vertices,sizeof(*list_vertex_map));
        if(bytes==SIZE_MAX || bytes>DG_MAX_BYTES || !(list_vertex_map=malloc(bytes?bytes:1))){
            if(!context->error)context->error=GL_OUT_OF_MEMORY;good=0;
        }else {
            unsigned size=DGVertexBytes(type);uint64_t offset=0,draws=multi?count:1;
            for(uint64_t draw=0;draw<draws;++draw){
                uint32_t length=multi?(uint32_t)list.counts[draw]:(uint32_t)count;
                const unsigned char *data=multi?list.indices[draw]:index_readback?index_readback:indices;
                GLint vertex_base=multi?list.first_or_base[draw]:base_kind?base:0;
                if(multi)list.first_or_base[draw]=(GLint)offset;
                /* DGBaseVertexSpan already checked every sum. Preserve its
                 * uint32 wrap and each draw's independent primitive boundary. */
                for(uint32_t i=0;i<length;++i){
                    uint32_t raw=0;memcpy(&raw,data+(size_t)i*size,size);
                    list_vertex_map[offset++]=(uint32_t)((int64_t)raw+vertex_base);
                }
            }
            converted_bytes=bytes;
        }
    }
    uint64_t enabled=0;
    for(unsigned i=0;good>0 && i<n;++i) {
        struct DGVertexArray *a=&arrays[i];
        uint32_t divisor=divisors?divisors[i]:0;
        int generic=a->slot>=16 && a->slot<DG_VERTEX_LEGACY_SLOTS;
        if(a->slot>=DG_VERTEX_SLOTS || !DGHostArrayProfileValid(context,a) || (core && (!generic || !a->buffer)) ||
           (enabled&DGVertexBit(a->slot)) || !DGVertexArrayModeValid(a) || (divisor && !generic) ||
           (a->normalized==DG_VERTEX_ARRAY_DOUBLE && (!core || !dg_core_api.vertex_attrib_lpointer))) { good=0;break; }
        enabled|=DGVertexBit(a->slot);
        uint64_t attribute_vertices=indirect?0:divisor?(instances?(instances-1)/divisor+1:0):vertices;
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
        if((list_indices || (big_endian && scalar>1)) && attribute_vertices) {
            size_t packed=DGArrayBytes(list_indices?list_vertices:attribute_vertices,(size_t)a->size*scalar);
            if(packed==SIZE_MAX || packed>DG_MAX_BYTES-converted_bytes){good=0;break;}
            void *converted=malloc(packed?packed:1),*raw=NULL;
            if(!converted){good=0;break;}
            if(a->buffer) {
                raw=malloc(span?span:1);
                if(!raw){free(converted);good=0;break;}
                glGetBufferSubData(GL_ARRAY_BUFFER,(GLintptr)a->pointer,(GLsizeiptr)span,raw);
                pointer=raw;
            }
            if(list_indices){
                size_t stride=a->stride?a->stride:a->size*scalar;
                for(uint64_t vertex=0;vertex<list_vertices;++vertex){
                    uint32_t index=divisor?0:list_vertex_map[vertex];
                    void *out=(char *)converted+(size_t)vertex*a->size*scalar;
                    const void *in=(const char *)pointer+(size_t)index*stride;
                    if(big_endian && scalar>1)DGSwapVertexComponents(out,in,1,a->size,scalar,0);
                    else memcpy(out,in,a->size*scalar);
                }
            }else DGSwapVertexComponents(converted,pointer,attribute_vertices,a->size,scalar,a->stride);
            free(raw);converted_arrays[i]=converted;converted_bytes+=packed;
            pointer=converted;interpreted.stride=0;glBindBuffer(GL_ARRAY_BUFFER,0);
        }
#if defined(__aarch64__)
        if(gradient_quad&&vertices==4&&DGNormalizeLegacyGradient(context,a,pointer)){
            pointer=gradient_first_texel;interpreted.stride=0;
        }
#endif
        DGHostArrayPointer(&interpreted,pointer);
        if(extended && generic)dg_core_api.divisor(a->slot-16,divisor);
    }
    if(good>0) {
        DGPrepareDrawableAliases(context,0);
        int submit_atlas=!core && m->args[0]==GL_QUADS && count>=4 && instances &&
            DGBackdropWritesSampledAtlas(context);
        if(!core)DGTraceProgram(context,m,arrays,(unsigned)n);
        if(dg_diagnostics && context->draw_debug_count++<16) {
            GLint viewport[4]={0},scissor[4]={0};glGetIntegerv(GL_VIEWPORT,viewport);glGetIntegerv(GL_SCISSOR_BOX,scissor);
            fprintf(stderr,"direct draw mode=%llx count=%llu index=%x ebo=%u arrays=%09llx vertices=%llu viewport=%d,%d,%d,%d scissor=%d:%d,%d,%d,%d\n",m->args[0],count,type,elements,(unsigned long long)enabled,vertices,
                viewport[0],viewport[1],viewport[2],viewport[3],glIsEnabled(GL_SCISSOR_TEST),scissor[0],scissor[1],scissor[2],scissor[3]);
        }
        /* Compatibility vertex and generic attribute zero can alias. Disable first,
         * then enable the requested set so a later generic disable does not
         * accidentally turn the compatibility position array back off. */
        DGHostArrayEnables(context,enabled);
        if(list_indices && multi)
            dg_core_api.multi_arrays((GLenum)m->args[0],list.first_or_base,list.counts,(GLsizei)count);
        else if(list_indices)glDrawArrays((GLenum)m->args[0],0,(GLsizei)count);
        else if(base_kind==DG_DRAW_ARRAYS_INDIRECT)
            dg_core_api.draw_arrays_indirect((GLenum)m->args[0],(const void *)(uintptr_t)m->args[15]);
        else if(base_kind==DG_DRAW_ELEMENTS_INDIRECT)
            dg_core_api.draw_elements_indirect((GLenum)m->args[0],type,(const void *)(uintptr_t)m->args[15]);
        else if(base_kind==DG_DRAW_FEEDBACK)
            dg_core_api.draw_feedback((GLenum)m->args[0],start);
        else if(base_kind==DG_DRAW_FEEDBACK_STREAM)
            dg_core_api.draw_feedback_stream((GLenum)m->args[0],start,end);
        else if(base_kind==DG_MULTI_ARRAYS)
            dg_core_api.multi_arrays((GLenum)m->args[0],list.first_or_base,list.counts,(GLsizei)count);
        else if(base_kind==DG_MULTI_ELEMENTS)
            dg_core_api.multi_elements((GLenum)m->args[0],list.counts,type,list.indices,(GLsizei)count);
        else if(base_kind==DG_MULTI_BASE)
            dg_core_api.multi_base((GLenum)m->args[0],list.counts,type,list.indices,(GLsizei)count,list.first_or_base);
        else if(base_kind==DG_BASE_ELEMENTS)
            dg_core_api.elements_base((GLenum)m->args[0],(GLsizei)count,type,indices,base);
        else if(base_kind==DG_BASE_RANGE)
            dg_core_api.range_elements_base((GLenum)m->args[0],start,end,(GLsizei)count,type,indices,base);
        else if(base_kind==DG_BASE_INSTANCED)
            dg_core_api.instanced_elements_base((GLenum)m->args[0],(GLsizei)count,type,indices,(GLsizei)instances,base);
        else if(instanced) {
            if(type)dg_core_api.elements_instanced((GLenum)m->args[0],(GLsizei)count,type,indices,(GLsizei)instances);
            else dg_core_api.arrays_instanced((GLenum)m->args[0],(GLint)first,(GLsizei)count,(GLsizei)instances);
        } else if(type) glDrawElements((GLenum)m->args[0],(GLsizei)count,type,indices);
        else glDrawArrays((GLenum)m->args[0],(GLint)first,(GLsizei)count);
        // Native Yosemite ping-pongs disjoint tiles of one rectangle atlas.
        // On the host, submit each recognized producer before the next pass
        // samples its result. A pre-draw flush or another texture barrier did
        // not fix the artifact. This does not wait or copy to CPU memory.
        if(submit_atlas)glFlush();
        if((count || feedback || indirect) && instances) DGMarkDrawableDamage(context);
    } else {
        if(dg_diagnostics && context->draw_debug_count++<32)
            fprintf(stderr,"direct draw validation failed mode=%llx count=%llu type=%x ebo=%u arrays=%llu vertices=%llu\n",m->args[0],count,type,elements,n,vertices);
        if(!context->error)context->error=GL_INVALID_OPERATION;
    }
    glBindBuffer(GL_ARRAY_BUFFER,(GLuint)array_buffer);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,(GLuint)element_buffer);
    if(!core)glClientActiveTexture((GLenum)client_texture);free(index_readback);
    for(unsigned i=0;i<n;++i)free(converted_arrays[i]);
    free(list.indices);free(list.converted);free(list_vertex_map);
    return good<0?-1:0;
}
