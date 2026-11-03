/* Bounded native buffer access. A failed query/read leaves caller memory
 * untouched. This does not yet implement or export a remote mapping pointer. */
static int DGBufferAccess(struct DGContext *c)
{
    if(!c)return 0;
    if(c->connection->capabilities&DG_CAP_BUFFER_QUERIES)return 1;
    c->error=GL_INVALID_OPERATION;return 0;
}
static int DGBufferQuery(struct DGContext *c,GLenum target,GLenum pname,int wide,GLint64 *value)
{
    if(!DGBufferAccess(c))return 0;
    struct DGPacket p=DGPacketNew(c,DG_BUFFER_QUERY);GLint64 result=0;
    p.message.flags=DG_OUTPUT_ONLY;p.message.bytes=sizeof(result);p.data=(void *)&result;p.capacity=sizeof(result);
    p.message.args[0]=target;p.message.args[1]=pname;p.message.args[2]=wide;
    DGSubmit(c,&p,1);if(p.valid_reply)*value=result;return p.valid_reply;
}
void glGetBufferParameteriv(GLenum target,GLenum pname,GLint *params)
{
    GLint64 result=0;if(!params) { if(dg_current)dg_current->error=GL_INVALID_VALUE;return; }
    if(DGBufferQuery(dg_current,target,pname,0,&result))*params=(GLint)result;
}
void glGetBufferParameteri64v(GLenum target,GLenum pname,GLint64 *params)
{
    GLint64 result=0;if(!params) { if(dg_current)dg_current->error=GL_INVALID_VALUE;return; }
    if(DGBufferQuery(dg_current,target,pname,1,&result))*params=result;
}
void glGetBufferSubData(GLenum target,GLintptr offset,GLsizeiptr size,void *data)
{
    struct DGContext *c=dg_current;if(!DGBufferAccess(c))return;
    if(offset<0 || size<0 || (size && !data)) { c->error=GL_INVALID_VALUE;return; }
    if((uint64_t)size>DG_MAX_BYTES) { c->error=GL_OUT_OF_MEMORY;return; }
    struct DGPacket p=DGPacketNew(c,DG_BUFFER_READ);
    p.message.flags=DG_OUTPUT_ONLY;p.message.bytes=(uint32_t)size;p.data=data;p.capacity=(size_t)size;
    p.message.args[0]=target;p.message.args[1]=(uint64_t)(int64_t)offset;
    DGSubmit(c,&p,1); // Borrow caller storage; never free it or upload its old contents.
}
void glCopyBufferSubData(GLenum readtarget,GLenum writetarget,GLintptr readoffset,GLintptr writeoffset,GLsizeiptr size)
{
    struct DGContext *c=dg_current;if(!DGBufferAccess(c))return;
    struct DGPacket p=DGPacketNew(c,DG_BUFFER_COPY);
    p.message.args[0]=readtarget;p.message.args[1]=writetarget;
    p.message.args[2]=(uint64_t)(int64_t)readoffset;p.message.args[3]=(uint64_t)(int64_t)writeoffset;
    p.message.args[4]=(uint64_t)(int64_t)size;DGSubmit(c,&p,0);
}
