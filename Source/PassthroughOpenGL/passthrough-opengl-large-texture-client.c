/* Large CPU texture inputs use ordered native PBO calls. Each data packet
 * stays bounded and the actual image operation executes once. */
struct DGLargeTextureInput { GLuint buffer,previous;GLenum prior,error;GLint unpack[7],normalized[7];int unpack_saved; };
static const GLenum dg_large_unpack_keys[]={GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,
    GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_ROW_BYTES_APPLE,
    GL_UNPACK_IMAGE_HEIGHT,GL_UNPACK_SKIP_IMAGES};
struct DGLargeTextureLayout { uint64_t span,offset,row_stride,image_stride,row_bytes;unsigned rows,images,bit_offset,bitmap; };
static struct DGLargeTextureLayout DGLargeTextureLayout(struct DGContext *c,unsigned dimensions,
    GLsizei width,GLsizei height,GLsizei depth,GLenum format,GLenum type)
{
    struct DGLargeTextureLayout layout={UINT64_MAX,0,0,0,0,0,0};
    if(!c)return layout;
    if(dimensions==3)layout.span=DGPixelSpan3DWide(width,height,depth,format,type,
        c->row_length[0],c->alignment[0],c->skip_rows[0],c->skip_pixels[0],
        c->row_bytes[0],c->image_height[0],c->skip_images[0]);
    else layout.span=DGPixelSpanWide(width,dimensions==1?1:height,format,type,c->row_length[0],c->alignment[0],
        c->skip_rows[0],c->skip_pixels[0],c->row_bytes[0]);
    if(layout.span==UINT64_MAX||!layout.span)return layout;
    uint64_t bits=DGRasterPixelBits(format,type),alignment=(uint64_t)c->alignment[0];
    layout.bit_offset=(unsigned)((uint64_t)c->skip_pixels[0]*bits%8);layout.bitmap=type==GL_BITMAP;
    layout.row_bytes=((uint64_t)width*bits+layout.bit_offset+7)/8;layout.rows=dimensions==1?1:(unsigned)height;
    layout.images=dimensions==3?(unsigned)depth:1;
    layout.row_stride=c->row_bytes[0]?(uint64_t)c->row_bytes[0]:
        (((((uint64_t)(c->row_length[0]?c->row_length[0]:width)*bits+7)/8)+alignment-1)/alignment)*alignment;
    layout.image_stride=dimensions==3?(uint64_t)(c->image_height[0]?c->image_height[0]:height)*layout.row_stride:
        (uint64_t)layout.rows*layout.row_stride;
    layout.offset=(uint64_t)c->skip_pixels[0]*bits/8;
    layout.offset+=(uint64_t)c->skip_rows[0]*layout.row_stride;
    if(dimensions==3)layout.offset+=(uint64_t)c->skip_images[0]*layout.image_stride;
    return layout;
}
/* Small actual uploads need neither a staging GL object nor its round trips.
 * Keep native endian/format conversion and bitmap bit order unchanged. */
static int DGCompactTextureInput(struct DGContext *c,struct DGPacket *p,int index,
    const void *pixels,const struct DGLargeTextureLayout *layout)
{
    if(!(c->connection->features3&DG_FEATURE3_COMPACT_TEXTURE) || !pixels ||
       layout->span==UINT64_MAX || !layout->span || !layout->rows || !layout->images)return 0;
    int contiguous=(layout->rows==1 || layout->row_stride==layout->row_bytes) &&
        (layout->images==1 || layout->image_stride==(uint64_t)layout->rows*layout->row_bytes);
    if(!layout->offset && contiguous)return 0;
    if(layout->row_bytes>DG_MAX_BYTES/layout->rows ||
       layout->row_bytes*layout->rows>DG_MAX_BYTES/layout->images)return 0;
    if(c->common_state_blocked){if(!c->error)c->error=GL_INVALID_OPERATION;return 1;}
    if(layout->span>UINTPTR_MAX-(uintptr_t)pixels){if(!c->error)c->error=GL_OUT_OF_MEMORY;return 1;}
    size_t bytes=(size_t)(layout->row_bytes*layout->rows*layout->images);
    const unsigned char *source=(const unsigned char *)pixels+(size_t)layout->offset;
    p->message.args[15]=2;p->message.args[14]=layout->bit_offset;p->message.args[13]=layout->row_bytes;
    if(contiguous)
        DGPacketPointer(p,index,source,bytes,0);
    else {
        DGPacketPointer(p,index,source,bytes,1);
        if(!p->failed){
            unsigned char *out=p->data+(size_t)p->message.args[index]-1;
            for(unsigned z=0;z<layout->images;++z)for(unsigned y=0;y<layout->rows;++y){
                memcpy(out,source+(size_t)((uint64_t)z*layout->image_stride+(uint64_t)y*layout->row_stride),
                    (size_t)layout->row_bytes);out+=(size_t)layout->row_bytes;
            }
        }
    }
    DGSubmit(c,p,0);return 1;
}
static int DGLargeTextureRange(struct DGContext *c,const unsigned char *pixels,uint64_t offset,uint64_t bytes)
{
    while(bytes){
        size_t count=bytes>DG_TEXTURE_READ_CHUNK_BYTES?DG_TEXTURE_READ_CHUNK_BYTES:(size_t)bytes;
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER,(GLintptr)offset,(GLsizeiptr)count,
            pixels);
        if(c->connection->dead)return 0;
        offset+=count;pixels+=count;bytes-=count;
    }
    return 1;
}
static void DGLargeTextureEnd(struct DGContext *c,struct DGLargeTextureInput *input)
{
    GLenum error=glGetError();if(!input->error)input->error=error;
    if(input->buffer){
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,input->previous);
        if(input->unpack_saved)for(unsigned i=0;i<7;++i)
            if(input->unpack[i]!=input->normalized[i])glPixelStorei(dg_large_unpack_keys[i],input->unpack[i]);
        glDeleteBuffers(1,&input->buffer);
        error=glGetError();if(!input->error)input->error=error;
    }
    if(input->prior)c->error=input->prior;
    else if(input->error)c->error=input->error;
}
/* 0 ordinary codec; 1 invoke with PBO offset zero then End; -1 cleaned failure. */
static int DGLargeTextureBegin(struct DGContext *c,const void *pixels,const struct DGLargeTextureLayout *layout,
    struct DGLargeTextureInput *input)
{
    uint64_t span=layout->span;
    if(!c||!pixels||c->pixel_buffer[0]||span==UINT64_MAX||span<=DG_MAX_BYTES)return 0;
    if(c->common_state_blocked){if(!c->error)c->error=GL_INVALID_OPERATION;return -1;}
    if(!(c->connection->features2&DG_FEATURE2_LARGE_TEXTURE_INPUT) ||
       (layout->bitmap&&!(c->connection->features2&DG_FEATURE2_RASTER_STREAM))){
        if(!c->error)c->error=GL_INVALID_OPERATION;return -1;
    }
    if(span>UINTPTR_MAX-(uintptr_t)pixels){
        if(!c->error)c->error=GL_OUT_OF_MEMORY;return -1;
    }
    uint64_t bytes=layout->row_bytes;
    if(!layout->rows||!layout->images||bytes>(uint64_t)INTPTR_MAX/layout->rows ||
       bytes*layout->rows>(uint64_t)INTPTR_MAX/layout->images){
        if(!c->error)c->error=GL_OUT_OF_MEMORY;return -1;}
    bytes*=layout->rows;bytes*=layout->images;
    memset(input,0,sizeof(*input));input->previous=c->pixel_buffer[0];input->prior=glGetError();
    glGenBuffers(1,&input->buffer);input->error=glGetError();
    if(!input->buffer||input->error){if(!input->error)input->error=GL_OUT_OF_MEMORY;goto failed;}
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,input->buffer);input->error=glGetError();
    if(input->error)goto failed;
    glBufferData(GL_PIXEL_UNPACK_BUFFER,(GLsizeiptr)bytes,NULL,GL_STREAM_DRAW);
    input->error=glGetError();if(input->error)goto failed;
    /* Skips/padding may be inaccessible guard pages. Pack only touched bytes.
     * Contiguous input stays borrowed; padded layouts coalesce bounded chunks
     * rather than making one expensive guest/host call per source row. */
    if(layout->row_stride==layout->row_bytes&&layout->image_stride==(uint64_t)layout->rows*layout->row_bytes){
        if(!DGLargeTextureRange(c,(const unsigned char *)pixels+(size_t)layout->offset,0,bytes)){
            input->error=GL_INVALID_OPERATION;goto failed;}
    }else {
        size_t capacity=bytes>DG_TEXTURE_READ_CHUNK_BYTES?DG_TEXTURE_READ_CHUNK_BYTES:(size_t)bytes;
        unsigned char *scratch=malloc(capacity);if(!scratch){input->error=GL_OUT_OF_MEMORY;goto failed;}
        size_t used=0;uint64_t written=0;int live=1;
        for(unsigned z=0;live&&z<layout->images;++z)for(unsigned y=0;live&&y<layout->rows;++y){
            const unsigned char *source=(const unsigned char *)pixels+(size_t)(layout->offset+
                (uint64_t)z*layout->image_stride+(uint64_t)y*layout->row_stride);
            uint64_t left=layout->row_bytes;
            while(live&&left){size_t count=left>capacity-used?capacity-used:(size_t)left;
                memcpy(scratch+used,source,count);used+=count;source+=count;left-=count;
                if(used==capacity){live=DGLargeTextureRange(c,scratch,written,used);written+=used;used=0;}}
        }
        if(live&&used)live=DGLargeTextureRange(c,scratch,written,used);
        free(scratch);if(!live){input->error=GL_INVALID_OPERATION;goto failed;}
    }
    input->error=glGetError();if(input->error)goto failed;
    input->unpack[0]=c->alignment[0];input->unpack[1]=c->row_length[0];
    input->unpack[2]=c->skip_rows[0];input->unpack[3]=c->skip_pixels[0];
    input->unpack[4]=c->row_bytes[0];input->unpack[5]=c->image_height[0];input->unpack[6]=c->skip_images[0];
    input->unpack_saved=1;
    input->normalized[0]=1;input->normalized[3]=(GLint)layout->bit_offset;
    input->normalized[4]=layout->bit_offset?(GLint)layout->row_bytes:0;
    for(unsigned i=0;i<7;++i)if(input->unpack[i]!=input->normalized[i])
        glPixelStorei(dg_large_unpack_keys[i],input->normalized[i]);
    return 1;
failed:
    DGLargeTextureEnd(c,input);return -1;
}
