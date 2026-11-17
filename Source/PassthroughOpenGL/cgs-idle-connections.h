/* A context owns at most two idle, explicitly unbound 2D user clients.
 * Rebinding still performs every normal identity/geometry/ownership query.
 * Never pool a locked drawable, CPU mapping or a texture's owning connection.
 * Qualified on Leopard's native CGLS attach/detach path; other families retain
 * their existing behavior unless built as an explicit research experiment.
 */
#ifndef VIM_CGS_IDLE_CONNECTIONS_H
#define VIM_CGS_IDLE_CONNECTIONS_H
#if VIM_GLI_DISPATCH_COUNT == 757 || defined(VIM_GLI_CONNECTION_REUSE_EXPERIMENT)
#define VIM_REUSE_IDLE_DRAWABLE_CONNECTIONS 1
#else
#define VIM_REUSE_IDLE_DRAWABLE_CONNECTIONS 0
#endif

static io_connect_t VIMTakeIdleDrawableConnection(struct VIMContext *c)
{
#if VIM_REUSE_IDLE_DRAWABLE_CONNECTIONS
    if(c->idle_drawable_connection_count){
        unsigned index=--c->idle_drawable_connection_count;
        io_connect_t connection=c->idle_drawable_connections[index];
        c->idle_drawable_connections[index]=0;return connection;
    }
#else
    (void)c;
#endif
    return 0;
}
static void VIMRecycleDrawable(struct VIMContext *c,struct VIMCGSDrawable *d)
{
#if VIM_REUSE_IDLE_DRAWABLE_CONNECTIONS
    if(d && c->idle_drawable_connection_count<2 && d->updates && d->accelerator_surface &&
       !d->locked && !d->backing && !d->framebuffer && !d->vram){
        uint64_t args[]={0,0};uint32_t record[11]={0};size_t size=sizeof(record);
        IOReturn error=IOConnectCallMethod(d->updates,kIOVM2DSetSurface,args,2,
            NULL,0,NULL,NULL,record,&size);
        if(!error){
            c->idle_drawable_connections[c->idle_drawable_connection_count++]=d->updates;
            d->updates=0;
        }
    }
#else
    (void)c;
#endif
    VIMDestroyDrawable(d);
}
static void VIMCloseIdleDrawableConnections(struct VIMContext *c)
{
    while(c->idle_drawable_connection_count){
        unsigned index=--c->idle_drawable_connection_count;
        IOServiceClose(c->idle_drawable_connections[index]);c->idle_drawable_connections[index]=0;
    }
}
#endif
