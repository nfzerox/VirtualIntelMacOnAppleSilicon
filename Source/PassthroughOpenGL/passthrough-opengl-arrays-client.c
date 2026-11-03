/* Client arrays are captured at draw time, not when Pointer is called.
 * VBO arguments carry offsets only. No guest pointer reaches host OpenGL. */
struct DGClientArray { struct DGVertexArray wire;const void *pointer;int enabled;GLuint divisor; };
struct DGVertexState {
    GLuint name,element_buffer;struct DGClientArray arrays[DG_VERTEX_SLOTS];
    struct DGVertexState *next;
};
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
            memcpy(v->arrays,f->vertex[0].arrays,sizeof(v->arrays));c->vertex=v;
            c->array_buffer=f->array_buffer;c->client_texture=f->client_texture;
        }
    }
    c->client_attrib_stack=f->next;free(f);
}
static void DGClientAttribWire(struct DGContext *c,struct DGClientStackState *state)
{
    state->array_buffer=c->array_buffer;state->client_texture=c->client_texture;
    if(c->vertex) { state->vertex_array=c->vertex->name;state->element_buffer=c->vertex->element_buffer; }
    for(unsigned i=0;i<DG_VERTEX_SLOTS;++i) {
        struct DGClientArray zero={0};const struct DGClientArray *a=c->vertex?&c->vertex->arrays[i]:&zero;state->arrays[i]=a->wire;
        state->arrays[i].slot=i;state->arrays[i].bytes=0;
        state->arrays[i].pointer=a->wire.buffer?(uintptr_t)a->pointer:0;
        if(a->enabled)state->enabled|=UINT32_C(1)<<i;
    }
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
                if(c->vertex->arrays[j].wire.buffer==names[i])c->vertex->arrays[j].wire.buffer=0;
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
    case GL_TEXTURE_COORD_ARRAY:return 8+c->client_texture;
    default:return -1;
    }
}
static int DGClientArrayInteger(struct DGContext *c,GLenum key,GLint *out)
{
    if(!c || c->profile!=0x1000 || c->common_state_blocked)return 0;
    if((key<GL_VERTEX_ARRAY || key>GL_TEXTURE_COORD_ARRAY_STRIDE) &&
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
}
static void DGArrayEnable(struct DGContext *c,int slot,int enabled)
{
    if(!c) return;
    if(slot<0 || (unsigned)slot>=DG_VERTEX_SLOTS) { c->error=GL_INVALID_ENUM;return; }
    if(!c->vertex) c->vertex=DGVertexState(c,0);
    if(c->vertex) c->vertex->arrays[slot].enabled=enabled;
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
    case 0x886a:*out=a->wire.normalized;return 1;
    case 0x889f:*out=(GLint)a->wire.buffer;return 1;
    case 0x88fd:*out=GL_FALSE;return 1; /* Integer pointer codec still unsupported. */
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
#include "passthrough-opengl-draw-capture-client.c"
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
#define DGDrawInstances DGDrawInstancesProfileBody
#endif
static void DGDrawInstances(struct DGContext *c,GLenum mode,GLint first,GLsizei count,GLenum type,
    const void *indices,GLsizei instances,int instanced)
{
    if(!c) return;
    if(first<0 || count<0 || instances<0 || (type && type!=GL_UNSIGNED_BYTE && type!=GL_UNSIGNED_SHORT && type!=GL_UNSIGNED_INT)) {
        c->error=GL_INVALID_VALUE;return;
    }
    if(instanced && !(c->connection->capabilities&DG_CAP_INSTANCED_ARRAYS)) { c->error=GL_INVALID_OPERATION;return; }
    if(!count || !instances) return;
    if(!c->vertex) c->vertex=DGVertexState(c,0);
    if(!c->vertex) return;
    struct DGVertexState *v=c->vertex;
    uint64_t vertices=(uint64_t)first+(uint32_t)count;
    size_t index_bytes=type ? DGArrayBytes(count,DGVertexBytes(type)) : 0;
    if(index_bytes==SIZE_MAX) { c->error=GL_INVALID_VALUE;return; }
    if(type && !v->element_buffer) {
        if(!indices) { c->error=GL_INVALID_VALUE;return; }
        vertices=c->big_endian_arrays?DGBigEndianMaxIndex(indices,(uint32_t)count,DGVertexBytes(type)):
            DGMaxIndex(indices,(uint32_t)count,type);
    }
    struct DGVertexArray descriptors[DG_VERTEX_SLOTS];uint32_t divisors[DG_VERTEX_SLOTS];unsigned n=0;
    int extended=instanced;
    for(unsigned i=0;i<DG_VERTEX_SLOTS;++i) if(v->arrays[i].enabled) {
        struct DGClientArray *array=&v->arrays[i];
        if(type && v->element_buffer && !array->wire.buffer && !array->divisor) {
            /* Mixed CPU arrays + EBO need its real index range before reading
             * guest memory. Do not guess a client allocation's length. */
            DGDrawFailure(c,2,i);return;
        }
        divisors[n]=array->divisor;if(array->divisor)extended=1;
        descriptors[n++]=array->wire;
    }
    if(extended && !(c->connection->capabilities&DG_CAP_INSTANCED_ARRAYS)) { c->error=GL_INVALID_OPERATION;return; }
    struct DGPacket packet=DGPacketNew(c,extended?DG_DRAW_INSTANCED_ARRAYS:DG_DRAW);
    size_t reserve=0,spans[DG_VERTEX_SLOTS]={0};struct DGDrawCapturePlan capture={0};
    int coalesce=dg_draw_capture_coalescing!=0;
    if(!DGDrawReservePart(&reserve,n*sizeof(descriptors[0])) ||
       (extended && !DGDrawReservePart(&reserve,n*sizeof(divisors[0]))) ||
       (type && !v->element_buffer && !DGDrawReservePart(&reserve,index_bytes)))packet.failed=1;
    for(unsigned i=0;!packet.failed && i<DG_VERTEX_SLOTS;++i)if(v->arrays[i].enabled && !v->arrays[i].wire.buffer){
        struct DGClientArray *array=&v->arrays[i];
        uint64_t elements=array->divisor?((uint32_t)instances-1)/(uint64_t)array->divisor+1:vertices;
        spans[i]=DGVertexSpan(&array->wire,elements);
        if(!array->pointer || spans[i]==SIZE_MAX){DGDrawFailure(c,3,i);packet.failed=1;}
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
    if(!packet.failed && reserve && n && !DGDrawPacketReserve(c,&packet,reserve))packet.failed=1;
    if(packet.failed){
        DGDrawFailure(c,4,n);DGSubmit(c,&packet,0);DGDrawPacketRelease(c,&packet);return;
    }
    packet.message.args[0]=mode;packet.message.args[1]=(uint32_t)first;packet.message.args[2]=(uint32_t)count;
    packet.message.args[3]=type;packet.message.args[4]=v->element_buffer;packet.message.args[8]=n;
    packet.message.args[13]=c->big_endian_arrays;
    DGPacketPointer(&packet,6,descriptors,n*sizeof(descriptors[0]),0);
    if(!n) { packet.failed=0;packet.message.args[6]=0; }
    uint64_t descriptor_offset=packet.message.args[6];
    if(extended) {
        packet.message.args[11]=(uint32_t)instances;packet.message.args[12]=instanced;
        if(n)DGPacketPointer(&packet,10,divisors,n*sizeof(divisors[0]),0);
    }
    if(type) {
        if(v->element_buffer) packet.message.args[5]=(uintptr_t)indices;
        else DGPacketPointer(&packet,5,indices,index_bytes,0);
    }
    if(coalesce)for(unsigned i=0;!packet.failed && i<capture.count;++i){
        struct DGDrawCaptureRange *range=&capture.ranges[i];
        DGPacketPointer(&packet,9,(const void*)range->begin,range->end-range->begin,0);
        range->wire=packet.message.args[9];
    }
    unsigned j=0;
    for(unsigned i=0;i<DG_VERTEX_SLOTS;++i) if(v->arrays[i].enabled) {
        struct DGClientArray *array=&v->arrays[i];struct DGVertexArray *d=&descriptors[j++];
        if(d->buffer) { d->pointer=(uintptr_t)array->pointer;d->bytes=0; }
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
#undef DGDrawInstances
static void DGDrawInstances(struct DGContext *c,GLenum mode,GLint first,GLsizei count,GLenum type,
    const void *indices,GLsizei instances,int instanced)
{
    struct DGClientProfileStamp s=DGClientProfileBegin(4,0,0);
    DGDrawInstancesProfileBody(c,mode,first,count,type,indices,instances,instanced);DGClientProfileEnd(s);
}
#endif
static void DGDraw(struct DGContext *c,GLenum mode,GLint first,GLsizei count,GLenum type,const void *indices)
{ DGDrawInstances(c,mode,first,count,type,indices,1,0); }
static void DGDestroyArrays(struct DGContext *c)
{
    while(c->client_attrib_stack) { struct DGClientAttribFrame *f=c->client_attrib_stack;c->client_attrib_stack=f->next;free(f); }
    while(c->vertex_states) { struct DGVertexState *v=c->vertex_states;c->vertex_states=v->next;free(v); }
}
