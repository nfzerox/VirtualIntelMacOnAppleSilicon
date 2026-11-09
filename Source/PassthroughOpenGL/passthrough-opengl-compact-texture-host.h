/* Compact CPU texture packets retain formats, byte swapping and native calls.
 * Only pixel-store addressing differs from the client's original storage. */
struct DGCompactUnpackScope { GLint saved[7],tight[7];int active; };
static const GLenum dg_compact_unpack_keys[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,
    GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ROW_BYTES_APPLE,
    GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES};
static size_t DGHostUnpackBytes(struct DGHostContext *c,struct DGMessage *m,unsigned dimensions,
    GLsizei width,GLsizei height,GLsizei depth,GLenum format,GLenum type)
{
    if(!m->args[15])return dimensions==3?DGPixelBytes3D(c,width,height,depth,format,type,0):
        DGPixelBytes(c,width,dimensions==1?1:height,format,type,0);
    if(m->args[15]!=2 || m->args[14]>7 || (type!=GL_BITMAP && m->args[14]))return SIZE_MAX;
    uint64_t bits=DGRasterPixelBits(format,type);
    if(!bits || width<0 || height<0 || depth<0)return SIZE_MAX;
    uint64_t row=((uint64_t)width*bits+m->args[14]+7)/8;
    if(m->args[13]!=row)return SIZE_MAX;
    uint64_t rows=dimensions==1?1:(unsigned)height,images=dimensions==3?(unsigned)depth:1;
    if(!width || !rows || !images)return 0;
    if(row>DG_MAX_BYTES/rows || row*rows>DG_MAX_BYTES/images)return SIZE_MAX;
    return (size_t)(row*rows*images);
}
static void DGHostCompactUnpackEnd(struct DGHostContext *c,struct DGCompactUnpackScope *scope)
{
    if(!scope->active)return;
    GLenum error=glGetError();if(error&&!c->error)c->error=error;
    for(unsigned i=0;i<7;++i)if(scope->saved[i]!=scope->tight[i])
        glPixelStorei(dg_compact_unpack_keys[i],scope->saved[i]);
    error=glGetError();if(error&&!c->error)c->error=error;
    scope->active=0;
}
static int DGHostCompactUnpackBegin(struct DGHostContext *c,struct DGMessage *m,
    struct DGCompactUnpackScope *scope)
{
    if(!m->args[15])return 1;
    GLenum prior=glGetError();if(prior&&!c->error)c->error=prior;
    for(unsigned i=0;i<7;++i)glGetIntegerv(dg_compact_unpack_keys[i],scope->saved+i);
    GLenum error=glGetError();if(error){if(!c->error)c->error=error;return 0;}
    scope->tight[0]=1;scope->tight[3]=(GLint)m->args[14];
    /* The decoder validates row bytes against dimensions/format before any
     * state change. Retain the containing-byte pitch of bit-skipped rows. */
    if(m->args[14])scope->tight[4]=(GLint)m->args[13];
    scope->active=1;
    for(unsigned i=0;i<7;++i)if(scope->saved[i]!=scope->tight[i])
        glPixelStorei(dg_compact_unpack_keys[i],scope->tight[i]);
    error=glGetError();if(error){if(!c->error)c->error=error;DGHostCompactUnpackEnd(c,scope);return 0;}
    return 1;
}
