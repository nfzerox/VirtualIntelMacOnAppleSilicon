/* Share the bounded retained-transfer allocation with texture readback.
 * One native buffer operation owns validation and mutation/snapshot semantics;
 * intermediate packets never partially modify the actual GL buffer. */
static int DGHostBufferTransfer(struct DGHostContext *c,struct DGMessage *m,struct DGReply *r)
{
    unsigned mode=(unsigned)m->args[0];
    if(m->args[0]!=mode || mode<DG_BUFFER_STAGE_DATA || mode>DG_BUFFER_STAGE_CANCEL)return -1;
    if(c->inside_begin){r->error=GL_INVALID_OPERATION;return 0;}
    GLenum prior=glGetError();if(prior&&!c->error)c->error=prior;
    if(mode<=DG_BUFFER_STAGE_READ){
        if(m->bytes || m->flags!=DG_REPLY || m->args[1]>UINT32_MAX || m->args[4]>UINT32_MAX)return -1;
        DGReleaseTextureRead(c);
        if(m->args[2]>INTPTR_MAX || m->args[3]>INTPTR_MAX){r->error=GL_INVALID_VALUE;return 0;}
        if(m->args[3]<=DG_MAX_BYTES || m->args[3]>SIZE_MAX)return -1;
        if(!DGReserveTextureRead(c,(size_t)m->args[3])){r->error=GL_OUT_OF_MEMORY;return 0;}
        c->buffer_transfer_mode=mode;c->buffer_transfer_target=(GLenum)m->args[1];
        c->buffer_transfer_offset=(GLintptr)m->args[2];c->buffer_transfer_usage=(GLenum)m->args[4];
        if(mode==DG_BUFFER_STAGE_READ){
            glGetBufferSubData(c->buffer_transfer_target,c->buffer_transfer_offset,
                (GLsizeiptr)c->texture_read_bytes,c->texture_read_stage);
            r->error=glGetError();if(r->error)DGReleaseTextureRead(c);
        }
        return 0;
    }
    if(mode==DG_BUFFER_STAGE_CANCEL){
        if(m->bytes || m->flags!=DG_REPLY)return -1;
        DGReleaseTextureRead(c);return 0;
    }
    if(!c->buffer_transfer_mode || !c->texture_read_stage){r->error=GL_INVALID_OPERATION;return 0;}
    if(mode==DG_BUFFER_STAGE_COMMIT){
        if(m->bytes || m->flags!=DG_REPLY)return -1;
        if(c->buffer_transfer_mode==DG_BUFFER_STAGE_READ || c->texture_read_offset!=c->texture_read_bytes){
            r->error=GL_INVALID_OPERATION;
        }else {
            if(c->buffer_transfer_mode==DG_BUFFER_STAGE_DATA)
                glBufferData(c->buffer_transfer_target,(GLsizeiptr)c->texture_read_bytes,
                    c->texture_read_stage,c->buffer_transfer_usage);
            else glBufferSubData(c->buffer_transfer_target,c->buffer_transfer_offset,
                    (GLsizeiptr)c->texture_read_bytes,c->texture_read_stage);
            r->error=glGetError();DGPBOWritten(c);
        }
        DGReleaseTextureRead(c);return 0;
    }
    int reading=mode==DG_BUFFER_STAGE_FETCH;
    if(m->flags!=(DG_REPLY|(reading?DG_OUTPUT_ONLY:DG_INPUT_ONLY)) ||
       !m->bytes || m->bytes>DG_TEXTURE_READ_CHUNK_BYTES)return -1;
    if(reading!=(c->buffer_transfer_mode==DG_BUFFER_STAGE_READ) ||
       m->args[2]!=c->texture_read_offset || m->bytes>c->texture_read_bytes-c->texture_read_offset){
        DGReleaseTextureRead(c);r->error=GL_INVALID_OPERATION;return 0;
    }
    if(reading)memcpy(m+1,c->texture_read_stage+c->texture_read_offset,m->bytes);
    else memcpy(c->texture_read_stage+c->texture_read_offset,m+1,m->bytes);
    c->texture_read_offset+=m->bytes;
    if(reading&&c->texture_read_offset==c->texture_read_bytes)DGReleaseTextureRead(c);
    return 0;
}
