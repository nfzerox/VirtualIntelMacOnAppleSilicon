/* Copy a tight native snapshot into only the caller's actual pixel bits.
 * Pure layout operation: no context, native driver or transport access. */
static size_t DGPackRasterBitmap(unsigned char *pixels,unsigned width,unsigned rows,int lsb)
{
    size_t stride=((size_t)width+7)/8;
    for(unsigned row=0;row<rows;++row){
        const unsigned char *source=pixels+(size_t)row*width;
        for(size_t byte=0;byte<stride;++byte){
            unsigned value=0;
            for(unsigned bit=0;bit<8 && byte*8+bit<width;++bit)
                value|=(unsigned)(source[byte*8+bit]&1u)<<(lsb?bit:7-bit);
            /* Write after consuming the source bits: compaction is safe
             * in-place even when the first output overlaps the input. */
            pixels[(size_t)row*stride+byte]=(unsigned char)value;
        }
    }
    return stride*rows;
}
static void DGCopyRasterRead(unsigned char *destination,const unsigned char *source,size_t bytes,
    uint64_t position,const struct DGRasterLayout *layout,unsigned width,int bitmap,int lsb)
{
    uint64_t source_row=bitmap?((uint64_t)width+7)/8:layout->row_bytes;
    while(bytes){
        uint64_t row=position/source_row,column=position%source_row;
        size_t count=source_row-column<bytes?(size_t)(source_row-column):bytes;
        unsigned char *out=destination+(size_t)(layout->offset+row*layout->stride+column);
        if(bitmap){
            unsigned shift=layout->bit_offset;
            for(size_t i=0;i<count;++i){
                uint64_t remaining=(uint64_t)width-(column+i)*8;
                unsigned n=remaining<8?(unsigned)remaining:8;
                unsigned mask=lsb?(1u<<n)-1u:((255u<<(8-n))&255u);
                unsigned value=source[i]&mask;
                unsigned first_mask=lsb?(mask<<shift)&255u:mask>>shift;
                unsigned first=lsb?(value<<shift)&255u:value>>shift;
                out[i]=(unsigned char)((out[i]&~first_mask)|first);
                if(shift+n>8){
                    unsigned last_mask=lsb?mask>>(8-shift):(mask<<(8-shift))&255u;
                    unsigned last=lsb?value>>(8-shift):(value<<(8-shift))&255u;
                    out[i+1]=(unsigned char)((out[i+1]&~last_mask)|last);
                }
            }
        }else memcpy(out,source,count);
        position+=count;source+=count;bytes-=count;
    }
}
