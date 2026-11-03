/* Connection-lock owned. Pack only negotiated, async zero-body GL messages.
 * First scalar stays an ordinary header; convert only when two records cost
 * no more than the original pair. This neither forces an extra submission
 * nor retains an application pointer. Mixed payloads keep their old path. */
static volatile int dg_compact_scalar_override=1; // Quiescent owned-process A/B only.
static unsigned DGCompactWords(const struct DGMessage *m)
{
    unsigned n=16;
    while(n>=4 && !(m->args[n-1]|m->args[n-2]|m->args[n-3]|m->args[n-4]))n-=4;
    while(n && !m->args[n-1])--n;
    return n;
}
static size_t DGCompactRecord(unsigned char *out,const struct DGMessage *m,unsigned words)
{
    struct DGCompactScalar r={(uint16_t)m->op,(uint16_t)words,m->context};
    memcpy(out,&r,sizeof(r));if(words)memcpy(out+sizeof(r),m->args,words*sizeof(uint64_t));
    return sizeof(r)+words*sizeof(uint64_t);
}
static int DGCompactAppend(struct DGConnection *c,const struct DGMessage *m)
{
    if(!dg_compact_scalar_override || !(c->capabilities&DG_CAP_COMPACT_SCALARS) ||
       m->op>=974 || m->flags || m->bytes || !m->context)return 0;
    unsigned words=DGCompactWords(m);size_t bytes=sizeof(struct DGCompactScalar)+words*sizeof(uint64_t);
    if(c->scalar_start && c->scalar_compact && bytes<=sizeof(c->batch)-c->used){
        size_t at=c->scalar_start-1;uint32_t body=0;uint64_t count=0;
        memcpy(&body,c->batch+at+offsetof(struct DGMessage,bytes),sizeof(body));
        memcpy(&count,c->batch+at+offsetof(struct DGMessage,args)+sizeof(uint64_t),sizeof(count));
        DGCompactRecord(c->batch+c->used,m,words);c->used+=bytes;body+=(uint32_t)bytes;++count;
        memcpy(c->batch+at+offsetof(struct DGMessage,bytes),&body,sizeof(body));
        memcpy(c->batch+at+offsetof(struct DGMessage,args)+sizeof(uint64_t),&count,sizeof(count));
        ++c->compact_commands;c->compact_saved_bytes+=sizeof(*m)-bytes;return 1;
    }
    if(c->scalar_start && !c->scalar_compact){
        size_t at=c->scalar_start-1;struct DGMessage first;memcpy(&first,c->batch+at,sizeof(first));
        unsigned first_words=DGCompactWords(&first);
        size_t body=sizeof(struct DGCompactScalar)+first_words*sizeof(uint64_t)+bytes;
        size_t total=sizeof(struct DGMessage)+body;
        if(total<=2*sizeof(struct DGMessage) && total<=sizeof(c->batch)-at){
            struct DGMessage header={.op=DG_COMPACT_SCALARS,.bytes=(uint32_t)body,.args={1,2}};
            memcpy(c->batch+at,&header,sizeof(header));
            size_t offset=at+sizeof(header);offset+=DGCompactRecord(c->batch+offset,&first,first_words);
            offset+=DGCompactRecord(c->batch+offset,m,words);c->used=offset;c->scalar_compact=1;
            c->compact_commands+=2;++c->compact_runs;c->compact_saved_bytes+=2*sizeof(*m)-total;return 1;
        }
    }
    // No useful pair yet (or the current compact run filled the batch).
    // Preserve the pending prefix and use exactly the old capacity rule.
    if(sizeof(*m)>sizeof(c->batch)-c->used && DGDrain(c))return -1;
    c->scalar_start=c->used+1;c->scalar_compact=0;
    memcpy(c->batch+c->used,m,sizeof(*m));c->used+=sizeof(*m);return 1;
}
