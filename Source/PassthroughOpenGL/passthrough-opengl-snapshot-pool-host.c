/* Producer-local reuse of unreferenced immutable storage AND its imported
 * native GL texture. Rebinding a fresh texture to an IOSurface is expensive.
 * Names are in this producer/auxiliary CGL share group, never another one.
 * Active and spare allocations share the existing global snapshot budget.
 * Spares are additionally capped at 32 MiB per producer / 64 MiB globally.
 */
struct DGSnapshotSpare {
    IOSurfaceRef storage;GLuint texture;int width,height;size_t bytes;
    struct DGPrivateTextureWatch texture_watch;
    struct DGSnapshotSpare *next;
    struct DGSnapshotReader readers[DG_SNAPSHOT_READERS];
};
struct DGSnapshotPool {
    pthread_mutex_t lock;unsigned refs,count;int closed;size_t bytes;
    struct DGCompletionQueue *queue;struct DGSnapshotSpare *spares;
};
static void DGSnapshotStorageFree(struct DGCompletionQueue *q,CGLContextObj producer,IOSurfaceRef storage,GLuint texture,size_t bytes)
{
    if(texture && CGLGetCurrentContext()==producer)glDeleteTextures(1,&texture);
    else if(texture && q){
        struct DGCompletionCleanup cleanup;
        if(DGCompletionCleanupBegin(q,&cleanup)){
            glDeleteTextures(1,&texture);DGCompletionCleanupEnd(&cleanup);
        }
    }
    if(storage)CFRelease(storage);
    pthread_mutex_lock(&dg_snapshot_lock);dg_snapshot_bytes-=bytes;pthread_mutex_unlock(&dg_snapshot_lock);
}
static struct DGSnapshotPool *DGSnapshotPoolGet(struct DGHostContext *c)
{
    if(!c->snapshot_pool){
        struct DGSnapshotPool *p=calloc(1,sizeof(*p));if(!p)return NULL;
        if(pthread_mutex_init(&p->lock,NULL)){free(p);return NULL;}
        p->queue=DGCompletionQueueGet(c);
        if(!p->queue){pthread_mutex_destroy(&p->lock);free(p);return NULL;}
        p->refs=1;c->snapshot_pool=p;
    }
    __sync_add_and_fetch(&c->snapshot_pool->refs,1);return c->snapshot_pool;
}
static void DGSnapshotPoolPut(struct DGSnapshotPool *p)
{
    if(!p || __sync_sub_and_fetch(&p->refs,1))return;
    DGCompletionQueuePut(p->queue);pthread_mutex_destroy(&p->lock);free(p);
}
static int DGSnapshotPoolTake(struct DGSurfaceSnapshot *v)
{
    if(!dg_snapshot_storage_reuse)return 0;
    struct DGSnapshotPool *p=v->pool;struct DGSnapshotSpare *s=NULL;
    pthread_mutex_lock(&p->lock);
    struct DGSnapshotSpare **link=&p->spares;
    while(*link){
        if((*link)->width==v->width && (*link)->height==v->height){s=*link;*link=s->next;break;}
        link=&(*link)->next;
    }
    if(s){p->count--;p->bytes-=s->bytes;}
    pthread_mutex_unlock(&p->lock);
    if(!s)return 0;
    pthread_mutex_lock(&dg_snapshot_lock);dg_snapshot_spare_bytes-=s->bytes;pthread_mutex_unlock(&dg_snapshot_lock);
    if(!DGSnapshotReleaseReaders(s->readers,1)){
        DGSnapshotStorageFree(p->queue,v->producer,s->storage,s->texture,s->bytes);
        DGPrivateTextureWatchForget(&s->texture_watch);free(s);return 0;
    }
    v->storage=s->storage;v->native_texture=s->texture;v->bytes=s->bytes;
    DGPrivateTextureWatchName(&v->texture_watch,&v->native_texture);
    DGPrivateTextureWatchForget(&s->texture_watch);free(s);
    __sync_fetch_and_add(&dg_snapshot_storage_reuses,1);return 1;
}
static int DGSnapshotPoolReturn(struct DGSurfaceSnapshot *v)
{
    if(!dg_snapshot_storage_reuse || !v->pool || !v->storage || !v->native_texture ||
       !__atomic_load_n(&v->recyclable,__ATOMIC_ACQUIRE))return 0;
    struct DGSnapshotSpare *s=calloc(1,sizeof(*s));if(!s)return 0;
    s->storage=v->storage;s->texture=v->native_texture;s->bytes=v->bytes;s->width=v->width;s->height=v->height;
    DGPrivateTextureWatchSet(&s->texture_watch,v->texture_watch.texture,&s->texture);
    struct DGSnapshotPool *p=v->pool;struct DGSnapshotSpare *evicted=NULL;int accepted=0;
    pthread_mutex_lock(&p->lock);pthread_mutex_lock(&dg_snapshot_lock);
    // Spares are MRU first. An idle uncommon size must not occupy a slot
    // forever while the current repeatedly used size allocates every frame.
    // Evict only THIS producer's idle entries, under the existing byte limits.
    // Native deletion happens outside BOTH locks (it may enter its CGL queue).
    if(!p->closed && s->bytes<=32u*1024u*1024u && s->bytes<=64u*1024u*1024u){
        while(p->spares && (p->count>=4 || p->bytes>32u*1024u*1024u-s->bytes ||
              dg_snapshot_spare_bytes>64u*1024u*1024u-s->bytes)){
            struct DGSnapshotSpare **oldest=&p->spares;
            while((*oldest)->next)oldest=&(*oldest)->next;
            struct DGSnapshotSpare *old=*oldest;*oldest=NULL;
            --p->count;p->bytes-=old->bytes;dg_snapshot_spare_bytes-=old->bytes;
            old->next=evicted;evicted=old;
        }
    }
    if(!p->closed && p->count<4 && s->bytes<=32u*1024u*1024u-p->bytes &&
       s->bytes<=64u*1024u*1024u-dg_snapshot_spare_bytes){
        memcpy(s->readers,v->readers,sizeof(s->readers));memset(v->readers,0,sizeof(v->readers));
        s->next=p->spares;p->spares=s;p->count++;p->bytes+=s->bytes;dg_snapshot_spare_bytes+=s->bytes;accepted=1;
    }
    pthread_mutex_unlock(&dg_snapshot_lock);pthread_mutex_unlock(&p->lock);
    while(evicted){struct DGSnapshotSpare *old=evicted;evicted=old->next;
        DGSnapshotDiscardReaders(p->queue,v->producer,old->readers);
        DGSnapshotStorageFree(p->queue,v->producer,old->storage,old->texture,old->bytes);
        DGPrivateTextureWatchForget(&old->texture_watch);free(old);
    }
    if(accepted){
        DGPrivateTextureWatchForget(&v->texture_watch);
    }else {DGPrivateTextureWatchForget(&s->texture_watch);free(s);}return accepted;
}
static void DGSnapshotPoolClose(struct DGHostContext *c)
{
    struct DGSnapshotPool *p=c->snapshot_pool;if(!p)return;c->snapshot_pool=NULL;
    pthread_mutex_lock(&p->lock);p->closed=1;struct DGSnapshotSpare *s=p->spares;p->spares=NULL;
    pthread_mutex_lock(&dg_snapshot_lock);dg_snapshot_spare_bytes-=p->bytes;pthread_mutex_unlock(&dg_snapshot_lock);
    p->bytes=0;p->count=0;pthread_mutex_unlock(&p->lock);
    while(s){struct DGSnapshotSpare *next=s->next;
        DGSnapshotDiscardReaders(p->queue,c->cgl,s->readers);
        DGSnapshotStorageFree(p->queue,c->cgl,s->storage,s->texture,s->bytes);
        DGPrivateTextureWatchForget(&s->texture_watch);free(s);s=next;}
    DGSnapshotPoolPut(p);
}
