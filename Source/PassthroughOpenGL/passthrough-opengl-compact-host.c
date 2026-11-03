/* Validate the ENTIRE compact envelope before executing its first command.
 * Native semantic errors still occur at the same ordered GL command. This
 * is a lossless header codec, not local GL validation, batching across a
 * public observation, a new renderer or permission to release CPU storage. */
static int DGInterpretCompact(struct DGInterpreter *s,int fd,const struct DGMessage *m)
{
    if(!s->hello || s->service_only || m->context || m->flags || m->args[0]!=1 ||
       m->args[1]<2 || m->bytes<2*sizeof(struct DGCompactScalar) ||
       m->bytes>DG_BATCH_BYTES-sizeof(*m) || m->args[1]>m->bytes/sizeof(struct DGCompactScalar))return -1;
    for(unsigned i=2;i<16;++i)if(m->args[i])return -1;
    const unsigned char *body=(const void *)(m+1);size_t offset=0;
    for(uint64_t i=0;i<m->args[1];++i){
        if(sizeof(struct DGCompactScalar)>m->bytes-offset)return -1;
        struct DGCompactScalar r;memcpy(&r,body+offset,sizeof(r));offset+=sizeof(r);
        if(r.op>=974 || r.words>16 || !r.context || r.context>=256 ||
           r.words*sizeof(uint64_t)>m->bytes-offset)return -1;
        offset+=r.words*sizeof(uint64_t);
    }
    if(offset!=m->bytes)return -1;
#ifdef VIM_PASSTHROUGH_OPENGL_COMMAND_PROFILE
    DGCommandProfileEnd(DGCommandProfileBegin(),DG_COMPACT_SCALARS,m->bytes,3);
#endif
    offset=0;
    for(uint64_t i=0;i<m->args[1];++i){
        struct DGCompactScalar r;memcpy(&r,body+offset,sizeof(r));offset+=sizeof(r);
        struct DGMessage decoded={.op=r.op,.context=r.context};
        memcpy(decoded.args,body+offset,r.words*sizeof(uint64_t));offset+=r.words*sizeof(uint64_t);
        if(DGInterpret(s,fd,&decoded))return -1;
    }
    return 0;
}
