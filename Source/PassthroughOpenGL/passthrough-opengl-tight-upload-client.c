/* Generic GL pixel-store normalization, not a compositor special case. Only
 * actual requested texels cross the stream. Prepared rows stay pinned until
 * consumed; sockets/older devices gather an owned tight span before return.
 * The host restores real unpack state before processing another GL command.
 */
static volatile int dg_tight_uploads=1; /* debugger-only same-boot comparison */
static uint64_t dg_tight_upload_calls,dg_tight_upload_source_bytes,dg_tight_upload_payload_bytes;
static uint64_t dg_tight_upload_gathers,dg_tight_upload_copies;
static int DGTightTexSubImage2D(struct DGContext *c,GLenum target,GLint level,GLint x,GLint y,
    GLsizei width,GLsizei height,GLenum format,GLenum type,const void *pixels)
{
    if(!c || !dg_tight_uploads || c->common_state_blocked || c->pixel_buffer[0] || !pixels ||
       !(c->connection->capabilities&DG_CAP_TIGHT_TEX_SUB_IMAGE_2D) || width<=0 || height<=0)return 0;
    size_t span=DGPixelBytes(c,width,height,format,type,0),pixel=DGPixelSize(format,type);
    if(span==SIZE_MAX || !pixel)return 0;
    uint64_t row=(uint64_t)width*pixel,tight=row*height;
    uint64_t stride=c->row_bytes[0]?(uint64_t)c->row_bytes[0]:
        (((uint64_t)(c->row_length[0]?c->row_length[0]:width)*pixel+c->alignment[0]-1)/c->alignment[0])*c->alignment[0];
    uint64_t skip=(uint64_t)c->skip_rows[0]*stride+(uint64_t)c->skip_pixels[0]*pixel;
    // Keep small uploads, overlapping rows, unknown shapes and already-tight
    // input on the ordinary codec. Native validation remains authoritative.
    if(tight<65536 || tight>DG_MAX_BYTES || span<=tight || stride<row || skip>span ||
       row>span-skip || (uintptr_t)pixels>UINTPTR_MAX-span)return 0;
    struct DGPacket p=DGPacketNew(c,DG_TIGHT_TEX_SUB_IMAGE_2D);
    p.message.args[0]=target;p.message.args[1]=(uint32_t)level;
    p.message.args[2]=(uint32_t)x;p.message.args[3]=(uint32_t)y;
    p.message.args[4]=(uint32_t)width;p.message.args[5]=(uint32_t)height;
    p.message.args[6]=format;p.message.args[7]=type;p.message.args[8]=1;
    p.message.bytes=(uint32_t)tight;
    const unsigned char *source=(const unsigned char *)pixels+(size_t)skip;
    int gathered=DGUploadMessageRows(c,&p.message,source,(unsigned)row,(unsigned)height,(size_t)stride,0);
    if(gathered<0) {
        p.data=malloc((size_t)tight);
        if(!p.data){c->error=GL_OUT_OF_MEMORY;return 1;}
        p.capacity=(size_t)tight;
        for(int i=0;i<height;++i)memcpy(p.data+(size_t)i*row,source+(size_t)i*stride,(size_t)row);
        DGSubmit(c,&p,0);DGPacketFree(&p);
        __sync_fetch_and_add(&dg_tight_upload_copies,1);
    } else if(gathered)__sync_fetch_and_add(&dg_tight_upload_gathers,1);
    if(!c->connection->dead) {
        __sync_fetch_and_add(&dg_tight_upload_calls,1);
        __sync_fetch_and_add(&dg_tight_upload_source_bytes,span);
        __sync_fetch_and_add(&dg_tight_upload_payload_bytes,tight);
    }
    return 1;
}
