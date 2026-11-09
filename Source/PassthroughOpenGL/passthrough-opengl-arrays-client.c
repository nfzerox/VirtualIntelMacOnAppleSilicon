/* Client arrays are captured at draw time, not when Pointer is called.
 * VBO arguments carry offsets only. No guest pointer reaches host OpenGL. */
struct DGClientArray { struct DGVertexArray wire;const void *pointer;int enabled;GLuint divisor; };
struct DGVertexState {
    GLuint name,element_buffer;struct DGClientArray arrays[DG_VERTEX_SLOTS];
    uint64_t enabled_vbo_arrays;
    struct {
        const void *element_pointer,*range_pointer;
        GLenum element_type,storage_hint;
        GLsizei range_length;
        int element_enabled,range_enabled;
    } apple;
    struct DGVertexState *next;
};
static void DGUpdateElementVBO(struct DGVertexState *v,unsigned slot)
{
    uint64_t bit=DGVertexBit(slot);
    if(v->arrays[slot].enabled && v->arrays[slot].wire.buffer)v->enabled_vbo_arrays|=bit;
    else v->enabled_vbo_arrays&=~bit;
}
/* Pointer queries are guest-local state, including VBO byte offsets. A
 * transport payload address is never a valid answer to this public query. */
GLenum DGGetVertexAttribPointer(void *opaque,GLuint index,GLenum pname,void **out)
{
    struct DGContext *c=opaque;
    if(index>=16)return GL_INVALID_VALUE;
    if(pname!=GL_VERTEX_ATTRIB_ARRAY_POINTER_ARB)return GL_INVALID_ENUM;
    if(!c || !out)return GL_INVALID_VALUE;
    *out=c->vertex?(void *)c->vertex->arrays[16+index].pointer:NULL;
    return GL_NO_ERROR;
}
/* Native GL owns stack acceptance/error ordering. Mirror only the logical
 * guest state that the executor cannot restore: its arrays use bounded
 * packet addresses, not the caller's pointers. Pixel-only/zero masks keep a
 * small pairing node, without allocating/copying an entire vertex snapshot. */
struct DGClientAttribFrame {
    struct DGClientAttribFrame *next;
    GLbitfield mask;GLuint array_buffer;int client_texture;
    struct DGVertexState vertex[];
};
static void DGDrawFailure(struct DGContext *c,unsigned reason,unsigned value)
{
    if(!(c->draw_failures&(1u<<reason))) {
        c->draw_failures|=1u<<reason;
        syslog(LOG_NOTICE,"VIM PassthroughOpenGL array failure reason=%u value=%x",reason,value);
        VIMGLStderr("VIM PassthroughOpenGL array failure reason=%u value=%x\n",reason,value);
    }
    c->error=GL_INVALID_OPERATION;
}
static struct DGVertexState *DGVertexState(struct DGContext *c,GLuint name)
{
    for(struct DGVertexState *v=c->vertex_states;v;v=v->next) if(v->name==name) return v;
    struct DGVertexState *v=calloc(1,sizeof(*v));if(!v) { c->error=GL_OUT_OF_MEMORY;return NULL; }
    v->name=name;v->next=c->vertex_states;c->vertex_states=v;return v;
}
static struct DGClientAttribFrame *DGClientAttribCapture(struct DGContext *c,GLbitfield mask)
{
    int vertex=!!(mask&GL_CLIENT_VERTEX_ARRAY_BIT);
    struct DGClientAttribFrame *f=calloc(1,sizeof(*f)+(vertex?sizeof(*f->vertex):0));
    if(!f) { if(!c->error)c->error=GL_OUT_OF_MEMORY;return NULL; }
    f->mask=mask;f->array_buffer=c->array_buffer;f->client_texture=c->client_texture;
    if(vertex&&c->vertex)f->vertex[0]=*c->vertex;
    return f;
}
static int DGClientAttribPreparePop(struct DGContext *c)
{
    struct DGClientAttribFrame *f=c->client_attrib_stack;
    // Any allocation must precede the native pop, so an adapter OOM cannot
    // consume the host frame while retaining the logical guest frame.
    return !f || !(f->mask&GL_CLIENT_VERTEX_ARRAY_BIT) || DGVertexState(c,f->vertex[0].name)!=NULL;
}
static void DGClientAttribRestore(struct DGContext *c)
{
    struct DGClientAttribFrame *f=c->client_attrib_stack;if(!f)return;
    if(f->mask&GL_CLIENT_VERTEX_ARRAY_BIT) {
        struct DGVertexState *v=DGVertexState(c,f->vertex[0].name);
        if(v) {
            v->element_buffer=f->vertex[0].element_buffer;
            v->enabled_vbo_arrays=f->vertex[0].enabled_vbo_arrays;
            v->apple=f->vertex[0].apple;
            memcpy(v->arrays,f->vertex[0].arrays,sizeof(v->arrays));c->vertex=v;
            c->array_buffer=f->array_buffer;c->client_texture=f->client_texture;
        }
    }
    c->client_attrib_stack=f->next;free(f);
}
static int DGClientAttribWire(struct DGContext *c,struct DGClientStackStatePoint *extended)
{
    struct DGClientStackState *state=&extended->base;
    state->array_buffer=c->array_buffer;state->client_texture=c->client_texture;
    if(c->vertex) { state->vertex_array=c->vertex->name;state->element_buffer=c->vertex->element_buffer; }
    for(unsigned i=0;i<DG_VERTEX_LEGACY_SLOTS;++i) {
        struct DGClientArray zero={0};const struct DGClientArray *a=c->vertex?&c->vertex->arrays[i]:&zero;state->arrays[i]=a->wire;
        state->arrays[i].slot=i;state->arrays[i].bytes=0;
        state->arrays[i].pointer=a->wire.buffer?(uintptr_t)a->pointer:0;
        if(a->enabled)state->enabled|=UINT32_C(1)<<i;
    }
    const struct DGClientArray *point=c->vertex?&c->vertex->arrays[DG_VERTEX_POINT_SLOT]:NULL;
    if(!point || (!point->enabled && !point->wire.size))return 0;
    extended->point=point->wire;
    extended->point.slot=DG_VERTEX_POINT_SLOT;extended->point.bytes=0;
    extended->point.pointer=point->wire.buffer?(uintptr_t)point->pointer:0;
    extended->enabled=!!point->enabled;
    return 1;
}
static void DGForgetClientBuffers(struct DGContext *c,GLsizei count,const GLuint *names)
{
    if(!c || c->common_state_blocked || count<=0 || !names)return;
    // Native deletion detaches the bound VAO's array references, not other
    // VAOs or already-pushed frames that retain those actual buffer objects.
    for(GLsizei i=0;i<count;++i)if(names[i]) {
        if(c->array_buffer==names[i])c->array_buffer=0;
        if(c->vertex) {
            if(c->vertex->element_buffer==names[i])c->vertex->element_buffer=0;
            for(unsigned j=0;j<DG_VERTEX_SLOTS;++j)
                if(c->vertex->arrays[j].wire.buffer==names[i]){
                    c->vertex->arrays[j].wire.buffer=0;DGUpdateElementVBO(c->vertex,j);
                }
        }
    }
}
static int DGArraySlot(struct DGContext *c,GLenum array)
{
    switch(array) {
    case GL_VERTEX_ARRAY:return 0;
    case GL_NORMAL_ARRAY:return 1;
    case GL_COLOR_ARRAY:return 2;
    case GL_SECONDARY_COLOR_ARRAY:return 3;
    case GL_FOG_COORDINATE_ARRAY:return 4;
    case GL_EDGE_FLAG_ARRAY:return DG_VERTEX_EDGE_SLOT;
    case GL_INDEX_ARRAY:return DG_VERTEX_INDEX_SLOT;
    case GL_WEIGHT_ARRAY_ARB:return (c->connection->features2&DG_FEATURE2_VERTEX_BLEND)?DG_VERTEX_WEIGHT_SLOT:-1;
    case GL_POINT_SIZE_ARRAY_APPLE:return (c->connection->features2&DG_FEATURE2_POINT_SIZE_ARRAY)?DG_VERTEX_POINT_SLOT:-1;
    case GL_TEXTURE_COORD_ARRAY:return 8+c->client_texture;
    default:return -1;
    }
}
static int DGClientPointSizes(struct DGContext *c)
{
    if(c->profile!=0x1000 || c->common_state_blocked ||
       !(c->connection->features2&DG_FEATURE2_POINT_SIZE_ARRAY))return 0;
    if(!c->point_sizes){
        const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
        if(extensions)c->point_sizes=strstr(extensions,"GL_APPLE_vertex_point_size")?1:-1;
    }
    return c->point_sizes>0;
}
static int DGClientWeightUnits(struct DGContext *c)
{
    if(!c || c->profile!=0x1000 || c->common_state_blocked ||
       !(c->connection->features2&DG_FEATURE2_VERTEX_BLEND))return 0;
    if(!c->weight_units){
        GLint units=0;glGetIntegerv(GL_MAX_VERTEX_UNITS_ARB,&units);
        if(units>0 && units<=(GLint)DG_VERTEX_WEIGHT_MAX)c->weight_units=units;
    }
    return c->weight_units;
}
static int DGClientArrayInteger(struct DGContext *c,GLenum key,GLint *out)
{
    if(!c || c->profile!=0x1000 || c->common_state_blocked)return 0;
    if(key==GL_POINT_SIZE_ARRAY_APPLE || key==GL_POINT_SIZE_ARRAY_TYPE_APPLE ||
       key==GL_POINT_SIZE_ARRAY_STRIDE_APPLE || key==GL_POINT_SIZE_ARRAY_BUFFER_BINDING_APPLE){
        if(!DGClientPointSizes(c))return 0;
        const struct DGClientArray *a=c->vertex?&c->vertex->arrays[DG_VERTEX_POINT_SLOT]:NULL;
        if(key==GL_POINT_SIZE_ARRAY_APPLE)*out=a && a->enabled;
        else if(key==GL_POINT_SIZE_ARRAY_TYPE_APPLE)*out=GL_FLOAT;
        else if(key==GL_POINT_SIZE_ARRAY_BUFFER_BINDING_APPLE)*out=a?a->wire.buffer:0;
        else *out=a && a->wire.size?(a->wire.stride?a->wire.stride:sizeof(GLfloat)):0;
        return 1;
    }
    if(((key>=GL_WEIGHT_ARRAY_TYPE_ARB && key<=GL_WEIGHT_ARRAY_ARB) ||
        key==GL_WEIGHT_ARRAY_BUFFER_BINDING_ARB) && DGClientWeightUnits(c)){
        const struct DGClientArray *a=c->vertex?&c->vertex->arrays[DG_VERTEX_WEIGHT_SLOT]:NULL;
        switch(key){
        case GL_WEIGHT_ARRAY_TYPE_ARB:*out=a && a->wire.type?a->wire.type:GL_FLOAT;return 1;
        case GL_WEIGHT_ARRAY_SIZE_ARB:*out=a && a->wire.size?a->wire.size:1;return 1;
        case GL_WEIGHT_ARRAY_STRIDE_ARB:*out=a && a->wire.size?(a->wire.stride?a->wire.stride:a->wire.size*DGVertexBytes(a->wire.type)):0;return 1;
        case GL_WEIGHT_ARRAY_BUFFER_BINDING_ARB:*out=a?a->wire.buffer:0;return 1;
        case GL_WEIGHT_ARRAY_ARB:*out=a && a->enabled;return 1;
        }
    }
    if(c->connection->features2&DG_FEATURE2_APPLE_ARRAYS){
        const struct DGVertexState *v=c->vertex;
        switch(key){
        case GL_ELEMENT_ARRAY_APPLE:*out=v && v->apple.element_enabled;return 1;
        case GL_ELEMENT_ARRAY_TYPE_APPLE:*out=v && v->apple.element_type?v->apple.element_type:GL_UNSIGNED_INT;return 1;
        case GL_VERTEX_ARRAY_RANGE_APPLE:*out=v && v->apple.range_enabled;return 1;
        case GL_VERTEX_ARRAY_RANGE_LENGTH_APPLE:*out=v?v->apple.range_length:0;return 1;
        case GL_MAX_VERTEX_ARRAY_RANGE_ELEMENT_APPLE:
            /* The optional native AGP mapping may not exist (Apple
             * Silicon). Our coherent snapshot path supports the period
             * minimum independent of that host mapping limit. */
            *out=65535;return 1;
        case GL_VERTEX_ARRAY_STORAGE_HINT_APPLE:
            *out=v && v->apple.storage_hint?v->apple.storage_hint:GL_STORAGE_CLIENT_APPLE;return 1;
        }
    }
    if((key<GL_VERTEX_ARRAY || key>GL_EDGE_FLAG_ARRAY_STRIDE) &&
       (key<GL_FOG_COORD_ARRAY_TYPE || key>GL_SECONDARY_COLOR_ARRAY) &&
       (key<GL_VERTEX_ARRAY_BUFFER_BINDING || key>GL_FOG_COORD_ARRAY_BUFFER_BINDING))return 0;
    // These are logical guest arrays. Native host pointers describe the last
    // submitted packet, not a Pointer/Enable call that has not drawn yet.
    static const struct {GLenum enabled,size,type,stride,buffer;unsigned slot,default_size;} fields[]={
        {GL_VERTEX_ARRAY,GL_VERTEX_ARRAY_SIZE,GL_VERTEX_ARRAY_TYPE,GL_VERTEX_ARRAY_STRIDE,GL_VERTEX_ARRAY_BUFFER_BINDING,0,4},
        {GL_NORMAL_ARRAY,0,GL_NORMAL_ARRAY_TYPE,GL_NORMAL_ARRAY_STRIDE,GL_NORMAL_ARRAY_BUFFER_BINDING,1,3},
        {GL_COLOR_ARRAY,GL_COLOR_ARRAY_SIZE,GL_COLOR_ARRAY_TYPE,GL_COLOR_ARRAY_STRIDE,GL_COLOR_ARRAY_BUFFER_BINDING,2,4},
        {GL_SECONDARY_COLOR_ARRAY,GL_SECONDARY_COLOR_ARRAY_SIZE,GL_SECONDARY_COLOR_ARRAY_TYPE,GL_SECONDARY_COLOR_ARRAY_STRIDE,GL_SECONDARY_COLOR_ARRAY_BUFFER_BINDING,3,3},
        {GL_FOG_COORD_ARRAY,0,GL_FOG_COORD_ARRAY_TYPE,GL_FOG_COORD_ARRAY_STRIDE,GL_FOG_COORD_ARRAY_BUFFER_BINDING,4,1},
        {GL_EDGE_FLAG_ARRAY,0,0,GL_EDGE_FLAG_ARRAY_STRIDE,GL_EDGE_FLAG_ARRAY_BUFFER_BINDING,DG_VERTEX_EDGE_SLOT,1},
        {GL_INDEX_ARRAY,0,GL_INDEX_ARRAY_TYPE,GL_INDEX_ARRAY_STRIDE,GL_INDEX_ARRAY_BUFFER_BINDING,DG_VERTEX_INDEX_SLOT,1},
        {GL_TEXTURE_COORD_ARRAY,GL_TEXTURE_COORD_ARRAY_SIZE,GL_TEXTURE_COORD_ARRAY_TYPE,GL_TEXTURE_COORD_ARRAY_STRIDE,GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING,8,4}
    };
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);++i) {
        if(key!=fields[i].enabled && (!fields[i].size || key!=fields[i].size) &&
           key!=fields[i].type && key!=fields[i].stride && key!=fields[i].buffer)continue;
        unsigned slot=fields[i].slot+(fields[i].slot==8?c->client_texture:0);
        struct DGClientArray zero={0};const struct DGClientArray *a=c->vertex?&c->vertex->arrays[slot]:&zero;
        if(key==fields[i].enabled)*out=a->enabled;
        else if(key==fields[i].size)*out=a->wire.size?(GLint)a->wire.size:(GLint)fields[i].default_size;
        else if(key==fields[i].type)*out=a->wire.type?(GLint)a->wire.type:GL_FLOAT;
        else if(key==fields[i].buffer)*out=(GLint)a->wire.buffer;
        else if(slot==DG_VERTEX_EDGE_SLOT || slot==DG_VERTEX_INDEX_SLOT)
            *out=a->wire.stride?(GLint)a->wire.stride:(GLint)DGVertexBytes(a->wire.type);
        else *out=a->wire.stride?(GLint)a->wire.stride:a->wire.size?
            (GLint)(a->wire.size*DGVertexBytes(a->wire.type)):(GLint)(fields[i].default_size*sizeof(GLfloat));
        return 1;
    }
    return 0;
}
static void DGArrayPointer(struct DGContext *c,unsigned slot,GLint size,GLenum type,GLsizei stride,
    const void *pointer,GLboolean normalized)
{
    if(!c) return;
    if(slot>=DG_VERTEX_SLOTS || size<1 || size>4 || stride<0 || !DGVertexBytes(type)) { DGDrawFailure(c,1,(unsigned)size);return; }
    if(!c->vertex) c->vertex=DGVertexState(c,0);
    if(!c->vertex) return;
    struct DGClientArray *a=&c->vertex->arrays[slot];
    // Matching native GLEngine ignores normalization for floating formats;
    // only integer attributes converted to float can be normalized here.
    int integer=type==GL_BYTE || type==GL_UNSIGNED_BYTE || type==GL_SHORT || type==GL_UNSIGNED_SHORT ||
        type==GL_INT || type==GL_UNSIGNED_INT;
    a->wire=(struct DGVertexArray){.slot=slot,.size=(uint32_t)size,.type=type,.stride=(uint32_t)stride,
        .normalized=slot>=16?!!normalized && integer:!!normalized,.buffer=c->array_buffer};a->pointer=pointer;
    DGUpdateElementVBO(c->vertex,slot);
}
static void DGWeightPointer(struct DGContext *c,GLint size,GLenum type,GLsizei stride,const void *pointer)
{
    int units=DGClientWeightUnits(c);
    GLenum error=!units?GL_INVALID_OPERATION:!DGWeightType(type)?GL_INVALID_ENUM:
        size<1 || size>units || stride<0?GL_INVALID_VALUE:GL_NO_ERROR;
    if(error){if(!c->error)c->error=error;return;}
    DGArrayPointer(c,DG_VERTEX_WEIGHT_SLOT,size,type,stride,pointer,GL_FALSE);
}
static void DGPointSizePointer(struct DGContext *c,GLenum type,GLsizei stride,const void *pointer)
{
    GLenum error=!DGClientPointSizes(c)?GL_INVALID_OPERATION:type!=GL_FLOAT?GL_INVALID_ENUM:
        stride<0?GL_INVALID_VALUE:GL_NO_ERROR;
    if(error){if(!c->error)c->error=error;return;}
    DGArrayPointer(c,DG_VERTEX_POINT_SLOT,1,type,stride,pointer,GL_FALSE);
}
static void DGIntegerArrayPointer(struct DGContext *c,GLuint index,GLint size,GLenum type,
                                  GLsizei stride,const void *pointer)
{
    GLenum error=GL_NO_ERROR;
    if(!(c->connection->features&DG_FEATURE_INTEGER_VERTEX_ARRAYS) || c->common_state_blocked)
        error=GL_INVALID_OPERATION;
    else if(index>=16 || size<1 || size>4 || stride<0)error=GL_INVALID_VALUE;
    else if(!DGIntegerVertexType(type))error=GL_INVALID_ENUM;
    else if(c->profile!=0x1000 && (!c->vertex || !c->vertex->name || (!c->array_buffer && pointer)))
        error=GL_INVALID_OPERATION;
    if(error){if(!c->error)c->error=error;return;}
    DGArrayPointer(c,16+index,size,type,stride,pointer,GL_FALSE);
    if(c->vertex)c->vertex->arrays[16+index].wire.normalized=DG_VERTEX_ARRAY_INTEGER;
}
static void DGDoubleArrayPointer(struct DGContext *c,GLuint index,GLint size,GLenum type,
                                 GLsizei stride,const void *pointer)
{
    GLenum error=GL_NO_ERROR;
    if(index>=16 || size<1 || size>4 || stride<0)error=GL_INVALID_VALUE;
    else if(type!=GL_DOUBLE)error=GL_INVALID_ENUM;
    else if(!c->vertex || !c->vertex->name || (!c->array_buffer && pointer))error=GL_INVALID_OPERATION;
    if(error){if(!c->error)c->error=error;return;}
    DGArrayPointer(c,16+index,size,type,stride,pointer,GL_FALSE);
    c->vertex->arrays[16+index].wire.normalized=DG_VERTEX_ARRAY_DOUBLE;
}
static void DGArrayEnable(struct DGContext *c,int slot,int enabled)
{
    if(!c) return;
    if(slot<0 || (unsigned)slot>=DG_VERTEX_SLOTS) { c->error=GL_INVALID_ENUM;return; }
    if(slot==DG_VERTEX_EDGE_SLOT || slot==DG_VERTEX_INDEX_SLOT){
        if(c->common_state_blocked || c->profile!=0x1000 ||
           (enabled && !(c->connection->features2&DG_FEATURE2_AUX_VERTEX_ARRAYS))){
            if(!c->error)c->error=GL_INVALID_OPERATION;return;
        }
    }
    if(!c->vertex) c->vertex=DGVertexState(c,0);
    if(c->vertex){c->vertex->arrays[slot].enabled=enabled;DGUpdateElementVBO(c->vertex,(unsigned)slot);}
}
static void DGAuxArrayPointer(struct DGContext *c,unsigned slot,GLenum type,GLsizei stride,const void *pointer)
{
    GLenum error=GL_NO_ERROR;
    if(c->profile!=0x1000 || c->common_state_blocked ||
       !(c->connection->features2&DG_FEATURE2_AUX_VERTEX_ARRAYS))error=GL_INVALID_OPERATION;
    else if(slot==DG_VERTEX_INDEX_SLOT && !DGColorIndexVertexType(type))error=GL_INVALID_ENUM;
    else if(stride<0)error=GL_INVALID_VALUE;
    if(error){if(!c->error)c->error=error;return;}
    DGArrayPointer(c,slot,1,type,stride,pointer,GL_FALSE);
}
static void DGGetClientPointer(struct DGContext *c,GLenum pname,void **out)
{
    if(c->profile!=0x1000 || c->common_state_blocked){
        if(!c->error)c->error=GL_INVALID_OPERATION;return;
    }
    unsigned slot;
    switch(pname){
    case GL_ELEMENT_ARRAY_POINTER_APPLE:case GL_VERTEX_ARRAY_RANGE_POINTER_APPLE:
        if(!(c->connection->features2&DG_FEATURE2_APPLE_ARRAYS)){
            if(!c->error)c->error=GL_INVALID_ENUM;return;
        }
        if(out)*out=c->vertex?(void *)(pname==GL_ELEMENT_ARRAY_POINTER_APPLE?
            c->vertex->apple.element_pointer:c->vertex->apple.range_pointer):NULL;
        else if(!c->error)c->error=GL_INVALID_VALUE;
        return;
    case GL_SELECTION_BUFFER_POINTER:case GL_FEEDBACK_BUFFER_POINTER:
        if(out)*out=c->render_buffers[pname==GL_FEEDBACK_BUFFER_POINTER].pointer;
        else if(!c->error)c->error=GL_INVALID_VALUE;
        return;
    case GL_VERTEX_ARRAY_POINTER:slot=0;break;
    case GL_NORMAL_ARRAY_POINTER:slot=1;break;
    case GL_COLOR_ARRAY_POINTER:slot=2;break;
    case GL_SECONDARY_COLOR_ARRAY_POINTER:slot=3;break;
    case GL_FOG_COORD_ARRAY_POINTER:slot=4;break;
    case GL_EDGE_FLAG_ARRAY_POINTER:slot=DG_VERTEX_EDGE_SLOT;break;
    case GL_INDEX_ARRAY_POINTER:slot=DG_VERTEX_INDEX_SLOT;break;
    case GL_WEIGHT_ARRAY_POINTER_ARB:
        if(!DGClientWeightUnits(c)){if(!c->error)c->error=GL_INVALID_ENUM;return;}
        slot=DG_VERTEX_WEIGHT_SLOT;break;
    case GL_POINT_SIZE_ARRAY_POINTER_APPLE:
        if(!DGClientPointSizes(c)){if(!c->error)c->error=GL_INVALID_ENUM;return;}
        slot=DG_VERTEX_POINT_SLOT;break;
    case GL_TEXTURE_COORD_ARRAY_POINTER:slot=8+c->client_texture;break;
    default:if(!c->error)c->error=GL_INVALID_ENUM;return;
    }
    if(out)*out=c->vertex?(void *)c->vertex->arrays[slot].pointer:NULL;
    else if(!c->error)c->error=GL_INVALID_VALUE;
}
static int DGAppleArrayOperation(struct DGContext *c)
{
    if(c->profile==0x1000 && !c->common_state_blocked &&
       (c->connection->features2&DG_FEATURE2_APPLE_ARRAYS))return 1;
    if(!c->error)c->error=GL_INVALID_OPERATION;
    return 0;
}
static void DGEnableClientArray(struct DGContext *c,GLenum array,int enabled)
{
    if(array==GL_POINT_SIZE_ARRAY_APPLE && !DGClientPointSizes(c)){
        if(!c->error)c->error=c->common_state_blocked?GL_INVALID_OPERATION:GL_INVALID_ENUM;return;
    }
    if(array==GL_WEIGHT_ARRAY_ARB && !DGClientWeightUnits(c)){
        if(!c->error)c->error=c->common_state_blocked?GL_INVALID_OPERATION:GL_INVALID_ENUM;return;
    }
    if(array!=GL_ELEMENT_ARRAY_APPLE && array!=GL_VERTEX_ARRAY_RANGE_APPLE){
        DGArrayEnable(c,DGArraySlot(c,array),enabled);return;
    }
    if(!DGAppleArrayOperation(c))return;
    if(!c->vertex)c->vertex=DGVertexState(c,0);
    if(!c->vertex)return;
    if(array==GL_ELEMENT_ARRAY_APPLE)c->vertex->apple.element_enabled=enabled;
    else c->vertex->apple.range_enabled=enabled;
}
static int DGElementIndexType(GLenum type)
{return type==GL_UNSIGNED_BYTE || type==GL_UNSIGNED_SHORT || type==GL_UNSIGNED_INT;}
static void DGAppleElementPointer(struct DGContext *c,GLenum type,const void *pointer)
{
    if(!DGAppleArrayOperation(c))return;
    if(!DGElementIndexType(type)){if(!c->error)c->error=GL_INVALID_ENUM;return;}
    if(!c->vertex)c->vertex=DGVertexState(c,0);
    if(c->vertex){c->vertex->apple.element_pointer=pointer;c->vertex->apple.element_type=type;}
}
static void DGAppleArrayRange(struct DGContext *c,GLsizei length,const void *pointer,int flush)
{
    if(!DGAppleArrayOperation(c))return;
    if(length<0){if(!c->error)c->error=GL_INVALID_VALUE;return;}
    /* Draw-time snapshots are already coherent. No retained mapping or
     * asynchronous host read of this guest range requires a flush. */
    if(flush)return;
    if(!c->vertex)c->vertex=DGVertexState(c,0);
    if(c->vertex){
        /* Apple retains the raw query values even when either value means
         * there is no valid backing range. */
        c->vertex->apple.range_pointer=pointer;
        c->vertex->apple.range_length=length;
    }
}
static void DGAppleArrayHint(struct DGContext *c,GLenum pname,GLint hint)
{
    if(!DGAppleArrayOperation(c))return;
    if(pname!=GL_VERTEX_ARRAY_STORAGE_HINT_APPLE ||
       (hint!=GL_STORAGE_CLIENT_APPLE && hint!=GL_STORAGE_SHARED_APPLE && hint!=GL_STORAGE_CACHED_APPLE)){
        if(!c->error)c->error=GL_INVALID_ENUM;return;
    }
    if(!c->vertex)c->vertex=DGVertexState(c,0);
    if(c->vertex)c->vertex->apple.storage_hint=(GLenum)hint;
}
static void DGInterleavedArrays(struct DGContext *c,GLenum format,GLsizei stride,const void *pointer)
{
    /* GL 2.1 table 2.5; sizes in native 32-bit words, including four UBYTE
     * color channels in one word. No read until a draw uses the arrays. */
    static const struct {unsigned char texture,color,normal,vertex,packed_color;} layout[]={
        {0,0,0,2,0},{0,0,0,3,0},{0,4,0,2,1},{0,4,0,3,1},
        {0,3,0,3,0},{0,0,3,3,0},{0,4,3,3,0},{2,0,0,3,0},
        {4,0,0,4,0},{2,4,0,3,1},{2,3,0,3,0},{2,0,3,3,0},
        {2,4,3,3,0},{4,4,3,4,0},
    };
    GLenum error=GL_NO_ERROR;
    if(c->profile!=0x1000 || c->common_state_blocked)error=GL_INVALID_OPERATION;
    else if(format<GL_V2F || format>GL_T4F_C4F_N3F_V4F)error=GL_INVALID_ENUM;
    else if(stride<0)error=GL_INVALID_VALUE;
    if(error){if(!c->error)c->error=error;return;}
    unsigned f=format-GL_V2F,t=layout[f].texture,col=layout[f].color,n=layout[f].normal;
    unsigned v=layout[f].vertex,packed=layout[f].packed_color;
    unsigned normal_offset=(t+(packed?1:col))*sizeof(GLfloat);
    unsigned vertex_offset=normal_offset+n*sizeof(GLfloat);
    if(!stride)stride=(GLsizei)(vertex_offset+v*sizeof(GLfloat));
    uintptr_t base=(uintptr_t)pointer;
    if(base>UINTPTR_MAX-vertex_offset){if(!c->error)c->error=GL_INVALID_VALUE;return;}
    if(!c->vertex)c->vertex=DGVertexState(c,0);
    if(!c->vertex)return;
    unsigned tex=8+c->client_texture;
    /* Period Apple GLEngine disables index/edge but keeps fog/secondary. */
    c->vertex->arrays[DG_VERTEX_EDGE_SLOT].enabled=0;
    c->vertex->arrays[DG_VERTEX_INDEX_SLOT].enabled=0;
    c->vertex->arrays[tex].enabled=!!t;
    c->vertex->arrays[2].enabled=!!col;
    c->vertex->arrays[1].enabled=!!n;
    c->vertex->arrays[0].enabled=1;
    DGUpdateElementVBO(c->vertex,DG_VERTEX_EDGE_SLOT);DGUpdateElementVBO(c->vertex,DG_VERTEX_INDEX_SLOT);
    DGUpdateElementVBO(c->vertex,tex);DGUpdateElementVBO(c->vertex,2);
    DGUpdateElementVBO(c->vertex,1);DGUpdateElementVBO(c->vertex,0);
    if(t)DGArrayPointer(c,tex,t,GL_FLOAT,stride,pointer,GL_FALSE);
    if(col)DGArrayPointer(c,2,col,packed?GL_UNSIGNED_BYTE:GL_FLOAT,stride,(const void *)(base+t*sizeof(GLfloat)),GL_FALSE);
    if(n)DGArrayPointer(c,1,n,GL_FLOAT,stride,(const void *)(base+normal_offset),GL_FALSE);
    DGArrayPointer(c,0,v,GL_FLOAT,stride,(const void *)(base+vertex_offset),GL_FALSE);
}
static uint64_t DGElementVBOMask(const struct DGVertexState *v)
{
    /* Generic attribute zero is the vertex-emitting array when present. */
    return v->enabled_vbo_arrays&~(v->arrays[16].enabled?DGVertexBit(0):UINT64_C(0));
}
static void DGPrepareArrayElementAt(struct DGContext *c,GLint index)
{
    struct DGVertexState *v=c->vertex;
    uint64_t mask=v?DGElementVBOMask(v):0;
    if(!mask || (c->big_endian_arrays && !(c->connection->features3&DG_FEATURE3_BIG_ENDIAN_ARRAY_ELEMENT)) || c->profile!=0x1000 ||
       !(c->connection->features2&DG_FEATURE2_ARRAY_ELEMENT))return;
    struct DGVertexArray arrays[DG_VERTEX_SLOTS];unsigned count=0;
    for(;mask;mask&=mask-1){
        unsigned slot=__builtin_ctzll(mask);
        const struct DGClientArray *a=&v->arrays[slot];
        arrays[count]=a->wire;arrays[count++].pointer=(uintptr_t)a->pointer;
    }
    struct DGPacket p=DGPacketNew(c,DG_ARRAY_ELEMENT_STATE);
    p.message.args[0]=count;p.message.args[2]=c->big_endian_arrays;
    if(c->big_endian_arrays && index>=0)p.message.args[3]=(uint64_t)index+1;
    DGPacketPointer(&p,1,arrays,count*sizeof(*arrays),0);
    DGSubmit(c,&p,0);DGPacketFree(&p);
}
static void DGPrepareArrayElement(struct DGContext *c)
{DGPrepareArrayElementAt(c,-1);}
static void DGArrayElement(struct DGContext *c,unsigned op,GLint index)
{
    if(c->profile!=0x1000 || !(c->connection->features2&DG_FEATURE2_ARRAY_ELEMENT)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return;
    }
    if(index<0){if(!c->error)c->error=GL_INVALID_VALUE;return;}
    const struct DGVertexState *v=c->vertex;
    if(!v)return;
    uint64_t buffers=DGElementVBOMask(v);
    if(buffers && c->big_endian_arrays && !(c->connection->features3&DG_FEATURE3_BIG_ENDIAN_ARRAY_ELEMENT)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return;
    }
    struct DGElementValue values[DG_VERTEX_SLOTS];unsigned count=0;
    unsigned slots=DG_VERTEX_LEGACY_SLOTS+!!v->arrays[DG_VERTEX_POINT_SLOT].enabled;
    for(unsigned i=0;i<slots;++i){
        const struct DGClientArray *a=&v->arrays[i];
        if(!a->enabled || a->wire.buffer || (i==0 && v->arrays[16].enabled))continue;
        size_t bytes=DGVertexBytes(a->wire.type)*a->wire.size;
        uint64_t offset=(uint64_t)index*(a->wire.stride?a->wire.stride:bytes);
        uintptr_t base=(uintptr_t)a->pointer;
        if(!bytes || bytes>sizeof(values[0].data) || !base ||
           offset>UINTPTR_MAX-base || bytes>UINTPTR_MAX-base-offset){
            if(!c->error)c->error=GL_INVALID_OPERATION;return;
        }
        values[count]=(struct DGElementValue){.slot=i,.size=a->wire.size,.type=a->wire.type,.normalized=a->wire.normalized};
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
        if(DGPeriodSignedCPUArray(&a->wire)){
            DGPeriodSignedArrayCopy(values[count].data,(const void *)(base+(uintptr_t)offset),&a->wire,1,c->big_endian_arrays);
            values[count].type=GL_FLOAT;
        }else
#endif
        memcpy(values[count].data,(const void *)(base+(uintptr_t)offset),bytes);
        ++count;
    }
    if(!count && !buffers)return;
    if(buffers && !c->common_state_blocked)DGPrepareArrayElementAt(c,index);
    struct DGPacket p=DGPacketNew(c,op);
    p.message.args[0]=(uint32_t)index;p.message.args[1]=count;
    p.message.args[3]=!!buffers;p.message.args[4]=c->big_endian_arrays;
    if(count)DGPacketPointer(&p,2,values,count*sizeof(*values),0);
    DGSubmit(c,&p,0);DGPacketFree(&p);
}
static void DGClientTexture(struct DGContext *c,GLenum unit)
{
    if(!c) return;
    if(unit<GL_TEXTURE0 || unit>=GL_TEXTURE0+8) { c->error=GL_INVALID_ENUM;return; }
    c->client_texture=(int)(unit-GL_TEXTURE0);
}
static int DGBindVertexArray(struct DGContext *c,GLuint name)
{
    if(!c)return 0;
    // VAOs must be generated in this context, not fabricated by binding an
    // arbitrary name. Preserve the old local state when native bind rejects
    // that name, without putting a round trip on every valid VAO switch.
    struct DGVertexState *v=c->vertex_states;
    for(;v && v->name!=name;v=v->next) {}
    if(!v && !name)v=DGVertexState(c,0);
    if(!v) { if(!c->error)c->error=name?GL_INVALID_OPERATION:GL_OUT_OF_MEMORY;return 0; }
    c->vertex=v;return 1;
}
static void DGGeneratedVertexArrays(struct DGContext *c,GLsizei n,const GLuint *names)
{
    if(!c || n<=0 || !names)return;
    for(GLsizei i=0;i<n;++i)if(names[i])DGVertexState(c,names[i]);
}
static void DGForgetVertexArrays(struct DGContext *c,GLsizei n,const GLuint *names)
{
    if(!c || n<=0 || !names)return;
    struct DGVertexState **link=&c->vertex_states;
    while(*link) {
        struct DGVertexState *v=*link;int remove=0;
        if(v->name)for(GLsizei i=0;i<n;++i)if(v->name==names[i]) { remove=1;break; }
        if(remove) { if(c->vertex==v)c->vertex=NULL;*link=v->next;free(v); }
        else link=&v->next;
    }
}
static void DGArrayDivisor(struct DGContext *c,GLuint index,GLuint divisor)
{
    if(!c)return;
    if(index>=16) { c->error=GL_INVALID_VALUE;return; }
    if(!(c->connection->capabilities&DG_CAP_INSTANCED_ARRAYS)) { c->error=GL_INVALID_OPERATION;return; }
    if(!c->vertex)c->vertex=DGVertexState(c,0);if(!c->vertex)return;
    struct DGPacket p=DGPacketNew(c,DG_ARRAY_DIVISOR);p.message.args[0]=index;p.message.args[1]=divisor;
    DGSubmit(c,&p,1);
    if(p.valid_reply)c->vertex->arrays[16+index].divisor=divisor;
}
static int DGArrayState(struct DGContext *c,GLuint index,GLenum pname,GLint *out)
{
    if(!c)return 0;
    if(index>=16) { c->error=GL_INVALID_VALUE;return -1; }
    // These setters are snapshotted at draw time. Queries must observe the
    // logical client VAO immediately, not the executor's last submitted draw.
    // Current generic values and divisor are already real native state.
    if(pname==0x8626 || pname==0x88fe)return 0;
    if(c->profile!=0x1000 && (!c->vertex || !c->vertex->name))return 0;
    if(!c->vertex)c->vertex=DGVertexState(c,0);if(!c->vertex)return -1;
    struct DGClientArray *a=&c->vertex->arrays[16+index];
    switch(pname) {
    case 0x8622:*out=a->enabled;return 1;
    case 0x8623:*out=a->wire.size?(GLint)a->wire.size:4;return 1;
    case 0x8624:
        // Native 13F1911 stores the effective packed stride after Pointer,
        // but a fresh VAO's untouched default stride is still zero.
        *out=a->wire.stride?(GLint)a->wire.stride:(GLint)(a->wire.size*DGVertexBytes(a->wire.type));return 1;
    case 0x8625:*out=a->wire.type?(GLint)a->wire.type:GL_FLOAT;return 1;
    case 0x886a:*out=a->wire.normalized==1;return 1;
    case 0x889f:*out=(GLint)a->wire.buffer;return 1;
    case 0x88fd:*out=a->wire.normalized==DG_VERTEX_ARRAY_INTEGER;return 1;
    default:return 0;
    }
}
/* Preflight the SAME append order/alignment as DGPacketPointer. Capture all
 * arrays/indices before submission; no retained client pointers or new wire.
 * A small packet takes exclusive ownership of this context's idle scratch.
 * DGSubmit already copies an async packet or consumes a synchronous transfer
 * BEFORE returning (the old path freed it immediately). Only then recycle it.
 * Large packets/sole borrowed inputs keep the original allocation path. */
enum {DG_DRAW_SCRATCH_LIMIT=64u*1024u};
static int DGDrawPacketReserve(struct DGContext *c,struct DGPacket *packet,size_t bytes)
{
    if(bytes<=DG_DRAW_SCRATCH_LIMIT){
        packet->data=c->draw_scratch;packet->capacity=c->draw_scratch_capacity;
        c->draw_scratch=NULL;c->draw_scratch_capacity=0;
        size_t capacity=256;while(capacity<bytes)capacity*=2;
        return DGPacketOwn(packet,capacity);
    }
    return DGPacketOwn(packet,bytes);
}
static void DGDrawPacketRelease(struct DGContext *c,struct DGPacket *packet)
{
    if(!packet->borrowed && packet->data && packet->capacity<=DG_DRAW_SCRATCH_LIMIT && !c->draw_scratch){
        c->draw_scratch=packet->data;c->draw_scratch_capacity=packet->capacity;
        packet->data=NULL;packet->capacity=0;
    }
    DGPacketFree(packet);
}
static int DGDrawReservePart(size_t *total,size_t bytes)
{
    if(*total>DG_MAX_BYTES)return 0;
    if(!bytes)return 1;
    size_t offset=(*total+7)&~(size_t)7;
    if(offset>DG_MAX_BYTES || bytes>DG_MAX_BYTES-offset)return 0;
    *total=offset+bytes;return 1;
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
static void DGPeriodCaptureSignedArray(struct DGPacket *packet,struct DGVertexArray *descriptor,
    const struct DGClientArray *array,uint64_t count,int big_endian)
{
    size_t bytes=DGArrayBytes(count,array->wire.size*sizeof(GLfloat));
    size_t offset=((size_t)packet->message.bytes+7)&~(size_t)7;
    if(bytes==SIZE_MAX || offset>DG_MAX_BYTES || bytes>DG_MAX_BYTES-offset ||
       !DGPacketOwn(packet,offset+bytes)){packet->failed=1;return;}
    if(offset>packet->message.bytes)memset(packet->data+packet->message.bytes,0,offset-packet->message.bytes);
    DGPeriodSignedArrayCopy(packet->data+offset,array->pointer,&array->wire,count,big_endian);
    packet->message.bytes=(uint32_t)(offset+bytes);
    descriptor->pointer=offset+1;descriptor->bytes=bytes;descriptor->type=GL_FLOAT;descriptor->stride=0;
}
#endif
#include "passthrough-opengl-draw-capture-client.c"
struct DGBaseDraw {
    unsigned kind;GLint vertex;GLuint start,end;
    struct DGDrawListItem *list;
    uint64_t indirect_offset;
};
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#define DGDrawInstancesBase DGDrawInstancesBaseProfileBody
#endif
static void DGDrawInstancesBase(struct DGContext *c,GLenum mode,GLint first,GLsizei count,GLenum type,
    const void *indices,GLsizei instances,int instanced,const struct DGBaseDraw *base)
{
    if(!c) return;
    if(first<0 || count<0 || instances<0 || (type && type!=GL_UNSIGNED_BYTE && type!=GL_UNSIGNED_SHORT && type!=GL_UNSIGNED_INT)) {
        c->error=GL_INVALID_VALUE;return;
    }
    if(instanced && !(c->connection->capabilities&DG_CAP_INSTANCED_ARRAYS)) { c->error=GL_INVALID_OPERATION;return; }
    int feedback=base && (base->kind==DG_DRAW_FEEDBACK || base->kind==DG_DRAW_FEEDBACK_STREAM);
    int indirect=base && (base->kind==DG_DRAW_ARRAYS_INDIRECT || base->kind==DG_DRAW_ELEMENTS_INDIRECT);
    if((!count && !feedback && !indirect) || !instances) return;
    if(!c->vertex) c->vertex=DGVertexState(c,0);
    if(!c->vertex) return;
    struct DGVertexState *v=c->vertex;
    if(base && !feedback && (!indirect || type) && c->profile!=0x1000 && !v->element_buffer){c->error=GL_INVALID_OPERATION;return;}
    struct DGDrawListItem *list=base?base->list:NULL;
    uint64_t vertices=list?0:(uint64_t)first+(uint32_t)count;
    size_t index_bytes=type && !list ? DGArrayBytes(count,DGVertexBytes(type)) : 0;
    if(index_bytes==SIZE_MAX) { c->error=GL_INVALID_VALUE;return; }
    if(type && !list && !v->element_buffer) {
        if(!indices) { c->error=GL_INVALID_VALUE;return; }
        vertices=base?DGBaseVertexSpan(indices,(uint32_t)count,DGVertexBytes(type),c->big_endian_arrays,base->vertex,0,0):
            c->big_endian_arrays?DGBigEndianMaxIndex(indices,(uint32_t)count,DGVertexBytes(type)):
            DGMaxIndex(indices,(uint32_t)count,type);
    }
    struct DGVertexArray descriptors[DG_VERTEX_SLOTS];uint32_t divisors[DG_VERTEX_SLOTS];unsigned n=0;
    int extended=instanced || base,mixed=0;
    unsigned slots=DG_VERTEX_LEGACY_SLOTS+!!v->arrays[DG_VERTEX_POINT_SLOT].enabled;
    for(unsigned i=0;i<slots;++i) if(v->arrays[i].enabled) {
        struct DGClientArray *array=&v->arrays[i];
        if((feedback || indirect) && (i<16 || !array->wire.buffer)){if(!c->error)c->error=GL_INVALID_OPERATION;return;}
        if(type && v->element_buffer && !array->wire.buffer && !array->divisor) {
            /* Mixed CPU arrays + EBO need its real index range before reading
             * guest memory. Do not guess a client allocation's length. */
            mixed=1;
        }
        divisors[n]=array->divisor;if(array->divisor)extended=1;
        descriptors[n++]=array->wire;
    }
    if(list){
        unsigned size=DGVertexBytes(type);
        void *snapshot=NULL;size_t capacity=0;
        int valid=1;
        for(GLsizei i=0;i<count && valid;++i){
            const struct DGDrawListItem *item=list+i;
            if(!item->count)continue;
            uint64_t end=0;
            if(!type)end=(uint64_t)item->first_or_base+item->count;
            else if(mixed || !v->element_buffer){
                size_t bytes=DGArrayBytes(item->count,size);
                const void *pointer=(const void *)(uintptr_t)item->index;
                if(bytes==SIZE_MAX || (mixed && item->index>INTPTR_MAX)){valid=0;break;}
                if(mixed){
                    if(bytes>capacity){
                        void *next=realloc(snapshot,bytes);
                        if(!next){c->error=GL_OUT_OF_MEMORY;valid=0;break;}
                        snapshot=next;capacity=bytes;
                    }
                    if(!DGReadBufferData(c,GL_ELEMENT_ARRAY_BUFFER,(GLintptr)item->index,bytes,snapshot)){
                        valid=0;break;
                    }
                    pointer=snapshot;
                }
                end=DGBaseVertexSpan(pointer,item->count,size,c->big_endian_arrays,item->first_or_base,0,0);
                if(end==UINT64_MAX){valid=0;break;}
            }
            if(end>vertices)vertices=end;
        }
        free(snapshot);
        if(!valid){if(!c->error)c->error=GL_INVALID_OPERATION;return;}
    }else if(mixed){
        if(c->profile!=0x1000 || (uintptr_t)indices>INTPTR_MAX){c->error=GL_INVALID_OPERATION;return;}
        void *snapshot=malloc(index_bytes);
        if(!snapshot){c->error=GL_OUT_OF_MEMORY;return;}
        int valid=DGReadBufferData(c,GL_ELEMENT_ARRAY_BUFFER,(GLintptr)(uintptr_t)indices,index_bytes,snapshot);
        if(valid)vertices=DGBaseVertexSpan(snapshot,(uint32_t)count,DGVertexBytes(type),c->big_endian_arrays,base?base->vertex:0,0,0);
        free(snapshot);
        if(!valid)return;
    }
    if(vertices==UINT64_MAX){c->error=GL_INVALID_OPERATION;return;}
    if(extended && !(c->connection->capabilities&DG_CAP_INSTANCED_ARRAYS)) { c->error=GL_INVALID_OPERATION;return; }
    struct DGPacket packet=DGPacketNew(c,extended?DG_DRAW_INSTANCED_ARRAYS:DG_DRAW);
    size_t reserve=0,spans[DG_VERTEX_SLOTS]={0};struct DGDrawCapturePlan capture={0};
    int coalesce=dg_draw_capture_coalescing!=0;
    if(!DGDrawReservePart(&reserve,n*sizeof(descriptors[0])) ||
       (extended && !DGDrawReservePart(&reserve,n*sizeof(divisors[0]))) ||
       (type && !v->element_buffer && !DGDrawReservePart(&reserve,index_bytes)))packet.failed=1;
    if(list){
        if(!DGDrawReservePart(&reserve,(size_t)count*sizeof(*list)))packet.failed=1;
        if(type && !v->element_buffer)
            for(GLsizei i=0;!packet.failed && i<count;++i)
                if(!DGDrawReservePart(&reserve,DGArrayBytes(list[i].count,DGVertexBytes(type))))packet.failed=1;
    }
    for(unsigned i=0;!packet.failed && i<slots;++i)if(v->arrays[i].enabled && !v->arrays[i].wire.buffer){
        struct DGClientArray *array=&v->arrays[i];
        uint64_t elements=array->divisor?((uint32_t)instances-1)/(uint64_t)array->divisor+1:vertices;
        spans[i]=DGVertexSpan(&array->wire,elements);
        if(!array->pointer || spans[i]==SIZE_MAX){DGDrawFailure(c,3,i);packet.failed=1;}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
        else if(DGPeriodSignedCPUArray(&array->wire)){
            if(spans[i]>UINTPTR_MAX-(uintptr_t)array->pointer ||
               !DGDrawReservePart(&reserve,DGArrayBytes(elements,array->wire.size*sizeof(GLfloat))))packet.failed=1;
        }
#endif
        else if(coalesce && !DGDrawCaptureAdd(&capture,array->pointer,spans[i]))packet.failed=1;
        else if(!coalesce && !DGDrawReservePart(&reserve,spans[i]))packet.failed=1;
    }
    if(coalesce && !packet.failed){
        DGDrawCaptureMerge(&capture);
        for(unsigned i=0;i<capture.count;++i)
            if(!DGDrawReservePart(&reserve,capture.ranges[i].end-capture.ranges[i].begin))packet.failed=1;
    }
    // With no descriptors, a sole large index span can use the existing
    // borrowed-input path. Do not replace that zero-copy case with a new copy.
    if(!packet.failed && reserve && (n || list) && !DGDrawPacketReserve(c,&packet,reserve))packet.failed=1;
    if(packet.failed){
        DGDrawFailure(c,4,n);DGSubmit(c,&packet,0);DGDrawPacketRelease(c,&packet);return;
    }
    packet.message.args[0]=mode;packet.message.args[1]=(uint32_t)first;packet.message.args[2]=(uint32_t)count;
    packet.message.args[3]=type;packet.message.args[4]=type?v->element_buffer:0;packet.message.args[8]=n;
    packet.message.args[13]=c->big_endian_arrays;
    DGPacketPointer(&packet,6,descriptors,n*sizeof(descriptors[0]),0);
    if(!n) { packet.failed=0;packet.message.args[6]=0; }
    uint64_t descriptor_offset=packet.message.args[6];
    if(extended) {
        packet.message.args[11]=(uint32_t)instances;packet.message.args[12]=instanced;
        if(n)DGPacketPointer(&packet,10,divisors,n*sizeof(divisors[0]),0);
    }
    if(base){
        packet.message.args[7]=((uint64_t)base->end<<32)|base->start;
        packet.message.args[14]=base->kind;
        packet.message.args[15]=indirect?base->indirect_offset:(uint64_t)(int64_t)base->vertex;
    }
    if(list){
        if(type && !v->element_buffer)for(GLsizei i=0;!packet.failed && i<count;++i){
            if(!list[i].count)continue;
            DGPacketPointer(&packet,5,(const void *)(uintptr_t)list[i].index,
                DGArrayBytes(list[i].count,DGVertexBytes(type)),0);
            list[i].index=packet.message.args[5];
        }
        packet.message.args[5]=0;
        DGPacketPointer(&packet,15,list,(size_t)count*sizeof(*list),0);
    }else if(type && !indirect) {
        if(v->element_buffer) packet.message.args[5]=(uintptr_t)indices;
        else DGPacketPointer(&packet,5,indices,index_bytes,0);
    }
    if(coalesce)for(unsigned i=0;!packet.failed && i<capture.count;++i){
        struct DGDrawCaptureRange *range=&capture.ranges[i];
        DGPacketPointer(&packet,9,(const void*)range->begin,range->end-range->begin,0);
        range->wire=packet.message.args[9];
    }
    unsigned j=0;
    for(unsigned i=0;i<slots;++i) if(v->arrays[i].enabled) {
        struct DGClientArray *array=&v->arrays[i];struct DGVertexArray *d=&descriptors[j++];
        if(d->buffer) { d->pointer=(uintptr_t)array->pointer;d->bytes=0; }
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ == 1050
        else if(DGPeriodSignedCPUArray(&array->wire)){
            uint64_t elements=array->divisor?((uint32_t)instances-1)/(uint64_t)array->divisor+1:vertices;
            DGPeriodCaptureSignedArray(&packet,d,array,elements,c->big_endian_arrays);
        }
#endif
        else {
            size_t span=spans[i];
            if(coalesce){
                d->pointer=DGDrawCaptureOffset(&capture,array->pointer,span);
                if(!d->pointer)packet.failed=1;
            }else{
                DGPacketPointer(&packet,9,array->pointer,span,0);
                d->pointer=packet.message.args[9];
            }
            d->bytes=span;
        }
    }
    if(n && !packet.failed) memcpy(packet.data+descriptor_offset-1,descriptors,n*sizeof(descriptors[0]));
    if(packet.failed) DGDrawFailure(c,4,n);
    DGSubmit(c,&packet,0);DGDrawPacketRelease(c,&packet);
}
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#undef DGDrawInstancesBase
static void DGDrawInstancesBase(struct DGContext *c,GLenum mode,GLint first,GLsizei count,GLenum type,
    const void *indices,GLsizei instances,int instanced,const struct DGBaseDraw *base)
{
    struct DGClientProfileStamp s=DGClientProfileBegin(4,0,0);
    DGDrawInstancesBaseProfileBody(c,mode,first,count,type,indices,instances,instanced,base);DGClientProfileEnd(s);
}
#endif
static void DGDrawInstances(struct DGContext *c,GLenum mode,GLint first,GLsizei count,GLenum type,
    const void *indices,GLsizei instances,int instanced)
{ DGDrawInstancesBase(c,mode,first,count,type,indices,instances,instanced,NULL); }
static void DGDrawBase(struct DGContext *c,GLenum mode,GLsizei count,GLenum type,const void *indices,
    GLsizei instances,unsigned kind,GLint vertex,GLuint start,GLuint end)
{
    if(!c)return;
    if(!(c->connection->features&DG_FEATURE_BASE_VERTEX)){c->error=GL_INVALID_OPERATION;return;}
    if(type!=GL_UNSIGNED_BYTE && type!=GL_UNSIGNED_SHORT && type!=GL_UNSIGNED_INT){c->error=GL_INVALID_ENUM;return;}
    if(start>end){c->error=GL_INVALID_VALUE;return;}
    struct DGBaseDraw base={.kind=kind,.vertex=vertex,.start=start,.end=end};
    DGDrawInstancesBase(c,mode,0,count,type,indices,instances,kind==DG_BASE_INSTANCED,&base);
}
static void DGMultiDraw(struct DGContext *c,GLenum mode,const GLint *first,
    const GLsizei *counts,GLenum type,const void *const *indices,GLsizei draws,const GLint *bases,unsigned kind)
{
    if(!c)return;
    if(!(c->connection->features&DG_FEATURE_MULTI_DRAW) ||
       (kind==DG_MULTI_BASE && !(c->connection->features&DG_FEATURE_BASE_VERTEX))){
        c->error=GL_INVALID_OPERATION;return;
    }
    if(kind!=DG_MULTI_ARRAYS && type!=GL_UNSIGNED_BYTE && type!=GL_UNSIGNED_SHORT && type!=GL_UNSIGNED_INT){
        c->error=GL_INVALID_ENUM;return;
    }
    if(draws<0 || (draws && (!counts || (kind==DG_MULTI_ARRAYS?!first:!indices) ||
                           (kind==DG_MULTI_BASE && !bases)))){c->error=GL_INVALID_VALUE;return;}
    if(!draws)return;
    if((uint32_t)draws>DG_DRAW_LIST_LIMIT){c->error=GL_OUT_OF_MEMORY;return;}
    struct DGDrawListItem *list=calloc((size_t)draws,sizeof(*list));
    if(!list){c->error=GL_OUT_OF_MEMORY;return;}
    int any=0;
    for(GLsizei i=0;i<draws;++i){
        // Native Intel and Apple Silicon skip nonpositive list entries,
        // without consuming their index pointer or changing nearby draws.
        if(counts[i]<=0)continue;
        any=1;
        if(kind==DG_MULTI_ARRAYS && first[i]<0){
            free(list);c->error=GL_INVALID_VALUE;return;
        }
        list[i].count=counts[i];
        if(kind==DG_MULTI_ARRAYS)list[i].first_or_base=first[i];
        else {
            list[i].index=(uintptr_t)indices[i];
            if(kind==DG_MULTI_BASE)list[i].first_or_base=bases[i];
        }
    }
    if(!any){free(list);return;}
    if(type && (!c->vertex || !c->vertex->element_buffer))
        for(GLsizei i=0;i<draws;++i)if(list[i].count && !list[i].index){
            free(list);c->error=GL_INVALID_VALUE;return;
        }
    struct DGBaseDraw command={.kind=kind,.list=list};
    DGDrawInstancesBase(c,mode,0,draws,type,NULL,1,0,&command);
    free(list);
}
static void DGDraw(struct DGContext *c,GLenum mode,GLint first,GLsizei count,GLenum type,const void *indices)
{ DGDrawInstances(c,mode,first,count,type,indices,1,0); }
static void DGDrawAppleElements(struct DGContext *c,GLenum mode,GLuint start,GLuint end,int ranged,
    const GLint *first,const GLsizei *counts,GLsizei draws,int multi)
{
    if(!DGAppleArrayOperation(c))return;
    if(draws<0 || (ranged && start>end) || (draws && (!first || !counts)) ||
       (!multi && draws && (*first<0 || *counts<0))){
        if(!c->error)c->error=GL_INVALID_VALUE;return;
    }
    if(!c->vertex || !c->vertex->apple.element_enabled){
        if(!c->error)c->error=GL_INVALID_OPERATION;return;
    }
    if(!draws)return;
    if((uint32_t)draws>DG_DRAW_LIST_LIMIT){if(!c->error)c->error=GL_OUT_OF_MEMORY;return;}
    GLenum type=c->vertex->apple.element_type?c->vertex->apple.element_type:GL_UNSIGNED_INT;
    uintptr_t pointer=(uintptr_t)c->vertex->apple.element_pointer;
    unsigned width=DGVertexBytes(type);
    struct DGDrawListItem *list=multi?calloc((size_t)draws,sizeof(*list)):NULL;
    if(multi && !list){if(!c->error)c->error=GL_OUT_OF_MEMORY;return;}
    unsigned used=0;
    for(GLsizei i=0;i<draws;++i){
        if(first[i]<0 || counts[i]<=0)continue;
        uint64_t offset=(uint64_t)first[i]*width,bytes=(uint64_t)counts[i]*width;
        if((!pointer && !c->vertex->element_buffer) || offset>UINTPTR_MAX-pointer ||
           bytes>UINTPTR_MAX-pointer-offset){
            if(!c->error)c->error=GL_INVALID_OPERATION;free(list);return;
        }
        const void *indices=(const void *)(pointer+(uintptr_t)offset);
        if(!multi){
            /* The existing base/range envelope also handles CPU vertices
             * mixed with an actual element buffer, unlike the old raw call. */
            DGDrawBase(c,mode,counts[i],type,indices,1,ranged?DG_BASE_RANGE:DG_BASE_ELEMENTS,0,start,end);
            return;
        }
        list[used++]=(struct DGDrawListItem){.index=(uintptr_t)indices,.count=counts[i]};
    }
    if(used){
        /* Start/end are an optimization hint; bounds of actual index bytes
         * are verified by the shared multidraw path. */
        struct DGBaseDraw command={.kind=DG_MULTI_ELEMENTS,.list=list};
        DGDrawInstancesBase(c,mode,0,(GLsizei)used,type,NULL,1,0,&command);
    }
    free(list);
}
static void DGDrawFeedback(struct DGContext *c,GLenum mode,GLuint name,GLuint stream,unsigned kind)
{
    if(!c)return;
    if(c->profile==0x1000 || !(c->connection->features2&DG_FEATURE2_FEEDBACK_OBJECTS)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return;
    }
    struct DGBaseDraw draw={.kind=kind,.start=name,.end=stream};
    DGDrawInstancesBase(c,mode,0,0,0,NULL,1,0,&draw);
}
static void DGDrawIndirect(struct DGContext *c,GLenum mode,GLenum type,const void *offset,unsigned kind)
{
    if(!c)return;
    if(c->profile==0x1000 || !(c->connection->features2&DG_FEATURE2_DRAW_INDIRECT)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return;
    }
    if(kind==DG_DRAW_ELEMENTS_INDIRECT && type!=GL_UNSIGNED_BYTE && type!=GL_UNSIGNED_SHORT && type!=GL_UNSIGNED_INT){
        if(!c->error)c->error=GL_INVALID_ENUM;return;
    }
    struct DGBaseDraw draw={.kind=kind,.indirect_offset=(uintptr_t)offset};
    DGDrawInstancesBase(c,mode,0,0,type,NULL,1,0,&draw);
}
static void DGDestroyArrays(struct DGContext *c)
{
    while(c->client_attrib_stack) { struct DGClientAttribFrame *f=c->client_attrib_stack;c->client_attrib_stack=f->next;free(f); }
    while(c->vertex_states) { struct DGVertexState *v=c->vertex_states;c->vertex_states=v->next;free(v); }
}
