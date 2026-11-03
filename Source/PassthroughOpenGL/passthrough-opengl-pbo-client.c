/* Pixel addresses are never dereferenced when they name native PBO offsets.
 * Binding changes are acknowledged so rejected calls cannot corrupt the
 * pointer-versus-offset decision. Ordinary non-PBO commands keep batching. */
static int DGPBOAvailable(struct DGContext *c)
{
    if(!c)return 0;
    if(c->connection->capabilities&DG_CAP_PIXEL_BUFFERS)return 1;
    c->error=GL_INVALID_OPERATION;return 0;
}
static void DGPBOBindings(struct DGContext *c,uint64_t value)
{ c->pixel_buffer[1]=(GLuint)value;c->pixel_buffer[0]=(GLuint)(value>>32); }
static int DGPBOBind(struct DGContext *c,GLenum target,GLuint buffer)
{
    if(!c || (target!=GL_PIXEL_PACK_BUFFER && target!=GL_PIXEL_UNPACK_BUFFER) ||
       !(c->connection->capabilities&DG_CAP_PIXEL_BUFFERS))return 0;
    struct DGPacket p=DGPacketNew(c,DG_PBO_BIND);p.message.args[0]=target;p.message.args[1]=buffer;
    struct DGReply r=DGSubmit(c,&p,1);if(p.valid_reply)DGPBOBindings(c,r.result);return 1;
}
static int DGPBODelete(struct DGContext *c,GLsizei n,const GLuint *buffers)
{
    if(!c || !(c->connection->capabilities&DG_CAP_PIXEL_BUFFERS))return 0;
    if(n<0 || (n && !buffers)) { c->error=GL_INVALID_VALUE;return 1; }
    int affected=n==0;
    for(GLsizei i=0;i<n && !affected;++i)
        affected=buffers[i] && (buffers[i]==c->pixel_buffer[0] || buffers[i]==c->pixel_buffer[1]);
    if(!affected)return 0;
    struct DGPacket p=DGPacketNew(c,DG_PBO_DELETE);p.message.args[0]=(uint32_t)n;p.message.flags=DG_INPUT_ONLY;
    if(n)DGPacketPointer(&p,1,buffers,DGArrayBytes(n,sizeof(*buffers)),0);
    struct DGReply r=DGSubmit(c,&p,1);if(p.valid_reply) { DGPBOBindings(c,r.result);DGForgetClientBuffers(c,n,buffers); }DGPacketFree(&p);return 1;
}
static int DGPBOStack(struct DGContext *c,GLbitfield mask,int pushing)
{
    if(!c || !(c->connection->capabilities&DG_CAP_PIXEL_BUFFERS))return 0;
    int vertex_push=pushing && (mask&GL_CLIENT_VERTEX_ARRAY_BIT);
    if(vertex_push && !(c->connection->capabilities&DG_CAP_CLIENT_VERTEX_STACK)) {
        if(!c->error)c->error=GL_INVALID_OPERATION;return 1;
    }
    struct DGClientAttribFrame *frame=pushing?DGClientAttribCapture(c,mask):NULL;
    if((pushing&&!frame) || (!pushing&&!DGClientAttribPreparePop(c)))return 1;
    struct DGClientStackState stack={0};struct DGPixelState3D *volume=&stack.pixel;struct DGPixelState *state=&volume->base;
    int extended=!!(c->connection->capabilities&DG_CAP_TEXTURE_3D);
    struct DGPacket p=DGPacketNew(c,DG_PBO_CLIENT_STACK);
    p.message.args[0]=pushing;p.message.args[1]=mask;p.message.flags=vertex_push?0:DG_OUTPUT_ONLY;
    if(vertex_push)DGClientAttribWire(c,&stack);
    p.message.bytes=vertex_push?sizeof(stack):extended ? sizeof(*volume) : sizeof(*state);p.data=(void *)&stack;
    p.capacity=p.message.bytes;DGSubmit(c,&p,1);
    if(p.valid_reply) {
        memcpy(c->alignment,state->alignment,sizeof(c->alignment));memcpy(c->row_length,state->row_length,sizeof(c->row_length));
        memcpy(c->skip_rows,state->skip_rows,sizeof(c->skip_rows));memcpy(c->skip_pixels,state->skip_pixels,sizeof(c->skip_pixels));
        memcpy(c->row_bytes,state->row_bytes,sizeof(c->row_bytes));memcpy(c->pixel_buffer,state->buffers,sizeof(c->pixel_buffer));
        if(extended) {
            memcpy(c->image_height,volume->image_height,sizeof(c->image_height));
            memcpy(c->skip_images,volume->skip_images,sizeof(c->skip_images));
        }
        if(pushing) { frame->next=c->client_attrib_stack;c->client_attrib_stack=frame;frame=NULL; }
        else DGClientAttribRestore(c);
    }
    free(frame);return 1;
}
static void DGPBOPixel3D(struct DGContext *c,unsigned subimage,GLenum target,GLint level,GLint internal,
    GLint x,GLint y,GLint z,GLsizei width,GLsizei height,GLsizei depth,GLint border,
    GLenum format,GLenum type,const void *offset)
{
    if(!DGPBOAvailable(c))return;
    struct DGPacket p=DGPacketNew(c,DG_PBO_PIXEL_3D);
    p.message.args[0]=subimage;p.message.args[1]=target;p.message.args[2]=(uint64_t)(int64_t)level;
    p.message.args[3]=(uint64_t)(int64_t)internal;p.message.args[4]=(uint64_t)(int64_t)x;p.message.args[5]=(uint64_t)(int64_t)y;
    p.message.args[6]=(uint64_t)(int64_t)width;p.message.args[7]=(uint64_t)(int64_t)height;
    p.message.args[8]=(uint64_t)(int64_t)border;p.message.args[9]=format;p.message.args[10]=type;
    p.message.args[11]=(uintptr_t)offset;p.message.args[12]=(uint64_t)(int64_t)depth;p.message.args[13]=(uint64_t)(int64_t)z;
    DGSubmit(c,&p,0);
}
static void DGPBOPixel(struct DGContext *c,unsigned kind,GLenum target,GLint level,GLint internal,
    GLint x,GLint y,GLsizei width,GLsizei height,GLint border,GLenum format,GLenum type,const void *offset)
{
    if(!DGPBOAvailable(c))return;
    struct DGPacket p=DGPacketNew(c,DG_PBO_PIXEL_2D);
    p.message.args[0]=kind;p.message.args[1]=target;p.message.args[2]=(uint64_t)(int64_t)level;
    p.message.args[3]=(uint64_t)(int64_t)internal;p.message.args[4]=(uint64_t)(int64_t)x;p.message.args[5]=(uint64_t)(int64_t)y;
    p.message.args[6]=(uint64_t)(int64_t)width;p.message.args[7]=(uint64_t)(int64_t)height;
    p.message.args[8]=(uint64_t)(int64_t)border;p.message.args[9]=format;p.message.args[10]=type;
    p.message.args[11]=(uintptr_t)offset;DGSubmit(c,&p,0); // Native PBO reads/writes remain asynchronous.
}
