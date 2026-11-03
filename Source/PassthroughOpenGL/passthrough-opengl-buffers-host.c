/* Actual native GL owns buffer validation/content. Only fixed-width values
 * and bounded bytes cross the wire, never a driver mapping/object pointer. */
static int DGHostBufferAccess(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    if((m->op==DG_BUFFER_QUERY && (m->bytes!=sizeof(GLint64) || m->args[2]>1)) ||
       (m->op==DG_BUFFER_COPY && m->bytes))return -1;
    GLenum error=GL_NO_ERROR;
    if(c->inside_begin)error=GL_INVALID_OPERATION;
    else {
        GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
        switch(m->op) {
        case DG_BUFFER_QUERY: {
            GLint64 result=0;
            if(m->args[2]) {
                if(!dg_core_api.buffer_parameter64)error=GL_INVALID_OPERATION;
                else dg_core_api.buffer_parameter64((GLenum)m->args[0],(GLenum)m->args[1],&result);
            } else {
                GLint narrow=0;glGetBufferParameteriv((GLenum)m->args[0],(GLenum)m->args[1],&narrow);result=narrow;
            }
            memcpy(m+1,&result,sizeof(result));break;
        }
        case DG_BUFFER_READ:
            glGetBufferSubData((GLenum)m->args[0],(GLintptr)(int64_t)m->args[1],m->bytes,m+1);break;
        case DG_BUFFER_COPY:
            if(!dg_core_api.copy_buffer)error=GL_INVALID_OPERATION;
            else dg_core_api.copy_buffer((GLenum)m->args[0],(GLenum)m->args[1],(GLintptr)(int64_t)m->args[2],
                (GLintptr)(int64_t)m->args[3],(GLsizeiptr)(int64_t)m->args[4]);
            DGPBOWritten(c);break;
        default:return -1;
        }
        GLenum native=glGetError();if(!error)error=native;
    }
    if(m->flags&DG_REPLY)reply->error=error;
    else if(error && !c->error)c->error=error;
    return 0;
}
