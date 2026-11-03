/* Public opaque GLsync values are process-local names, not host pointers.
 * Connection-scoped lookup rejects names from an unrelated share group. */
struct DGClientSync { uint32_t name,remote;struct DGClientSync *next,*global_next; };
static uint32_t dg_sync_next;
static struct DGClientSync *dg_sync_names;
static pthread_mutex_t dg_sync_names_lock=PTHREAD_MUTEX_INITIALIZER;
static void DGUnregisterSyncName(struct DGClientSync *s)
{
    pthread_mutex_lock(&dg_sync_names_lock);struct DGClientSync **link=&dg_sync_names;
    while(*link && *link!=s)link=&(*link)->global_next;
    if(*link)*link=s->global_next;pthread_mutex_unlock(&dg_sync_names_lock);
}
static uint32_t DGNewSyncName(void)
{
    uint32_t before;
    do { before=__atomic_load_n(&dg_sync_next,__ATOMIC_RELAXED);if(before==UINT32_MAX)return 0; }
    while(!__sync_bool_compare_and_swap(&dg_sync_next,before,before+1));
    return before+1;
}
static int DGSyncAvailable(struct DGContext *c)
{
    if(!c)return 0;
    if(c->connection->capabilities&DG_CAP_SYNC_OBJECTS)return 1;
    c->error=GL_INVALID_OPERATION;return 0;
}
static uint32_t DGSyncRemote(struct DGContext *c,GLsync sync,int remove)
{
    struct DGConnection *connection=c->connection;uint32_t remote=0;
    uintptr_t name=(uintptr_t)sync;
    pthread_mutex_lock(&connection->lock);
    struct DGClientSync **link=&connection->syncs;
    while(*link) {
        struct DGClientSync *s=*link;
        if(s->name==name) { remote=s->remote;if(remove) { *link=s->next;DGUnregisterSyncName(s);free(s); }break; }
        link=&s->next;
    }
    pthread_mutex_unlock(&connection->lock);return remote;
}
static void DGFreeClientSyncs(struct DGConnection *c)
{ while(c->syncs) { struct DGClientSync *s=c->syncs;c->syncs=s->next;DGUnregisterSyncName(s);free(s); } }
GLsync glFenceSync(GLenum condition,GLbitfield flags)
{
    struct DGContext *c=dg_current;if(!DGSyncAvailable(c))return NULL;
    struct DGClientSync *s=calloc(1,sizeof(*s));uint32_t name=DGNewSyncName();
    if(!s || !name) { free(s);c->error=GL_OUT_OF_MEMORY;return NULL; }
    struct DGPacket p=DGPacketNew(c,DG_SYNC_FENCE);p.message.args[0]=condition;p.message.args[1]=flags;
    struct DGReply r=DGSubmit(c,&p,1);
    if(!p.valid_reply || !r.result || r.result>UINT32_MAX) { free(s);return NULL; }
    s->name=name;s->remote=(uint32_t)r.result;
    pthread_mutex_lock(&c->connection->lock);s->next=c->connection->syncs;c->connection->syncs=s;
    pthread_mutex_lock(&dg_sync_names_lock);s->global_next=dg_sync_names;dg_sync_names=s;
    pthread_mutex_unlock(&dg_sync_names_lock);pthread_mutex_unlock(&c->connection->lock);
    return (GLsync)(uintptr_t)name;
}
GLboolean glIsSync(GLsync sync)
{
    if(!dg_current || !sync)return GL_FALSE;
    // Native 13F1911 IsSync recognizes a live process-wide sync name even
    // from an unrelated context. This is identity, not permission to wait or
    // delete through a different stream/share group.
    uintptr_t name=(uintptr_t)sync;GLboolean found=GL_FALSE;
    pthread_mutex_lock(&dg_sync_names_lock);
    for(struct DGClientSync *s=dg_sync_names;s;s=s->global_next)if(s->name==name) { found=GL_TRUE;break; }
    pthread_mutex_unlock(&dg_sync_names_lock);return found;
}
void glDeleteSync(GLsync sync)
{
    struct DGContext *c=dg_current;if(!c || !sync)return;
    if(!DGSyncAvailable(c))return;
    uint32_t remote=DGSyncRemote(c,sync,1);if(!remote) { c->error=GL_INVALID_VALUE;return; }
    struct DGPacket p=DGPacketNew(c,DG_SYNC_DELETE);p.message.args[0]=remote;DGSubmit(c,&p,0);
}
GLenum glClientWaitSync(GLsync sync,GLbitfield flags,GLuint64 timeout)
{
    struct DGContext *c=dg_current;if(!DGSyncAvailable(c))return GL_WAIT_FAILED;
    uint32_t remote=DGSyncRemote(c,sync,0);if(!remote) { c->error=GL_INVALID_VALUE;return GL_WAIT_FAILED; }
    struct DGPacket p=DGPacketNew(c,DG_SYNC_CLIENT_WAIT);p.message.args[0]=remote;p.message.args[1]=flags;p.message.args[2]=timeout;
    struct DGReply r=DGSubmit(c,&p,1);return p.valid_reply?(GLenum)r.result:GL_WAIT_FAILED;
}
void glWaitSync(GLsync sync,GLbitfield flags,GLuint64 timeout)
{
    struct DGContext *c=dg_current;if(!DGSyncAvailable(c))return;
    uint32_t remote=DGSyncRemote(c,sync,0);if(!remote) { c->error=GL_INVALID_VALUE;return; }
    struct DGPacket p=DGPacketNew(c,DG_SYNC_WAIT);p.message.args[0]=remote;p.message.args[1]=flags;p.message.args[2]=timeout;
    DGSubmit(c,&p,0);
}
void glGetSynciv(GLsync sync,GLenum pname,GLsizei bufSize,GLsizei *length,GLint *values)
{
    struct DGContext *c=dg_current;if(!DGSyncAvailable(c))return;
    uint32_t remote=DGSyncRemote(c,sync,0);
    if(!remote || bufSize<0 || (bufSize && !values)) { c->error=GL_INVALID_VALUE;return; }
    struct DGSyncQuery out={0};struct DGPacket p=DGPacketNew(c,DG_SYNC_GET);
    p.message.flags=DG_OUTPUT_ONLY;p.message.bytes=sizeof(out);p.data=(void *)&out;p.capacity=sizeof(out);
    p.message.args[0]=remote;p.message.args[1]=pname;p.message.args[2]=bufSize?1:0;
    DGSubmit(c,&p,1);
    if(p.valid_reply && out.length>=0 && out.length<=1) { if(length)*length=out.length;if(bufSize && out.length)*values=out.value; }
}
