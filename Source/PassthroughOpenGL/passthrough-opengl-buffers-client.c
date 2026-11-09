/* Native buffer access. A failed query/read leaves caller memory untouched.
 * Large transfers stage one native operation, not partially applied chunks. */
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
static int DGLargeBufferTransfer(struct DGContext *c,unsigned mode,GLenum target,
    GLintptr offset,GLsizeiptr size,void *data,GLenum usage)
{
    if(!c)return 0;
    if(c->common_state_blocked || !(c->connection->features2&DG_FEATURE2_LARGE_BUFFER_TRANSFER)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return 0;
    }
    if(offset<0 || size<0 || (size && !data)){
        if(!c->error)c->error=GL_INVALID_VALUE;return 0;
    }
    if((uint64_t)size>UINTPTR_MAX-(uintptr_t)data){
        if(!c->error)c->error=GL_OUT_OF_MEMORY;return 0;
    }
    struct DGPacket begin=DGPacketNew(c,DG_BUFFER_TRANSFER);
    begin.message.args[0]=mode;begin.message.args[1]=target;
    begin.message.args[2]=(uint64_t)offset;begin.message.args[3]=(uint64_t)size;begin.message.args[4]=usage;
    DGSubmit(c,&begin,1);if(!begin.valid_reply)return 0;
    uint64_t cursor=0;int success=1;
    while(cursor<(uint64_t)size){
        uint32_t bytes=(uint64_t)size-cursor>DG_TEXTURE_READ_CHUNK_BYTES?
            DG_TEXTURE_READ_CHUNK_BYTES:(uint32_t)((uint64_t)size-cursor);
        struct DGPacket chunk=DGPacketNew(c,DG_BUFFER_TRANSFER);
        chunk.message.args[0]=mode==DG_BUFFER_STAGE_READ?DG_BUFFER_STAGE_FETCH:DG_BUFFER_STAGE_WRITE;
        chunk.message.args[2]=cursor;chunk.message.bytes=bytes;
        chunk.data=(unsigned char *)data+(size_t)cursor;chunk.capacity=bytes;
        chunk.message.flags=mode==DG_BUFFER_STAGE_READ?DG_OUTPUT_ONLY:DG_INPUT_ONLY;
        DGSubmit(c,&chunk,1);
        if(!chunk.valid_reply){success=0;break;}
        cursor+=bytes;
    }
    if(mode!=DG_BUFFER_STAGE_READ || !success){
        struct DGPacket end=DGPacketNew(c,DG_BUFFER_TRANSFER);
        end.message.args[0]=success?DG_BUFFER_STAGE_COMMIT:DG_BUFFER_STAGE_CANCEL;
        DGSubmit(c,&end,1);success=success&&end.valid_reply;
    }
    return success;
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
static int DGReadBufferData(struct DGContext *c,GLenum target,GLintptr offset,GLsizeiptr size,void *data)
{
    if(!DGBufferAccess(c))return 0;
    if(offset<0 || size<0 || (size && !data)) { c->error=GL_INVALID_VALUE;return 0; }
    if((uint64_t)size>DG_MAX_BYTES)return DGLargeBufferTransfer(c,DG_BUFFER_STAGE_READ,target,offset,size,data,0);
    struct DGPacket p=DGPacketNew(c,DG_BUFFER_READ);
    p.message.flags=DG_OUTPUT_ONLY;p.message.bytes=(uint32_t)size;p.data=data;p.capacity=(size_t)size;
    p.message.args[0]=target;p.message.args[1]=(uint64_t)(int64_t)offset;
    DGSubmit(c,&p,1); // Borrow caller storage; never free it or upload its old contents.
    return p.valid_reply;
}
void glGetBufferSubData(GLenum target,GLintptr offset,GLsizeiptr size,void *data)
{ DGReadBufferData(dg_current,target,offset,size,data); }
void glCopyBufferSubData(GLenum readtarget,GLenum writetarget,GLintptr readoffset,GLintptr writeoffset,GLsizeiptr size)
{
    struct DGContext *c=dg_current;if(!DGBufferAccess(c))return;
    struct DGPacket p=DGPacketNew(c,DG_BUFFER_COPY);
    p.message.args[0]=readtarget;p.message.args[1]=writetarget;
    p.message.args[2]=(uint64_t)(int64_t)readoffset;p.message.args[3]=(uint64_t)(int64_t)writeoffset;
    p.message.args[4]=(uint64_t)(int64_t)size;DGSubmit(c,&p,0);
}
