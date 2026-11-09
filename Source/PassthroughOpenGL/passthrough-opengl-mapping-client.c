/* Guest-owned map memory, shared by contexts on this connection. Opaque
 * leases, not GLuint names or native addresses, identify actual mappings. */
struct DGClientMapping { struct DGMapInfo info;unsigned char *memory;struct DGClientMapping *next; };
static void DGDropClientMap(struct DGConnection *connection,uint32_t lease)
{
    struct DGClientMapping **link=&connection->maps;
    while(*link) {
        struct DGClientMapping *map=*link;
        if(map->info.lease==lease) {
            *link=map->next;connection->map_bytes-=map->info.length;free(map->memory);free(map);return;
        }link=&map->next;
    }
}
static void DGFreeClientMaps(struct DGConnection *connection)
{ while(connection->maps)DGDropClientMap(connection,connection->maps->info.lease); }
static struct DGClientMapping *DGClientMap(struct DGConnection *connection,uint32_t lease)
{ for(struct DGClientMapping *m=connection->maps;m;m=m->next)if(m->info.lease==lease)return m;return NULL; }
static int DGMapAvailable(struct DGContext *c)
{
    if(!c)return 0;
    // Mapping leases are a negotiated transport capability, not a newer-OS
    // GL feature. Older GLI adapters already expose the same typed buffer-map
    // entry points. An OS-version gate made native ARB VBO/PBO calls fail even
    // when the paired host supported and owned their actual mappings.
    if(c->connection->capabilities&DG_CAP_BUFFER_MAPPING)return 1;
    c->error=GL_INVALID_OPERATION;return 0;
}
static int DGGetMap(struct DGContext *c,GLenum target,struct DGMapInfo *info)
{
    struct DGPacket p=DGPacketNew(c,DG_MAP_INFO);
    p.message.args[0]=target;p.message.flags=DG_OUTPUT_ONLY;p.message.bytes=sizeof(*info);
    p.data=(void *)info;p.capacity=sizeof(*info);DGSubmit(c,&p,1);return p.valid_reply;
}
static int DGMapTransfer(struct DGContext *c,GLenum target,struct DGClientMapping *map,uint64_t start,uint64_t bytes,int writing)
{
    if(start>map->info.length || bytes>map->info.length-start) { c->error=GL_INVALID_VALUE;return 0; }
    while(bytes) {
        uint32_t n=bytes>DG_MAP_CHUNK?DG_MAP_CHUNK:(uint32_t)bytes;
        struct DGPacket p=DGPacketNew(c,writing?DG_MAP_WRITE:DG_MAP_READ);
        p.message.args[0]=target;p.message.args[1]=map->info.lease;p.message.args[2]=start;
        p.message.bytes=n;p.data=map->memory+(size_t)start;p.capacity=n;
        if(!writing)p.message.flags=DG_OUTPUT_ONLY;
        DGSubmit(c,&p,!writing);
        if(c->connection->dead || (!writing && !p.valid_reply))return 0;
        start+=n;bytes-=n;
    }return 1;
}
static void *DGMapBegin(struct DGContext *c,GLenum target,GLintptr offset,GLsizeiptr length,GLenum access,int ranged)
{
    if(!DGMapAvailable(c))return NULL;
    struct DGConnection *connection=c->connection;pthread_mutex_lock(&connection->map_lock);
    struct DGMapInfo info={0};struct DGPacket p=DGPacketNew(c,DG_MAP_BEGIN);
    p.message.flags=DG_OUTPUT_ONLY;p.message.bytes=sizeof(info);p.data=(void *)&info;p.capacity=sizeof(info);
    p.message.args[0]=target;p.message.args[1]=access;p.message.args[2]=(uint64_t)(int64_t)offset;
    p.message.args[3]=(uint64_t)(int64_t)length;p.message.args[4]=ranged;
    struct DGReply reply=DGSubmit(c,&p,1);
    // Native allocator address reuse proves that its previous lease no longer
    // names a live map. The executor reports only that opaque retired lease.
    if(!connection->dead && reply.result && reply.result<=UINT32_MAX)DGDropClientMap(connection,(uint32_t)reply.result);
    if(!p.valid_reply || !info.lease) { pthread_mutex_unlock(&connection->map_lock);return NULL; }
    struct DGClientMapping *map=NULL;
    if(info.length<=SIZE_MAX && info.length<=DG_MAP_BUDGET-connection->map_bytes) {
        map=calloc(1,sizeof(*map));
        if(map) {
            size_t bytes=info.length?(size_t)info.length:1;
            map->memory=(info.access&(GL_MAP_READ_BIT|DG_MAP_SEEDED))?malloc(bytes):calloc(1,bytes);
        }
    }
    if(!map || !map->memory) {
        if(map) { free(map->memory);free(map); }
        struct DGPacket cancel=DGPacketNew(c,DG_MAP_END);cancel.message.args[0]=target;cancel.message.args[1]=info.lease;
        DGSubmit(c,&cancel,1);c->error=GL_OUT_OF_MEMORY;pthread_mutex_unlock(&connection->map_lock);return NULL;
    }
    map->info=info;map->next=connection->maps;connection->maps=map;connection->map_bytes+=info.length;
    if((info.access&(GL_MAP_READ_BIT|DG_MAP_SEEDED)) && !DGMapTransfer(c,target,map,0,info.length,0)) {
        struct DGPacket cancel=DGPacketNew(c,DG_MAP_END);cancel.message.args[0]=target;cancel.message.args[1]=info.lease;
        DGSubmit(c,&cancel,1);DGDropClientMap(connection,info.lease);pthread_mutex_unlock(&connection->map_lock);return NULL;
    }
    struct DGPacket ready=DGPacketNew(c,DG_MAP_READY);ready.message.args[0]=target;ready.message.args[1]=info.lease;
    DGSubmit(c,&ready,0);DGFlushTransport(c); // One-way: release the host seed now, not at a later app command.
    void *result=map->memory;
    if(connection->dead) { DGDropClientMap(connection,info.lease);result=NULL; }
    pthread_mutex_unlock(&connection->map_lock);return result;
}
void *glMapBuffer(GLenum target,GLenum access)
{ return DGMapBegin(dg_current,target,0,0,access,0); }
void *glMapBufferRange(GLenum target,GLintptr offset,GLsizeiptr length,GLbitfield access)
{ return DGMapBegin(dg_current,target,offset,length,access,1); }
void glGetBufferPointerv(GLenum target,GLenum pname,void **params)
{
    struct DGContext *c=dg_current;if(!DGMapAvailable(c))return;
    if(pname!=GL_BUFFER_MAP_POINTER) { c->error=GL_INVALID_ENUM;return; }
    if(!params) { c->error=GL_INVALID_VALUE;return; }
    pthread_mutex_lock(&c->connection->map_lock);struct DGMapInfo info={0};
    if(DGGetMap(c,target,&info)) {
        struct DGClientMapping *map=DGClientMap(c->connection,info.lease);
        if(info.lease && !map)c->error=GL_INVALID_OPERATION;else *params=map?map->memory:NULL;
    }pthread_mutex_unlock(&c->connection->map_lock);
}
GLboolean glUnmapBuffer(GLenum target)
{
    struct DGContext *c=dg_current;if(!DGMapAvailable(c))return GL_FALSE;
    struct DGConnection *connection=c->connection;pthread_mutex_lock(&connection->map_lock);
    struct DGMapInfo info={0};if(!DGGetMap(c,target,&info)) { pthread_mutex_unlock(&connection->map_lock);return GL_FALSE; }
    struct DGClientMapping *map=DGClientMap(connection,info.lease);
    if(info.lease && !map) { c->error=GL_INVALID_OPERATION;pthread_mutex_unlock(&connection->map_lock);return GL_FALSE; }
    if(map && (info.access&GL_MAP_WRITE_BIT) && !(info.access&GL_MAP_FLUSH_EXPLICIT_BIT) &&
       !DGMapTransfer(c,target,map,0,info.length,1)) { pthread_mutex_unlock(&connection->map_lock);return GL_FALSE; }
    struct DGPacket p=DGPacketNew(c,DG_MAP_END);p.message.args[0]=target;p.message.args[1]=info.lease;
    struct DGReply r=DGSubmit(c,&p,1);
    if(p.valid_reply && info.lease)DGDropClientMap(connection,info.lease);
    GLboolean result=p.valid_reply && r.result?GL_TRUE:GL_FALSE;
    pthread_mutex_unlock(&connection->map_lock);return result;
}
static void DGFlushMap(struct DGContext *c,GLenum target,GLintptr offset,GLsizeiptr length,int apple)
{
    if(!DGMapAvailable(c))return;
    // Both native Mavericks profiles distinguish this deprecated extension:
    // its core dispatch rejects it, rather than publishing a mapped range.
    if(apple && c->profile!=0x1000) { c->error=GL_INVALID_OPERATION;return; }
    struct DGConnection *connection=c->connection;pthread_mutex_lock(&connection->map_lock);
    struct DGMapInfo info={0};if(!DGGetMap(c,target,&info)) { pthread_mutex_unlock(&connection->map_lock);return; }
    struct DGClientMapping *map=DGClientMap(connection,info.lease);
    if(!map) { c->error=GL_INVALID_OPERATION;pthread_mutex_unlock(&connection->map_lock);return; }
    uint64_t base=info.offset;
    if(offset<0 || length<0 || base>info.size || (uint64_t)offset>info.size-base || (uint64_t)length>info.size-base-(uint64_t)offset) {
        c->error=GL_INVALID_VALUE;pthread_mutex_unlock(&connection->map_lock);return;
    }
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 101100
    /* Held Iris Pro on El Capitan and native Metal GL validate the actual
     * mapped range and require EXPLICIT. The older Mavericks contract is
     * deliberately more permissive; do not apply that quirk to newer GLI. */
    if(!apple && !(info.access&GL_MAP_FLUSH_EXPLICIT_BIT)){
        if(!c->error)c->error=GL_INVALID_OPERATION;pthread_mutex_unlock(&connection->map_lock);return;
    }
    if(!apple && ((uint64_t)offset>info.length || (uint64_t)length>info.length-(uint64_t)offset)){
        if(!c->error)c->error=GL_INVALID_VALUE;pthread_mutex_unlock(&connection->map_lock);return;
    }
#endif
    uint64_t left=base+(uint64_t)offset,right=left+(uint64_t)length;
    if(left<info.offset)left=info.offset;if(right>info.offset+info.length)right=info.offset+info.length;
    if(right>left && (info.access&GL_MAP_WRITE_BIT) && !DGMapTransfer(c,target,map,left-info.offset,right-left,1)) {
        pthread_mutex_unlock(&connection->map_lock);return;
    }
    struct DGPacket p=DGPacketNew(c,DG_MAP_FLUSH);p.message.args[0]=target;p.message.args[1]=info.lease;
    p.message.args[2]=(uint64_t)(int64_t)offset;p.message.args[3]=(uint64_t)(int64_t)length;p.message.args[4]=apple;
    DGSubmit(c,&p,1);pthread_mutex_unlock(&connection->map_lock);
}
void glFlushMappedBufferRange(GLenum target,GLintptr offset,GLsizeiptr length)
{ DGFlushMap(dg_current,target,offset,length,0); }
void glFlushMappedBufferRangeAPPLE(GLenum target,GLintptr offset,GLsizeiptr size)
{ DGFlushMap(dg_current,target,offset,size,1); }
