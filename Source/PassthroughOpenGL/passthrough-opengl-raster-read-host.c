/* Capture one native read, retaining transfer operations and byte swapping.
 * Byte formats use tight pack spacing; bitmap serialization avoids Apple's
 * broken native bit packer without replacing native stencil/index transfer. */
#include "passthrough-opengl-raster-copy.h"
static GLenum DGNativeStippleCPU(unsigned char pixels[128])
{
    GLint binding=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);
    if(binding)return GL_INVALID_OPERATION;
    static const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS,GL_PACK_ROW_BYTES_APPLE};
    GLint saved[5],tight[]={1,0,0,0,0};
    for(unsigned i=0;i<5;++i)glGetIntegerv(keys[i],&saved[i]);
    GLenum error=glGetError();if(error)return error;
    for(unsigned i=0;i<5;++i)if(saved[i]!=tight[i])glPixelStorei(keys[i],tight[i]);
    glGetPolygonStipple(pixels);error=glGetError();
    for(unsigned i=0;i<5;++i)if(saved[i]!=tight[i])glPixelStorei(keys[i],saved[i]);
    GLenum restore=glGetError();return error?error:restore;
}
/* A context-local state query must not unbind a deleted-but-retained PBO.
 * Copy just the native stipple state to a lazily created, independent CGL
 * context. No guest namespace objects or application stack slots are used. */
static GLenum DGNativeStippleSnapshot(struct DGHostContext *c,unsigned char pixels[128],GLint lsb)
{
    if(!c->stipple_query_context){
        CGLError created=CGLCreateContext(CGLGetPixelFormat(c->cgl),NULL,&c->stipple_query_context);
        if(created!=kCGLNoError)return created==kCGLBadAlloc?GL_OUT_OF_MEMORY:GL_INVALID_OPERATION;
    }
    GLint source_screen=0,query_screen=0;
    if(CGLGetVirtualScreen(c->cgl,&source_screen)!=kCGLNoError ||
       CGLGetVirtualScreen(c->stipple_query_context,&query_screen)!=kCGLNoError ||
       (source_screen!=query_screen && CGLSetVirtualScreen(c->stipple_query_context,source_screen)!=kCGLNoError))
        return GL_INVALID_OPERATION;
    if(CGLCopyContext(c->cgl,c->stipple_query_context,GL_POLYGON_STIPPLE_BIT)!=kCGLNoError ||
       CGLSetCurrentContext(c->stipple_query_context)!=kCGLNoError)return GL_INVALID_OPERATION;
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_LSB_FIRST,lsb);
    glGetPolygonStipple(pixels);GLenum error=glGetError();
    if(CGLSetCurrentContext(c->cgl)!=kCGLNoError)return GL_INVALID_OPERATION;
    return error;
}
static void DGHostGetPolygonStipple(struct DGHostContext *c,GLubyte *pixels)
{
    GLenum prior=glGetError();if(prior&&!c->error)c->error=prior;
    GLint binding=0;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);
    /* The generated raster decoder already validated profile, Begin/End,
     * mapping and the complete destination extent against this actual PBO. */
    struct DGPixelState state={0};DGNativePixelState(&state);
    GLint lsb=0;glGetIntegerv(GL_PACK_LSB_FIRST,&lsb);
    GLenum error=glGetError();if(error){if(!c->error)c->error=error;return;}
    struct DGRasterLayout layout=DGRasterLayout(32,32,1,state.row_length[1],state.alignment[1],
        state.skip_rows[1],state.skip_pixels[1],state.row_bytes[1]);
    unsigned char tight[128],row[5];
    error=binding?DGNativeStippleSnapshot(c,tight,lsb):DGNativeStippleCPU(tight);
    uint64_t offset=(uintptr_t)pixels;
    if(!error && !binding)DGCopyRasterRead(pixels,tight,sizeof(tight),0,&layout,32,1,lsb);
    else if(!error && !layout.bit_offset && layout.stride==4){
        glBufferSubData(GL_PIXEL_PACK_BUFFER,(GLintptr)(offset+layout.offset),sizeof(tight),tight);
        error=glGetError();if(!error)DGPBOWritten(c);
    }else if(!error){
        struct DGRasterLayout one=layout;one.offset=0;one.rows=1;
        for(unsigned y=0;y<32&&!error;++y){
            uint64_t destination=offset+layout.offset+(uint64_t)y*layout.stride;
            if(layout.bit_offset){
                glGetBufferSubData(GL_PIXEL_PACK_BUFFER,(GLintptr)destination,(GLsizeiptr)layout.row_bytes,row);
                error=glGetError();if(error)break;
                DGCopyRasterRead(row,tight+y*4,4,0,&one,32,1,lsb);
            }
            glBufferSubData(GL_PIXEL_PACK_BUFFER,(GLintptr)destination,(GLsizeiptr)layout.row_bytes,
                layout.bit_offset?row:tight+y*4);
            error=glGetError();if(!error)DGPBOWritten(c);
        }
    }
    if(error&&!c->error)c->error=error;
}
static int DGHostStippleRead(struct DGHostContext *c,struct DGMessage *m,struct DGReply *r)
{
    if(m->flags!=(DG_REPLY|DG_OUTPUT_ONLY) || m->bytes!=128 || m->args[0]!=1)return -1;
    if(c->inside_begin || c->profile!=kCGLOGLPVersion_Legacy){r->error=GL_INVALID_OPERATION;return 0;}
    GLenum prior=glGetError();if(prior&&!c->error)c->error=prior;
    r->error=DGNativeStippleCPU((unsigned char *)(m+1));
    return 0;
}
static int DGHostRasterRead(struct DGHostContext *c,struct DGMessage *m,struct DGReply *r)
{
    if(m->flags!=DG_REPLY || m->bytes)return -1;
    GLint x=(GLint)(int64_t)m->args[0],y=(GLint)(int64_t)m->args[1];
    GLsizei width=(GLsizei)m->args[2],height=(GLsizei)m->args[3];
    GLenum format=(GLenum)m->args[4],type=(GLenum)m->args[5];
    if(m->args[0]!=(uint64_t)(int64_t)x || m->args[1]!=(uint64_t)(int64_t)y ||
       m->args[2]!=(uint64_t)(int64_t)width || m->args[3]!=(uint64_t)(int64_t)height ||
       m->args[4]!=format || m->args[5]!=type)return -1;
    if(c->inside_begin){r->error=GL_INVALID_OPERATION;return 0;}
    if(type==GL_BITMAP && c->profile!=kCGLOGLPVersion_Legacy){r->error=GL_INVALID_ENUM;return 0;}
    GLenum prior=glGetError();if(prior&&!c->error)c->error=prior;
    struct DGPixelState state={0};DGNativePixelState(&state);
    r->error=glGetError();if(r->error)return 0;
    if(state.buffers[1]){r->error=GL_INVALID_OPERATION;return 0;}
    struct DGRasterLayout layout=DGRasterLayout(width,height,DGRasterPixelBits(format,type),
        state.row_length[1],state.alignment[1],state.skip_rows[1],state.skip_pixels[1],state.row_bytes[1]);
    if(layout.span==UINT64_MAX || (layout.span<=DG_MAX_BYTES&&type!=GL_BITMAP) || !layout.rows ||
       layout.row_bytes>SIZE_MAX/layout.rows)return -1;
    size_t row=type==GL_BITMAP?((size_t)width+7)/8:(size_t)layout.row_bytes;
    size_t bytes=row*layout.rows;
    if(m->args[6]!=bytes || m->args[7]!=layout.bit_offset){r->error=GL_INVALID_OPERATION;return 0;}
    size_t allocation=bytes;
    if(type==GL_BITMAP){
        if((size_t)width>SIZE_MAX/(size_t)height){r->error=GL_OUT_OF_MEMORY;return 0;}
        allocation=(size_t)width*(size_t)height;
    }
    if(!DGReserveTextureRead(c,allocation)){r->error=GL_OUT_OF_MEMORY;return 0;}
    static const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS,GL_PACK_ROW_BYTES_APPLE};
    GLint saved[]={state.alignment[1],state.row_length[1],state.skip_rows[1],state.skip_pixels[1],state.row_bytes[1]};
    GLint tight[]={1,0,0,0,0};
    for(unsigned i=0;i<5;++i)if(saved[i]!=tight[i])glPixelStorei(keys[i],tight[i]);
    GLint lsb=0;if(type==GL_BITMAP)glGetIntegerv(GL_PACK_LSB_FIRST,&lsb);
    /* Native Apple GL's BITMAP pack uses an unpacked-byte row stride and
     * ignores sub-byte skips. A byte-sized native index has the same final
     * low bit after native transfer/maps; compact it without that packer.
     * CPU work is confined to rare bitmap reads, not ordinary RGBA paths. */
    glReadPixels(x,y,width,height,format,type==GL_BITMAP?GL_UNSIGNED_BYTE:type,c->texture_read_stage);
    r->error=glGetError();
    for(unsigned i=0;i<5;++i)if(saved[i]!=tight[i])glPixelStorei(keys[i],saved[i]);
    GLenum restore=glGetError();if(!r->error)r->error=restore;
    if(r->error)DGReleaseTextureRead(c);else {
        if(type==GL_BITMAP)c->texture_read_bytes=DGPackRasterBitmap(c->texture_read_stage,
            (unsigned)width,(unsigned)height,lsb);
        c->buffer_transfer_mode=DG_BUFFER_STAGE_READ;
    }
    return 0;
}

/* Public bitmap reads have to preserve partial destination bytes as well as
 * pack-buffer lifetime. Never push onto the application's client stack: it
 * may be full. A deleted name can remain bound in another shared context,
 * and rebinding that integer would create a different buffer object. */
static GLenum DGHostBitmapPBO(struct DGHostContext *c,GLint x,GLint y,GLsizei width,GLsizei height,
    GLenum format,uint64_t offset,GLuint binding,const struct DGPixelState *state)
{
    struct DGRasterLayout layout=DGRasterLayout(width,height,1,state->row_length[1],
        state->alignment[1],state->skip_rows[1],state->skip_pixels[1],state->row_bytes[1]);
    if(layout.span==UINT64_MAX)return GL_INVALID_OPERATION;
    size_t tight_row=((size_t)width+7)/8;
    int named=glIsBuffer(binding);
    size_t snapshot;
    if(named){
        if((size_t)width>SIZE_MAX/(size_t)height)return GL_OUT_OF_MEMORY;
        snapshot=(size_t)width*(size_t)height;
    }else snapshot=tight_row;
    if(layout.row_bytes>SIZE_MAX-snapshot ||
       !DGReserveTextureRead(c,snapshot+(size_t)layout.row_bytes))return GL_OUT_OF_MEMORY;
    static const GLenum keys[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,
        GL_PACK_SKIP_PIXELS,GL_PACK_ROW_BYTES_APPLE};
    GLint saved[]={state->alignment[1],state->row_length[1],state->skip_rows[1],state->skip_pixels[1],state->row_bytes[1]};
    GLint tight[]={1,0,0,0,0},lsb=0;glGetIntegerv(GL_PACK_LSB_FIRST,&lsb);
    for(unsigned i=0;i<5;++i)if(saved[i]!=tight[i])glPixelStorei(keys[i],tight[i]);
    GLenum error=glGetError();
    if(!error&&named){
        /* Shared contexts are serialized on the same interpreter connection;
         * the live name cannot be deleted between these native calls. Read
         * once. Do not map the destination: even a temporary native mapping
         * changes the application's remembered BUFFER_ACCESS state. */
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
        glReadPixels(x,y,width,height,format,GL_UNSIGNED_BYTE,c->texture_read_stage);
        error=glGetError();glBindBuffer(GL_PIXEL_PACK_BUFFER,binding);
        GLenum restore=glGetError();if(!error)error=restore;
        if(!error)DGPackRasterBitmap(c->texture_read_stage,(unsigned)width,(unsigned)height,lsb);
    }
    if(!error){
        /* Keep the deleted-but-retained object bound throughout. A one-row
         * native bitmap read avoids Apple's erroneous multi-row byte stride.
         * Save/merge its partial bytes, not the skipped/padded buffer regions. */
        unsigned char *original=c->texture_read_stage+snapshot;
        struct DGRasterLayout one=layout;one.offset=0;one.rows=1;
        for(unsigned row=0;row<(unsigned)height&&!error;++row){
            uint64_t destination=offset+layout.offset+(uint64_t)row*layout.stride;
            unsigned char *source=c->texture_read_stage+(named?(size_t)row*tight_row:0);
            glGetBufferSubData(GL_PIXEL_PACK_BUFFER,(GLintptr)destination,(GLsizeiptr)layout.row_bytes,original);
            error=glGetError();if(error)break;
            if(!named){
                int64_t source_y=(int64_t)y+row;
                glReadPixels(x,source_y>INT32_MAX?INT32_MAX:(GLint)source_y,width,1,format,GL_BITMAP,
                    (void *)(uintptr_t)destination);
                error=glGetError();if(error)break;DGPBOWritten(c);
                glGetBufferSubData(GL_PIXEL_PACK_BUFFER,(GLintptr)destination,(GLsizeiptr)tight_row,source);
                error=glGetError();if(error)break;
            }
            DGCopyRasterRead(original,source,tight_row,0,&one,(unsigned)width,1,lsb);
            glBufferSubData(GL_PIXEL_PACK_BUFFER,(GLintptr)destination,(GLsizeiptr)layout.row_bytes,original);
            error=glGetError();if(!error)DGPBOWritten(c);
        }
    }
    for(unsigned i=0;i<5;++i)if(saved[i]!=tight[i])glPixelStorei(keys[i],saved[i]);
    GLenum restore=glGetError();if(!error)error=restore;
    DGReleaseTextureRead(c);return error;
}
