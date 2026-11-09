/* GLEngine can write before RenderMode; it must never retain wire storage
 * or a guest virtual address. Only GL's written prefix crosses the bridge. */
static int DGHostRegisterRenderBuffer(struct DGHostContext *c,struct DGMessage *m,
    struct DGReply *reply,int which)
{
    if(m->bytes || m->flags!=DG_REPLY || m->args[0]>DG_MAX_BYTES/4 ||
       m->args[1]>UINT32_MAX || (!which && m->args[1]))return -1;
    if(c->inside_begin || c->profile!=kCGLOGLPVersion_Legacy){
        reply->error=GL_INVALID_OPERATION;return 0;
    }
    GLsizei words=(GLsizei)m->args[0];
    void *next=calloc(words?words:1,4);
    if(!next){reply->error=GL_OUT_OF_MEMORY;return 0;}
    GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
    if(which)glFeedbackBuffer(words,(GLenum)m->args[1],next);
    else glSelectBuffer(words,next);
    reply->error=glGetError();
    if(reply->error){
        /* Do not free a pointer the driver actually adopted if a second
         * earlier native error remained pending after the first drain. */
        void *registered=NULL;
        glGetPointerv(which?GL_FEEDBACK_BUFFER_POINTER:GL_SELECTION_BUFFER_POINTER,&registered);
        if(registered!=next){free(next);return 0;}
        if(!c->error)c->error=reply->error;
        reply->error=GL_NO_ERROR;
    }
    if(c->render_staged==c->render_buffers[which].data){
        c->render_staged=NULL;c->render_staged_words=0;
    }
    free(c->render_buffers[which].data);
    c->render_buffers[which].data=next;c->render_buffers[which].words=(uint32_t)words;
    return 0;
}
static uint32_t DGHostRenderWritten(int which,const void *data,uint32_t capacity,GLint result)
{
    if(!data || !capacity)return 0;
    if(result==-1)return capacity; /* Native writes the partial final record. */
    if(result<=0)return 0;
    if(which==1)return (uint32_t)result<=capacity?(uint32_t)result:0;
    /* Selection returns record count, not GLuint count. Each record has a
     * length and depth pair, followed by that many names. Bound the walk. */
    if((uint32_t)result>capacity/3)return 0;
    const GLuint *words=data;uint32_t at=0;
    for(GLint hit=0;hit<result;++hit){
        if(capacity-at<3 || words[at]>capacity-at-3)return 0;
        at+=3+words[at];
    }
    return at;
}
static int DGHostRenderMode(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    if(m->args[0]>UINT32_MAX || m->args[1]>1)return -1;
    int previous=DGRenderBufferIndex(c->render_mode);
    uint32_t capacity=previous<0?0:c->render_buffers[previous].words;
    uint32_t inline_words=capacity<DG_RENDER_INLINE_WORDS?capacity:DG_RENDER_INLINE_WORDS;
    int modern=!!m->args[1];
    if(modern?(m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) ||
               m->bytes!=sizeof(struct DGRenderModeReply)+inline_words*4):
              (m->bytes || m->flags!=DG_REPLY))return -1;
    c->render_staged=NULL;c->render_staged_words=0;
    GLenum error=GL_NO_ERROR;
    GLint result=0;
    if(c->inside_begin || c->profile!=kCGLOGLPVersion_Legacy)error=GL_INVALID_OPERATION;
    else {
        GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
        result=glRenderMode((GLenum)m->args[0]);
        error=glGetError();
        GLint actual=GL_RENDER;glGetIntegerv(GL_RENDER_MODE,&actual);c->render_mode=(GLenum)actual;
    }
    reply->result=(uint32_t)result;
    if(!modern){if(error && !c->error)c->error=error;return 0;}
    struct DGRenderModeReply *status=(void *)(m+1);
    status->mode=c->render_mode?c->render_mode:GL_RENDER;status->error=error;
    /* An invalid transition can still finish the old mode and return hits.
     * Keep its GL error separate from the transport status and copyback. */
    if(previous>=0){
        const void *data=c->render_buffers[previous].data;
        uint32_t written=DGHostRenderWritten(previous,data,capacity,result);
        reply->result|=(uint64_t)written<<32;
        if(written){
            uint32_t first=written<inline_words?written:inline_words;
            memcpy(status+1,data,first*4);
            if(written>first){
                c->render_staged=data;c->render_staged_words=written;
                c->render_stage_next=first;
            }
        }
    }
    return 0;
}
static int DGHostRenderRead(struct DGHostContext *c,struct DGMessage *m)
{
    if(m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) || !m->bytes || m->bytes%4 ||
       m->bytes>DG_RENDER_CHUNK_WORDS*4 || !c->render_staged ||
       m->args[0]!=c->render_stage_next || m->bytes/4>c->render_staged_words-c->render_stage_next)return -1;
    memcpy(m+1,(const unsigned char *)c->render_staged+c->render_stage_next*4,m->bytes);
    c->render_stage_next+=m->bytes/4;
    if(c->render_stage_next==c->render_staged_words)c->render_staged=NULL;
    return 0;
}
static void DGHostFreeRenderBuffers(struct DGHostContext *c)
{
    free(c->render_buffers[0].data);free(c->render_buffers[1].data);
}
