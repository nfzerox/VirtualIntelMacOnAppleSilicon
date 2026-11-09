/* Native GL retains only host allocations. The public pointer remains
 * owned by the guest, and results become visible at RenderMode. */
static void DGRegisterRenderBuffer(struct DGContext *c,unsigned op,int which,
    GLsizei count,GLenum type,void *pointer)
{
    GLenum error=GL_NO_ERROR;
    if(c->profile!=0x1000 || c->common_state_blocked ||
       !(c->connection->features2&DG_FEATURE2_RETAINED_RENDER))error=GL_INVALID_OPERATION;
    else if(count<0 || (count && !pointer))error=GL_INVALID_VALUE;
    else if((uint32_t)count>DG_MAX_BYTES/4)error=GL_OUT_OF_MEMORY;
    if(error){if(!c->error)c->error=error;return;}
    struct DGPacket p=DGPacketNew(c,op);
    p.message.args[0]=(uint32_t)count;p.message.args[1]=type;
    DGSubmit(c,&p,1);
    if(p.valid_reply){
        c->render_buffers[which].pointer=pointer;c->render_buffers[which].words=(uint32_t)count;
    }
}
static GLint DGRenderMode(struct DGContext *c,unsigned op,GLenum mode)
{
    if(c->common_state_blocked){if(!c->error)c->error=GL_INVALID_OPERATION;return 0;}
    struct DGPacket p=DGPacketNew(c,op);p.message.args[0]=mode;
    if(!(c->connection->features2&DG_FEATURE2_RETAINED_RENDER))
        return (GLint)(uint32_t)DGSubmit(c,&p,1).result;
    int previous=DGRenderBufferIndex(c->render_mode);
    unsigned capacity=previous<0?0:c->render_buffers[previous].words;
    unsigned inline_words=capacity<DG_RENDER_INLINE_WORDS?capacity:DG_RENDER_INLINE_WORDS;
    struct {struct DGRenderModeReply status;uint32_t words[DG_RENDER_INLINE_WORDS];} result;
    p.message.args[1]=1;p.message.flags=DG_OUTPUT_ONLY;
    p.data=(void *)&result;p.message.bytes=p.capacity=sizeof(result.status)+inline_words*4;
    struct DGReply r=DGSubmit(c,&p,1);
    if(!p.valid_reply)return 0;
    c->render_mode=result.status.mode;
    unsigned written=(uint32_t)(r.result>>32);
    if(written>capacity){if(!c->error)c->error=GL_INVALID_OPERATION;return 0;}
    if(written){
        unsigned char *out=c->render_buffers[previous].pointer;
        unsigned inline_written=written<inline_words?written:inline_words;
        memcpy(out,result.words,inline_written*4);
        for(unsigned offset=inline_written;offset<written;){
            unsigned count=written-offset;
            if(count>DG_RENDER_CHUNK_WORDS)count=DG_RENDER_CHUNK_WORDS;
            struct DGPacket chunk=DGPacketNew(c,DG_RETAINED_RENDER_READ);
            chunk.message.args[0]=offset;chunk.message.flags=DG_OUTPUT_ONLY;
            chunk.data=out+offset*4;chunk.message.bytes=chunk.capacity=count*4;
            DGSubmit(c,&chunk,1);
            if(!chunk.valid_reply)return 0;
            offset+=count;
        }
    }
    if(result.status.error && !c->error)c->error=result.status.error;
    return (GLint)(uint32_t)r.result;
}
