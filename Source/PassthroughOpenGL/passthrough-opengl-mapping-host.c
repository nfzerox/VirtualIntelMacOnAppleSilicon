/* Native CPU mapping pointers are never serialized. A lease is accepted only
 * after querying the current target's real native mapping again. In particular
 * IsBuffer(name)==false does not kill a retained, deleted-name object. */
struct DGHostMapping {
    struct DGMapInfo info;void *pointer;unsigned char *seed;GLuint buffer;
    struct DGHostMapping *next;
};
static void DGRemoveHostMap(struct DGAliasGroup *group,struct DGHostMapping *map)
{
    struct DGHostMapping **link=&group->maps;while(*link && *link!=map)link=&(*link)->next;
    if(*link) { *link=map->next;group->map_bytes-=map->info.length;--group->map_count;free(map->seed);free(map); }
}
static void DGDestroyMappings(struct DGAliasGroup *group)
{ while(group->maps)DGRemoveHostMap(group,group->maps); }
static GLenum DGMappingNativeInfo(struct DGHostContext *c,GLenum target,void **pointer,struct DGMapInfo *info,GLuint *buffer)
{
    glGetBufferPointerv(target,GL_BUFFER_MAP_POINTER,pointer);GLenum error=glGetError();
    if(error || !*pointer)return error;
    GLint64 size=0,offset=0,length=0;GLint name=0;GLenum binding=0;
    if(c->profile==kCGLOGLPVersion_Legacy) {
        // Public GL3's 64-bit getter is not a valid GL2 dispatch entry. A
        // compatibility whole-buffer map has its actual size and zero offset.
        GLint narrow=0;glGetBufferParameteriv(target,GL_BUFFER_SIZE,&narrow);size=length=narrow;
    } else {
        dg_core_api.buffer_parameter64(target,GL_BUFFER_SIZE,&size);
        dg_core_api.buffer_parameter64(target,0x9121,&offset); // BUFFER_MAP_OFFSET
        dg_core_api.buffer_parameter64(target,0x9120,&length); // BUFFER_MAP_LENGTH
    }
    error=glGetError();if(error)return error;
    if(size<0 || offset<0 || length<0 || offset>size || length>size-offset)return GL_INVALID_OPERATION;
    switch(target) {
    case 0x8892:binding=0x8894;break;case 0x8893:binding=0x8895;break;
    case 0x88eb:binding=0x88ed;break;case 0x88ec:binding=0x88ef;break;
    case 0x8a11:binding=0x8a28;break;case 0x8c2a:binding=0x8c2a;break;
    case 0x8c8e:binding=0x8c8f;break;case 0x8f36:case 0x8f37:binding=target;break;
    case 0x8f3f:binding=0x8f43;break;
    default:break;
    }
    if(binding) { glGetIntegerv(binding,&name);error=glGetError();if(error)return error; }
    info->size=size;info->offset=offset;info->length=length;*buffer=(GLuint)name;return GL_NO_ERROR;
}
static struct DGHostMapping *DGCurrentHostMap(struct DGHostContext *c,GLenum target,GLenum *error)
{
    struct DGMapInfo info={0};void *pointer=NULL;GLuint buffer=0;
    *error=DGMappingNativeInfo(c,target,&pointer,&info,&buffer);if(*error || !pointer)return NULL;
    for(struct DGHostMapping *map=c->alias_group->maps;map;map=map->next)
        if(map->pointer==pointer && map->buffer==buffer && map->info.size==info.size &&
           (c->profile==kCGLOGLPVersion_Legacy || (map->info.offset==info.offset && map->info.length==info.length)))return map;
    *error=GL_INVALID_OPERATION;return NULL;
}
static GLenum DGHostMapBegin(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    if(m->args[4]>1 || m->args[0]>UINT32_MAX || m->args[1]>UINT32_MAX)return GL_INVALID_VALUE;
    struct DGAliasGroup *group=c->alias_group;
    // VectorKit legitimately keeps hundreds of small tile buffers mapped.
    // A fixed256-object cap rejected Maps at only3MB. Bound actual bytes
    // and allocation/lease exhaustion, not an unrelated number of objects.
    if(group->next_map==UINT32_MAX){
        if(dg_diagnostics&&c->diagnostic_errors++<8)fprintf(stderr,"direct mapping limit count=%u bytes=%zu requested=%lld\n",group->map_count,group->map_bytes,(long long)m->args[3]);
        return GL_OUT_OF_MEMORY;
    }
    GLenum target=(GLenum)m->args[0];int ranged=(int)m->args[4];uint32_t access=(uint32_t)m->args[1];
    GLint64 size=0;
    if(c->profile==kCGLOGLPVersion_Legacy) { GLint narrow=0;glGetBufferParameteriv(target,GL_BUFFER_SIZE,&narrow);size=narrow; }
    else dg_core_api.buffer_parameter64(target,GL_BUFFER_SIZE,&size);
    GLenum error=glGetError();if(error)return error;
    int64_t offset=ranged?(int64_t)m->args[2]:0,length=ranged?(int64_t)m->args[3]:size;
    uint32_t flags=ranged?access:access==GL_READ_ONLY?GL_MAP_READ_BIT:access==GL_WRITE_ONLY?GL_MAP_WRITE_BIT:
        access==GL_READ_WRITE?(GL_MAP_READ_BIT|GL_MAP_WRITE_BIT):0;
    int valid=size>=0 && offset>=0 && length>=0 && offset<=size && length<=size-offset;
    if(valid && (uint64_t)length>DG_MAX_BYTES)return GL_OUT_OF_MEMORY;
    unsigned char *seed=NULL;GLint mapped=0;glGetBufferParameteriv(target,GL_BUFFER_MAPPED,&mapped);
    error=glGetError();if(error)return error;
    if(valid && !mapped && length && (flags&GL_MAP_WRITE_BIT) && !(flags&GL_MAP_READ_BIT) &&
       !(flags&(GL_MAP_INVALIDATE_RANGE_BIT|GL_MAP_INVALIDATE_BUFFER_BIT))) {
        // Write-only is not invalidation. Preserve bytes the application will
        // not write, without illegally reading from its write-only mapping.
        seed=malloc((size_t)length);if(!seed)return GL_OUT_OF_MEMORY;
        glGetBufferSubData(target,(GLintptr)offset,(GLsizeiptr)length,seed);error=glGetError();
        if(error) { free(seed);return error; }
    }
    struct DGHostMapping *map=calloc(1,sizeof(*map));if(!map) { free(seed);return GL_OUT_OF_MEMORY; }
    map->pointer=ranged?dg_core_api.map_range(target,(GLintptr)offset,(GLsizeiptr)length,access):glMapBuffer(target,access);
    error=glGetError();
    if(error || !map->pointer) {
        if(dg_diagnostics&&c->diagnostic_errors++<8)fprintf(stderr,"direct native map error=%x count=%u bytes=%zu offset=%lld length=%lld access=%x\n",error,group->map_count,group->map_bytes,(long long)offset,(long long)length,access);
        free(seed);free(map);return error;
    }
    error=DGMappingNativeInfo(c,target,&map->pointer,&map->info,&map->buffer);
    if(error) { glUnmapBuffer(target);glGetError();free(seed);free(map);return error; }
    if(ranged && c->profile==kCGLOGLPVersion_Legacy) { map->info.offset=offset;map->info.length=length; }
    // Nonempty live native maps cannot occupy the same address. Reuse is a
    // retirement witness; zero-size maps are kept separate by native name.
    for(struct DGHostMapping *old=group->maps,*next;old;old=next) {
        next=old->next;
        if(old->pointer==map->pointer && (old->info.length || map->info.length)) {
            reply->result=old->info.lease;DGRemoveHostMap(group,old);break;
        }
    }
    if(map->info.length>DG_MAX_BYTES || map->info.length>DG_MAP_BUDGET-group->map_bytes) {
        if(dg_diagnostics&&c->diagnostic_errors++<8)fprintf(stderr,"direct mapping byte limit count=%u bytes=%zu requested=%llu\n",group->map_count,group->map_bytes,(unsigned long long)map->info.length);
        glUnmapBuffer(target);glGetError();free(seed);free(map);return GL_OUT_OF_MEMORY;
    }
    map->seed=seed;map->info.access=flags|(seed?DG_MAP_SEEDED:0);map->info.lease=++group->next_map;
    map->next=group->maps;group->maps=map;++group->map_count;group->map_bytes+=map->info.length;
    memcpy(m+1,&map->info,sizeof(map->info));return GL_NO_ERROR;
}
static int DGHostMappingCommand(struct DGHostContext *c,struct DGMessage *m,struct DGReply *reply)
{
    if((m->op==DG_MAP_BEGIN || m->op==DG_MAP_INFO)?m->bytes!=sizeof(struct DGMapInfo):
       (m->op!=DG_MAP_READ && m->op!=DG_MAP_WRITE && m->bytes))return -1;
    GLenum error=GL_NO_ERROR;
    if(c->inside_begin)error=GL_INVALID_OPERATION;
    else if(!c->alias_group || !dg_core_api.buffer_parameter64 || !dg_core_api.map_range || !dg_core_api.flush_range)error=GL_INVALID_OPERATION;
    else {
        GLenum prior=glGetError();if(prior && !c->error)c->error=prior;
        if(m->op==DG_MAP_BEGIN)error=DGHostMapBegin(c,m,reply);
        else {
            GLenum target=(GLenum)m->args[0];struct DGHostMapping *map=DGCurrentHostMap(c,target,&error);
            if(m->op==DG_MAP_INFO) {
                if(map)memcpy(m+1,&map->info,sizeof(map->info));
            } else if(m->op==DG_MAP_END && !map && !error) {
                reply->result=glUnmapBuffer(target);error=glGetError();
            } else if(!error && (!map || m->args[1]!=map->info.lease))error=GL_INVALID_OPERATION;
            else if(!error) {
                uint64_t offset=m->args[2],length=m->bytes;
                switch(m->op) {
                case DG_MAP_READ:case DG_MAP_WRITE:
                    if(offset>map->info.length || length>map->info.length-offset)error=GL_INVALID_VALUE;
                    else if(m->op==DG_MAP_READ) {
                        const unsigned char *source=map->seed?map->seed:map->pointer;
                        if(!(map->info.access&(GL_MAP_READ_BIT|DG_MAP_SEEDED)))error=GL_INVALID_OPERATION;
                        else if(length)memcpy(m+1,source+(size_t)offset,(size_t)length);
                    } else if(!(map->info.access&GL_MAP_WRITE_BIT))error=GL_INVALID_OPERATION;
                    else if(length) { memcpy((unsigned char *)map->pointer+(size_t)offset,m+1,(size_t)length);DGPBOWritten(c); }
                    break;
                case DG_MAP_READY:
                    free(map->seed);map->seed=NULL;map->info.access&=~DG_MAP_SEEDED;break;
                case DG_MAP_FLUSH: {
                    int apple=m->args[4]!=0;length=m->args[3];uint64_t base=map->info.offset;
                    if(m->args[4]>1 || (int64_t)offset<0 || (int64_t)length<0 || base>map->info.size ||
                       offset>map->info.size-base || length>map->info.size-base-offset)error=GL_INVALID_VALUE;
                    else if(apple) { glFlushMappedBufferRangeAPPLE(target,(GLintptr)offset,(GLsizeiptr)length);error=glGetError(); }
                    else if(map->info.access&GL_MAP_FLUSH_EXPLICIT_BIT) {
                        // Mavericks validates against full storage. Only the
                        // intersection can have legally been written by the
                        // guest; never read beyond its actual mapped window.
                        uint64_t start=offset<map->info.length?offset:map->info.length;
                        uint64_t count=length<map->info.length-start?length:map->info.length-start;
                        if(count)dg_core_api.flush_range(target,(GLintptr)start,(GLsizeiptr)count);
                        error=glGetError();
                    }
                    // Old GLEngine accepts non-explicit core flush. The
                    // ordinary mapping's writes are committed by native
                    // unmap; this is not a fabricated GPU completion.
                    break;
                }
                case DG_MAP_END:
                    reply->result=glUnmapBuffer(target);error=glGetError();
                    if(!error)DGRemoveHostMap(c->alias_group,map);break;
                default:return -1;
                }
            }
        }
    }
    if(m->flags&DG_REPLY)reply->error=error;
    else if(error && !c->error)c->error=error;
    return 0;
}
