/* Large pack layouts must not serialize skipped/padded caller storage.
 * Native GL snapshots once; replies contain consecutive touched row bytes. */
#include "passthrough-opengl-raster-copy.h"
/* The state is always 128 bytes; even a very large caller layout needs only
 * one small native snapshot, not allocation/serialization of its gaps. */
static int DGReadPolygonStipple(struct DGContext *c,void *pixels)
{
    if(!c || c->pixel_buffer[1] || !(c->connection->features3&DG_FEATURE3_STIPPLE_READ))return 0;
    if(c->profile!=0x1000 || c->common_state_blocked){if(!c->error)c->error=GL_INVALID_OPERATION;return 1;}
    if(!pixels){if(!c->error)c->error=GL_INVALID_VALUE;return 1;}
    struct DGRasterLayout layout=DGRasterLayout(32,32,1,c->row_length[1],c->alignment[1],
        c->skip_rows[1],c->skip_pixels[1],c->row_bytes[1]);
    if(layout.span==UINT64_MAX || layout.span>UINTPTR_MAX-(uintptr_t)pixels){
        if(!c->error)c->error=GL_OUT_OF_MEMORY;return 1;}
    GLint lsb=0;glGetIntegerv(GL_PACK_LSB_FIRST,&lsb);
    unsigned char tight[128];struct DGPacket p=DGPacketNew(c,DG_STIPPLE_READ);
    p.message.flags=DG_OUTPUT_ONLY;DGPacketPointer(&p,0,tight,sizeof(tight),1);
    DGSubmit(c,&p,1);
    if(p.valid_reply){DGPacketCopy(&p,0,tight,sizeof(tight));
        DGCopyRasterRead(pixels,tight,sizeof(tight),0,&layout,32,1,lsb);}
    DGPacketFree(&p);return 1;
}
static int DGLargeReadPixels(struct DGContext *c,GLint x,GLint y,GLsizei width,GLsizei height,
    GLenum format,GLenum type,void *pixels)
{
    if(!c || c->pixel_buffer[1])return 0;
    struct DGRasterLayout layout=DGRasterLayout(width,height,DGRasterPixelBits(format,type),
        c->row_length[1],c->alignment[1],c->skip_rows[1],c->skip_pixels[1],c->row_bytes[1]);
    if(layout.span==UINT64_MAX || !layout.span ||
       (layout.span<=DG_MAX_BYTES && (type!=GL_BITMAP || !(c->connection->features2&DG_FEATURE2_RASTER_STREAM))))return 0;
    if(c->common_state_blocked || !(c->connection->features2&DG_FEATURE2_RASTER_STREAM)){
        if(!c->error)c->error=GL_INVALID_OPERATION;return 1;
    }
    if(!pixels){if(!c->error)c->error=GL_INVALID_VALUE;return 1;}
    if(layout.span>UINTPTR_MAX-(uintptr_t)pixels || layout.row_bytes>SIZE_MAX/layout.rows){
        if(!c->error)c->error=GL_OUT_OF_MEMORY;return 1;
    }
    size_t source_row=type==GL_BITMAP?((size_t)width+7)/8:(size_t)layout.row_bytes;
    size_t total=source_row*layout.rows;
    size_t capacity=total>DG_TEXTURE_READ_CHUNK_BYTES?DG_TEXTURE_READ_CHUNK_BYTES:total;
    unsigned char *scratch=malloc(capacity);if(!scratch){if(!c->error)c->error=GL_OUT_OF_MEMORY;return 1;}
    GLint lsb=0;if(type==GL_BITMAP)glGetIntegerv(GL_PACK_LSB_FIRST,&lsb);
    struct DGPacket begin=DGPacketNew(c,DG_RASTER_READ_STAGE);
    begin.message.args[0]=(uint64_t)(int64_t)x;begin.message.args[1]=(uint64_t)(int64_t)y;
    begin.message.args[2]=(uint32_t)width;begin.message.args[3]=(uint32_t)height;
    begin.message.args[4]=format;begin.message.args[5]=type;
    begin.message.args[6]=total;begin.message.args[7]=layout.bit_offset;
    DGSubmit(c,&begin,1);if(!begin.valid_reply){free(scratch);return 1;}
    size_t position=0;
    while(position<total){
        size_t count=total-position>capacity?capacity:total-position;
        struct DGPacket chunk=DGPacketNew(c,DG_BUFFER_TRANSFER);
        chunk.message.args[0]=DG_BUFFER_STAGE_FETCH;chunk.message.args[2]=position;
        chunk.message.flags=DG_OUTPUT_ONLY;chunk.message.bytes=(uint32_t)count;
        chunk.data=scratch;chunk.capacity=count;DGSubmit(c,&chunk,1);
        if(!chunk.valid_reply){
            struct DGPacket cancel=DGPacketNew(c,DG_BUFFER_TRANSFER);
            cancel.message.args[0]=DG_BUFFER_STAGE_CANCEL;DGSubmit(c,&cancel,1);break;
        }
        DGCopyRasterRead(pixels,scratch,count,position,&layout,(unsigned)width,type==GL_BITMAP,lsb);
        position+=count;
    }
    free(scratch);return 1;
}
