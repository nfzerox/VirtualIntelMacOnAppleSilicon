/* Carry only touched imaging bytes. Prefixes/padding may be inaccessible;
 * separable outputs must retain their actual row/column aliasing. */
static int DGCompactImaging(struct DGContext *c,struct DGPacket *p,enum DGImagingTransfer op,
    GLint width,GLint height,size_t bits,int pack,const int indices[2],const void *const pixels[2])
{
    if(!(c->connection->features3&DG_FEATURE3_COMPACT_IMAGING))return 0;
    struct DGRasterLayout layout[2];uint64_t bytes[2]={0,0};uintptr_t start[2]={0,0};int needed=0;
    unsigned images=indices[1]>=0?2:1;
    for(unsigned i=0;i<images;++i){
        layout[i]=DGImagingRasterLayout(op,width,height,bits,c->row_length[pack],c->alignment[pack],
            c->skip_rows[pack],c->skip_pixels[pack],c->row_bytes[pack],i);
        if(layout[i].span==UINT64_MAX || (layout[i].span&&!pixels[i]))return 0;
        if(layout[i].span>UINTPTR_MAX-(uintptr_t)pixels[i]){
            if(!c->error)c->error=GL_OUT_OF_MEMORY;return 1;
        }
        if(layout[i].rows && layout[i].row_bytes>DG_MAX_BYTES/layout[i].rows)return 0;
        bytes[i]=layout[i].row_bytes*layout[i].rows;
        start[i]=(uintptr_t)pixels[i]+layout[i].offset;
        needed|=layout[i].offset!=0 || (layout[i].rows>1 && layout[i].stride!=layout[i].row_bytes);
    }
    if(!needed)return 0;
    /* Two separable vectors are each contiguous. Preserve overlapping
     * actual ranges in one packet so native write ordering is unchanged. */
    unsigned first=start[1]<start[0]?1:0,second=1-first;
    uint64_t delta=images==2?start[second]-start[first]:0;
    if(images==2 && bytes[first] && bytes[second] && delta<bytes[first]){
        uint64_t merged=delta+bytes[second]>bytes[first]?delta+bytes[second]:bytes[first];
        if(merged>DG_MAX_BYTES)return 0;
        p->message.args[15]=2;
        DGPacketPointer(p,indices[first],(const void *)start[first],(size_t)merged,pack?2:0);
        p->message.args[indices[second]]=p->message.args[indices[first]]+delta;
        DGSubmit(c,p,pack);
        if(pack)DGPacketCopy(p,indices[first],(void *)start[first],(size_t)merged);
        return 1;
    }
    uint64_t aligned=(bytes[0]+7)&~UINT64_C(7);
    if(images==2 && bytes[1] && (aligned>DG_MAX_BYTES || bytes[1]>DG_MAX_BYTES-aligned))return 0;
    p->message.args[15]=2;
    for(unsigned i=0;i<images;++i)if(bytes[i]){
        /* A stripped prefix does not make the actual vector noncontiguous.
         * Keep the existing large-input borrow optimization; seeded output
         * needs one copy, not an extra clear followed by another copy. */
        if(layout[i].rows<=1 || layout[i].stride==layout[i].row_bytes){
            DGPacketPointer(p,indices[i],(const void *)start[i],(size_t)bytes[i],pack?2:0);
            if(p->failed)break;
            continue;
        }
        DGPacketPointer(p,indices[i],(const void *)start[i],(size_t)bytes[i],1);
        if(p->failed)break;
        unsigned char *out=p->data+(size_t)p->message.args[indices[i]]-1;
        for(unsigned row=0;row<layout[i].rows;++row)
            memcpy(out+(size_t)row*layout[i].row_bytes,
                (const void *)(start[i]+(uint64_t)row*layout[i].stride),(size_t)layout[i].row_bytes);
    }
    DGSubmit(c,p,pack);
    if(pack && p->valid_reply)for(unsigned i=0;i<images;++i)if(bytes[i]){
        const unsigned char *source=p->data+(size_t)p->message.args[indices[i]]-1;
        for(unsigned row=0;row<layout[i].rows;++row)
            memcpy((void *)(start[i]+(uint64_t)row*layout[i].stride),
                source+(size_t)row*layout[i].row_bytes,(size_t)layout[i].row_bytes);
    }
    return 1;
}
