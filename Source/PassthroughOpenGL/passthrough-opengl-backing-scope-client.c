/* One typed owner connects kernel exclusion to actual native GPU use.
 * No device/CPU pointer escapes to GLI, no cross-client lock donation, and
 * immutable prepared page vectors are fetched only on native cache miss. */
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
int32_t DGClientProfileWaitSurfaceBacking(uint32_t connection,void *mapping,size_t *size)
{
    struct DGClientProfileStamp stamp=DGClientProfileBegin(1,DG_PROFILE_CLIENT_BACKING_WAIT,*size);
    IOReturn error=IOConnectCallStructMethod(connection,kIOVM2DWaitSurfaceBacking,NULL,0,mapping,size);
    DGClientProfileEnd(stamp);return error;
}
#endif
int DGSupportsBackingRenderScopes(void)
{
    struct DGContext *c=dg_current;
    return c && c->connection->device && !c->connection->dead && c->connection->drained_close_supported &&
        (c->connection->capabilities&DG_CAP_BACKING_RENDER_SCOPE);
}
// Qualified defaults, with explicit same-boot diagnostic comparisons retained.
// Every admission still requires the paired kernel/host capabilities below;
// unsupported peers keep the synchronous fallback, not fabricated completion.
static volatile int dg_deferred_backing_scopes=1;
static volatile int dg_future_backing_scopes=1;
static volatile int dg_writer_backing_scopes=1;
static volatile uint64_t dg_writer_backing_stats[4]; // claims, admitted continuations, no-write refusals, initial gates
int DGSupportsBackingWriterScopesInContext(void *opaque)
{
    struct DGContext *c=opaque;
    return dg_writer_backing_scopes && dg_deferred_backing_scopes && dg_future_backing_scopes &&
        c && c->connection->device && !c->connection->dead && c->connection->drained_close_supported &&
        (c->connection->capabilities&DG_CAP_BACKING_RENDER_SCOPE) &&
        (c->connection->capabilities&DG_CAP_BACKING_WRITER_CONTINUATION);
}
int DGSupportsBackingWriterScopes(void){return DGSupportsBackingWriterScopesInContext(dg_current);}
static volatile uint64_t dg_deferred_backing_stats[4]; // attempts, begins, pending commits, synchronous fallbacks
static pthread_once_t dg_backing_namespace_once=PTHREAD_ONCE_INIT;
static uint64_t dg_backing_namespace[2];
static void DGBackingNamespace(void)
{
    uuid_t value;
    if(VIMGetResourceNamespace(value))memcpy(dg_backing_namespace,value,16);
}
static int DGBackingKernelEnd(struct DGContext *c,unsigned mode,struct VIMSurfaceState *out)
{
    struct DGConnection *connection=c->connection;struct VIMSurfaceState state={0};size_t size=sizeof(state);
    uint64_t args[]={c->backing_loan.serial,mode};
    IOReturn error=IOConnectCallMethod(connection->device,c->backing_writer?kIOVM2DEndWriterRAMLoan:kIOVM2DEndRAMBackingLoan,
        args,2,NULL,0,NULL,NULL,&state,&size);
    if(error || size!=sizeof(state) || state.version!=VIM_SURFACE_STATE_VERSION ||
       state.object!=c->backing_loan.backing.state.object || state.incarnation!=c->backing_loan.backing.state.incarnation ||
       (state.flags&VIM_SURFACE_STATE_CONTEXT_LOCKED))return 0;
    c->backing_loan=(struct VIMSurfaceBackingLoan){0};c->backing_deferred=c->backing_future=c->backing_writer=0;connection->backing_owner=NULL;
    if(out)*out=state;return 1;
}
static int DGBackingKernelPending(struct DGContext *c,uint64_t ticket,struct VIMSurfaceState *out)
{
    pthread_once(&dg_backing_namespace_once,DGBackingNamespace);
    struct VIMSurfaceBackingPending request={.serial=c->backing_loan.serial,.ticket=ticket};
    memcpy(request.namespace_id,dg_backing_namespace,16);
    struct VIMSurfaceState state={0};size_t size=sizeof(state);
    uint32_t method=c->backing_writer?kIOVM2DCommitWriterRAMLoan:
        c->backing_future?kIOVM2DCommitFutureRAMLoan:kIOVM2DCommitDeferredRAMLoan;
    uint32_t flags=VIM_SURFACE_STATE_GPU_PENDING|(c->backing_future?VIM_SURFACE_STATE_GPU_FUTURE_SEALED:0);
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
    struct DGClientProfileStamp commit_profile=DGClientProfileBegin(1,DG_PROFILE_CLIENT_BACKING_COMMIT,sizeof(request));
#endif
    IOReturn error=IOConnectCallStructMethod(c->connection->device,method,
        &request,sizeof(request),&state,&size);
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
    DGClientProfileEnd(commit_profile);
#endif
    if(error || size!=sizeof(state) || state.version!=VIM_SURFACE_STATE_VERSION ||
       state.flags!=(flags|(c->backing_writer?VIM_SURFACE_STATE_GPU_NEWER:0)) || state.object!=c->backing_loan.backing.state.object ||
       state.incarnation!=c->backing_loan.backing.state.incarnation ||
       state.content_epoch!=c->backing_loan.backing.state.content_epoch+1)return 0;
    c->backing_loan=(struct VIMSurfaceBackingLoan){0};c->backing_deferred=c->backing_future=c->backing_writer=0;c->connection->backing_owner=NULL;
    __sync_fetch_and_add(&dg_deferred_backing_stats[2],1);
    if(out)*out=state;return 1;
}
static void DGBackingFailed(struct DGContext *c)
{
    // A lost reply may follow real native work. Abort drains the SAME stream,
    // not a best-effort Flush. If retirement fails the driver quarantines its
    // pin/exclusion. Never invent a normal End or retry drawing on that token.
    if(c->backing_loan.serial)DGBackingKernelEnd(c,VIM_RAM_LOAN_ABORT,NULL);
    else if(c->connection->device){
        // A lost continuation reply may have claimed a gate without exposing
        // its serial. Actual clientClose drains that same native stream and
        // cancels any retained writer claim; do not replay unknown admission.
        IOServiceClose(c->connection->device);c->connection->device=0;
    }
    c->connection->dead=1;c->error=GL_INVALID_OPERATION;
}
int DGEndBackingRender(void *opaque,struct VIMSurfaceState *committed)
{
    struct DGContext *c=opaque;if(!c)return -1;
    struct DGConnection *connection=c->connection;pthread_mutex_lock(&connection->backing_lock);
    int result=-1;
    if(c->backing_loan.serial){
        struct DGPacket p=DGPacketNew(c,c->backing_deferred?DG_BACKING_RENDER_DEFER:DG_BACKING_RENDER_END);
        p.message.args[0]=c->backing_loan.serial;
        if(c->backing_deferred && c->backing_future)p.message.args[1]=1;
        struct DGReply reply=DGSubmit(c,&p,1);
        if(c->backing_deferred && p.valid_reply && !reply.error && reply.result){
            uint64_t ticket=reply.result;
            if(DGBackingKernelPending(c,ticket,committed))result=1;
            else if(c->backing_writer){DGBackingFailed(c);result=0;}
            else {
                // A refused subscription has no callback. Drain this REAL
                // ticket before normal written End restores its CPU gate.
                p=DGPacketNew(c,DG_BACKING_COMPLETION_WAIT);p.message.context=0;p.message.args[0]=ticket;
                reply=DGSubmit(c,&p,1);
                if(p.valid_reply && !reply.error && reply.result==1 && DGBackingKernelEnd(c,VIM_RAM_LOAN_WRITTEN,committed)){
                    p=DGPacketNew(c,DG_BACKING_COMPLETION_RELEASE);p.message.context=0;p.message.args[0]=ticket;
                    reply=DGSubmit(c,&p,1);result=p.valid_reply && !reply.error && reply.result==1;
                }
                if(result!=1){DGBackingFailed(c);result=0;}
            }
        }
        else if(p.valid_reply && !reply.error && reply.result==1 && DGBackingKernelEnd(c,VIM_RAM_LOAN_WRITTEN,committed))result=1;
        else {DGBackingFailed(c);result=0;}
    }
    pthread_mutex_unlock(&connection->backing_lock);return result;
}
static int DGBackingUnlockCPU(io_connect_t endpoint)
{
    struct VIMSurfaceState state={0};size_t size=sizeof(state);uint64_t read_only=1;
    return !IOConnectCallMethod(endpoint,kIOVM2DUnlockSurfaceBacking,&read_only,1,NULL,0,NULL,NULL,&state,&size);
}
static int DGTryWriterContinuation(struct DGContext *c,const struct VIMFramebufferLayout *expected)
{
    struct VIMSurfaceWriterLoan reply={0};size_t size=sizeof(reply);
    IOReturn error=IOConnectCallStructMethod(c->connection->device,kIOVM2DContinueWriterRAMLoan,
        expected,sizeof(*expected),&reply,&size);
    if(error==kIOReturnUnsupported || error==kIOReturnBadArgument || error==kIOReturnNotReady || error==kIOReturnBusy)return -1;
    if(error){DGBackingFailed(c);return 0;}
    const struct VIMSurfaceVRAM *b=&reply.loan.backing;
    if(size!=sizeof(reply) || !reply.loan.serial || !reply.previous_ticket ||
       b->version!=VIM_SURFACE_RAM_VERSION || b->offset || !b->length || b->length>DG_MAX_BYTES ||
       b->width!=expected->width || b->height!=expected->height || b->row_bytes!=expected->row_bytes ||
       (uint64_t)b->row_bytes*b->height>b->length || b->state.version!=VIM_SURFACE_STATE_VERSION ||
       !b->state.object || !b->state.incarnation || !b->state.content_epoch ||
       b->state.flags!=(VIM_SURFACE_STATE_GPU_PENDING|VIM_SURFACE_STATE_GPU_NEWER|VIM_SURFACE_STATE_GPU_FUTURE_SEALED)){
        DGBackingFailed(c);return 0;
    }
    c->backing_loan=reply.loan;c->backing_deferred=c->backing_future=c->backing_writer=1;
    c->connection->backing_owner=c;__sync_fetch_and_add(&dg_writer_backing_stats[0],1);
    struct DGPacket p=DGPacketNew(c,DG_BACKING_RENDER_CONTINUE);p.message.flags=DG_INPUT_ONLY;
    uint64_t *a=p.message.args;
    a[0]=b->offset;a[1]=b->length;a[2]=a[7]=b->width;a[3]=a[8]=b->height;a[4]=b->row_bytes;
    a[5]=reply.previous_ticket;a[10]=b->state.object;a[11]=b->state.incarnation;
    a[12]=b->state.content_epoch;a[13]=1;a[15]=reply.loan.serial;
    struct DGReply native=DGSubmit(c,&p,1);
    if(p.valid_reply && !native.error && native.result==1){
        __sync_fetch_and_add(&dg_writer_backing_stats[1],1);return 1;
    }
    if(p.valid_reply && !native.error && !native.result && DGBackingKernelEnd(c,VIM_RAM_LOAN_READ_ONLY,NULL)){
        __sync_fetch_and_add(&dg_writer_backing_stats[2],1);return -1;
    }
    DGBackingFailed(c);return 0;
}
int DGBeginBackingRender(uint32_t surface,const struct VIMFramebufferLayout *expected)
{
    struct DGContext *c=dg_current;
    if(!DGSupportsBackingRenderScopes() || !surface || surface==1 || !expected ||
       expected->version!=VIM_FRAMEBUFFER_LAYOUT_VERSION || expected->bits_per_pixel!=32 ||
       expected->width!=(uint32_t)c->width || expected->height!=(uint32_t)c->height || expected->offset)return -1;
    struct DGConnection *connection=c->connection;pthread_mutex_lock(&connection->backing_lock);
    int result=-1,locked=0;
    if(connection->backing_owner)goto done;
    uint64_t target[]={surface,0x800};uint32_t record[11]={0};size_t size=sizeof(record);
    IOReturn error=IOConnectCallMethod(connection->device,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&size);
    if(error)goto done;
    if(DGSupportsBackingWriterScopes()){
        int continued=DGTryWriterContinuation(c,expected);
        if(continued>=0){result=continued;goto done;}
    }
    struct VIMSurfaceMapping mapping={0};size=sizeof(mapping);
    error=IOConnectCallStructMethod(connection->device,kIOVM2DLockSurfaceBacking,NULL,0,&mapping,&size);
    if(error==kIOReturnCannotLock){size=sizeof(mapping);
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
        error=DGClientProfileWaitSurfaceBacking(connection->device,&mapping,&size);
#else
        error=IOConnectCallStructMethod(connection->device,kIOVM2DWaitSurfaceBacking,NULL,0,&mapping,&size);
#endif
    }
    if(error)goto done;locked=1;
    if(size!=sizeof(mapping) || memcmp(&mapping.layout,expected,sizeof(*expected)))goto done;
    struct VIMSurfaceRAM ram={0};uint64_t query[]={0,UINT32_MAX};size=sizeof(ram);
    error=IOConnectCallMethod(connection->device,kIOVM2DGetRAMSurface,query,2,NULL,0,NULL,NULL,&ram,&size);
    if(error || size!=sizeof(ram) || ram.backing.version!=VIM_SURFACE_RAM_VERSION || ram.reserved ||
       ram.first!=UINT32_MAX || ram.count || !ram.total || ram.total>16384 ||
       ram.backing.length!=(uint64_t)ram.total*4096 || ram.backing.width!=expected->width ||
       ram.backing.height!=expected->height || ram.backing.row_bytes!=expected->row_bytes ||
       ram.backing.offset)goto done;
    size=sizeof(c->backing_loan);c->backing_deferred=c->backing_future=c->backing_writer=0;
    int deferred=dg_deferred_backing_scopes && (connection->capabilities&DG_CAP_BACKING_COMPLETION);
    int writer=deferred && DGSupportsBackingWriterScopes();
    if(deferred)__sync_fetch_and_add(&dg_deferred_backing_stats[0],1);
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
    struct DGClientProfileStamp begin_profile=DGClientProfileBegin(1,DG_PROFILE_CLIENT_BACKING_BEGIN,sizeof(ram.backing));
#endif
    error=IOConnectCallStructMethod(connection->device,writer?kIOVM2DBeginWriterRAMLoan:
        deferred?kIOVM2DBeginDeferredRAMLoan:kIOVM2DBeginRAMBackingLoan,
        &ram.backing,sizeof(ram.backing),&c->backing_loan,&size);
#ifdef VIM_PASSTHROUGH_OPENGL_CLIENT_PROFILE
    DGClientProfileEnd(begin_profile);
#endif
    if(deferred && (error==kIOReturnUnsupported || error==kIOReturnBadArgument || error==kIOReturnBusy)){
        __sync_fetch_and_add(&dg_deferred_backing_stats[3],1);
        c->backing_loan=(struct VIMSurfaceBackingLoan){0};size=sizeof(c->backing_loan);deferred=writer=0;
        error=IOConnectCallStructMethod(connection->device,kIOVM2DBeginRAMBackingLoan,
            &ram.backing,sizeof(ram.backing),&c->backing_loan,&size);
    }
    if(error)goto done;
    c->backing_writer=writer;
    c->backing_deferred=deferred;
    c->backing_future=deferred && dg_future_backing_scopes &&
        (connection->capabilities&DG_CAP_BACKING_FUTURE_VERSION)!=0;
    if(deferred)__sync_fetch_and_add(&dg_deferred_backing_stats[1],1);
    if(writer)__sync_fetch_and_add(&dg_writer_backing_stats[3],1);
    if(size!=sizeof(c->backing_loan) || !c->backing_loan.serial ||
       memcmp(&c->backing_loan.backing,&ram.backing,sizeof(ram.backing))){
        // The new kernel is a trusted peer, but an invalid response is still
        // not permission to release an unknown live loan as an ordinary lock.
        connection->backing_owner=c;locked=0;DGBackingFailed(c);result=0;goto done;
    }
    locked=0;connection->backing_owner=c;
    struct DGPacket p=DGPacketNew(c,DG_BACKING_RENDER_BEGIN);p.message.flags=DG_INPUT_ONLY;
    uint64_t *a=p.message.args;const struct VIMSurfaceVRAM *b=&ram.backing;
    a[0]=b->offset;a[1]=b->length;a[2]=a[7]=b->width;a[3]=a[8]=b->height;a[4]=b->row_bytes;
    a[10]=b->state.object;a[11]=b->state.incarnation;a[12]=b->state.content_epoch;a[13]=1;a[15]=c->backing_loan.serial;
    struct DGReply reply=DGSubmit(c,&p,1);
    if(p.valid_reply && !reply.error && reply.result==2){
        uint64_t *pages=malloc((size_t)ram.total*sizeof(*pages));int valid=pages!=NULL;
        for(unsigned first=0;valid && first<ram.total;){
            struct VIMSurfaceRAM part={0};uint64_t chunk[]={c->backing_loan.serial,first};size=sizeof(part);
            error=IOConnectCallMethod(connection->device,kIOVM2DGetRAMBackingLoan,chunk,2,NULL,0,NULL,NULL,&part,&size);
            valid=!error && size==sizeof(part) && !part.reserved && part.first==first && part.total==ram.total &&
                part.count && part.count<=VIM_SURFACE_RAM_PAGE_CHUNK && part.count<=ram.total-first &&
                !memcmp(&part.backing,b,sizeof(*b));
            if(valid){memcpy(pages+first,part.pages,part.count*sizeof(*pages));first+=part.count;}
        }
        if(valid){
            p.message.bytes=ram.total*sizeof(*pages);a[14]=ram.total;
            p.data=(void*)pages;p.capacity=p.message.bytes;p.borrowed=1;reply=DGSubmit(c,&p,1);
        }else p.valid_reply=0;
        free(pages);
    }
    if(p.valid_reply && !reply.error && reply.result==1)result=1;
    else if(p.valid_reply && !reply.error && !reply.result){
        // Unsupported native layout is acknowledged only after any partial
        // registration use has drained. No GPU draw occurred: read-only End.
        if(!DGBackingKernelEnd(c,VIM_RAM_LOAN_READ_ONLY,NULL)){DGBackingFailed(c);result=0;}
    }else {DGBackingFailed(c);result=0;}
done:
    if(locked && !DGBackingUnlockCPU(connection->device)){connection->dead=1;c->error=GL_INVALID_OPERATION;result=0;}
    pthread_mutex_unlock(&connection->backing_lock);return result;
}
