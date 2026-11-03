/* Actual typed GL client + kernel-owned loan, x64/i386 independently.
 * The source ID must name the explicit, unpresented WS RAM fixture. No Mesa,
 * native OpenGL symbol interposition or graphics framework replacement here. */
#include <IOKit/IOKitLib.h>
#include <stdio.h>
/* Fixture-only failure receipts; NEVER compiled into the installed driver. */
static IOReturn owner_struct_call(mach_port_t connection,uint32_t selector,const void *input,size_t input_size,void *output,size_t *output_size)
{
    IOReturn error=IOConnectCallStructMethod(connection,selector,input,input_size,output,output_size);
    if(error)fprintf(stderr,"OWNER struct selector=%u IOReturn=%08x input=%lu output=%lu\n",selector,error,
        (unsigned long)input_size,(unsigned long)(output_size?*output_size:0));
    return error;
}
#define IOConnectCallStructMethod owner_struct_call
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#undef IOConnectCallStructMethod
#include <assert.h>
static int future_mode;
static unsigned future_reads;
static void observe(io_connect_t observer,uint32_t color)
{
    struct VIMSurfaceMapping map={0};size_t size=sizeof(map);
    assert(!IOConnectCallStructMethod(observer,future_mode?kIOVM2DWaitSurfaceBacking:kIOVM2DLockSurfaceBacking,NULL,0,&map,&size));
    assert(map.layout.width==61 && map.layout.height==37 && map.layout.row_bytes==256);
    volatile uint32_t *pixels=(void*)(uintptr_t)(map.address+map.layout.offset);
    for(unsigned y=0;y<37;++y)for(unsigned x=0;x<61;++x)assert(pixels[y*64+x]==color);
    assert(DGBackingUnlockCPU(observer));
}
static void consume_pending(io_connect_t observer,struct DGContext *producer,struct DGContext *consumer,
    void *consumer_key,const struct VIMSurfaceState *expected,uint32_t color)
{
    struct VIMSurfaceMapping map={0};size_t size=sizeof(map);
    IOReturn error=IOConnectCallStructMethod(observer,kIOVM2DLockFutureGPUSurface,NULL,0,&map,&size);
    if(error==kIOReturnNotReady)return; // completion won acquisition; never fabricate a pending loan
    assert(!error && size==sizeof(map) && !map.address && !map.length);
    struct VIMSurfaceFutureRead read={0};size=sizeof(read);
    assert(!IOConnectCallStructMethod(observer,kIOVM2DGetFutureGPUSurface,NULL,0,&read,&size));
    assert(size==sizeof(read) && read.ticket && read.state.object==expected->object &&
        read.state.incarnation==expected->incarnation && read.state.content_epoch==expected->content_epoch &&
        read.state.flags==(VIM_SURFACE_STATE_GPU_PENDING|VIM_SURFACE_STATE_GPU_NEWER|
            VIM_SURFACE_STATE_GPU_FUTURE_SEALED|VIM_SURFACE_STATE_CONTEXT_LOCKED));
    struct VIMSurfaceState denied={0};size=sizeof(denied);uint64_t write=0;
    assert(IOConnectCallMethod(observer,kIOVM2DUnlockSurfaceBacking,&write,1,NULL,0,NULL,NULL,&denied,&size)==kIOReturnNotPermitted);
    struct VIMSurfaceRAM forbidden={0};size=sizeof(forbidden);uint64_t query[]={0,UINT32_MAX};
    assert(IOConnectCallMethod(observer,kIOVM2DGetRAMSurface,query,2,NULL,0,NULL,NULL,&forbidden,&size)==kIOReturnNotOpen);
    struct VIMSurfaceMapping blocked={0};size=sizeof(blocked);
    assert(IOConnectCallStructMethod(producer->connection->device,kIOVM2DLockSurfaceBacking,NULL,0,&blocked,&size)==kIOReturnCannotLock);
    assert(DGMakeCurrent(consumer,consumer_key,GL_UNSIGNED_BYTE,61,37));
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    assert(DGCopyBackingFutureTexture(read.ticket,&read.state,GL_RGBA8,61,37));
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    uint32_t image[61*37];glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glReadPixels(0,0,61,37,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,image);
    for(unsigned i=0;i<61*37;++i)assert(image[i]==color);
    assert(!glGetError());glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    // Reservation still exists after real consumer GPU work. It belongs to
    // THIS read endpoint; neither write rejection nor producer completion
    // may release it behind us.
    size=sizeof(denied);uint64_t read_only=1;
    assert(!IOConnectCallMethod(observer,kIOVM2DUnlockSurfaceBacking,&read_only,1,NULL,0,NULL,NULL,&denied,&size));
    assert(denied.content_epoch==read.state.content_epoch && !(denied.flags&VIM_SURFACE_STATE_CONTEXT_LOCKED));
    ++future_reads;
}
static void qualify_writers(uint32_t surface,struct DGContext *c,void* key,io_connect_t observer)
{
    dg_deferred_backing_scopes=dg_future_backing_scopes=dg_writer_backing_scopes=1;
    assert(DGSupportsBackingWriterScopes());future_mode=1;
    struct DGContext *consumer=DGCreateContext(NULL,NULL);unsigned char consumer_key=0;
    struct VIMFramebufferLayout layout={1,61,37,256,0,32};
    struct VIMSurfaceState previous={0};uint64_t admitted=dg_writer_backing_stats[1];
    for(unsigned round=0;round<32;++round){
        assert(DGMakeCurrent(c,key,GL_UNSIGNED_BYTE,61,37));uint64_t before=dg_writer_backing_stats[1];
        int admitted=DGBeginBackingRender(surface,&layout);
        if(admitted!=1 || !c->backing_writer)fprintf(stderr,"OWNER Begin round=%u result=%d writer=%d deferred=%d future=%d dead=%d error=%x stats=%llu,%llu,%llu,%llu\n",
            round,admitted,c->backing_writer,c->backing_deferred,c->backing_future,c->connection->dead,c->error,
            dg_writer_backing_stats[0],dg_writer_backing_stats[1],dg_writer_backing_stats[2],dg_writer_backing_stats[3]);
        assert(admitted==1 && c->backing_writer);
        uint64_t epoch=c->backing_loan.backing.state.content_epoch;
        struct VIMSurfaceMapping recursive={0};size_t recursive_size=sizeof(recursive);
        assert(IOConnectCallStructMethod(observer,kIOVM2DWaitSurfaceBacking,NULL,0,&recursive,&recursive_size)==kIOReturnCannotLock);
        if(dg_writer_backing_stats[1]!=before && !future_reads){
            // Hold a genuine OLD immutable reader while the NEXT GPU writer
            // owns the gate. It must neither unlock CPU access nor observe
            // that writer's later pixels through its old sealed version.
            consume_pending(observer,c,consumer,&consumer_key,&previous,
                (round-1)&1?0xff0000ffu:0xffff0000u);
            assert(DGMakeCurrent(c,key,GL_UNSIGNED_BYTE,61,37));
        }
        glClearColor(round&1?0:1,0,round&1?1:0,1);glClear(GL_COLOR_BUFFER_BIT);
        assert(DGEndBackingRender(c,&previous)==1 && previous.content_epoch==epoch+1 &&
            previous.flags==(VIM_SURFACE_STATE_GPU_PENDING|VIM_SURFACE_STATE_GPU_NEWER|VIM_SURFACE_STATE_GPU_FUTURE_SEALED));
    }
    observe(observer,0xff0000ffu); // FIRST CPU observation, after all successive GPU writers
    struct VIMSurfaceMapping map={0};size_t size=sizeof(map);
    assert(!IOConnectCallStructMethod(observer,kIOVM2DLockSurfaceBacking,NULL,0,&map,&size));
    struct VIMSurfaceState state={0};size=sizeof(state);
    assert(!IOConnectCallStructMethod(observer,kIOVM2DGetSurfaceState,NULL,0,&state,&size));
    assert(state.content_epoch==previous.content_epoch && DGBackingUnlockCPU(observer));
    assert(dg_writer_backing_stats[1]>admitted && future_reads);
    printf("PASS actual writer continuations=%llu, old GPU reads=%u, exact final CPU epoch=%llu\n",
        (unsigned long long)(dg_writer_backing_stats[1]-admitted),future_reads,(unsigned long long)state.content_epoch);
    assert(DGMakeCurrent(c,key,GL_UNSIGNED_BYTE,61,37) && DGBeginBackingRender(surface,&layout)==1);
    glClearColor(1,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    DGDestroyContext(c);observe(observer,0xffffff00u);DGDestroyContext(consumer);
    assert(!IOServiceClose(observer));
    puts("PASS real retained writer gate, pending GPU-only handoff, reader overlap, newest CPU contents/epoch and producer destruction");
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;char *end=0;unsigned long parsed=strtoul(argv[1],&end,0);
    if(!end || *end || parsed<=2 || parsed>UINT32_MAX)return 2;
    uint32_t surface=(uint32_t)parsed;unsigned char key=0;
    struct DGContext *c=DGCreateContext(NULL,NULL);assert(c && DGMakeCurrent(c,&key,GL_UNSIGNED_BYTE,61,37));
    assert(strstr((const char*)glGetString(GL_RENDERER),"Intel") && DGSupportsBackingRenderScopes());
    struct DGContext *consumer=NULL;unsigned char consumer_key=0;
    future_mode=getenv("VIM_OWNER_FUTURE")!=NULL;
    if(future_mode){
        assert(DGSupportsBackingFutureTextures());dg_deferred_backing_scopes=dg_future_backing_scopes=1;
        consumer=DGCreateContext(NULL,NULL);assert(consumer && DGMakeCurrent(c,&key,GL_UNSIGNED_BYTE,61,37));
    }
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    io_connect_t observer=0;assert(service && !IOServiceOpen(service,mach_task_self(),2,&observer));IOObjectRelease(service);
    uint64_t target[]={surface,0x800};uint32_t record[11]={0};size_t size=sizeof(record);
    assert(!IOConnectCallMethod(observer,kIOVM2DSetSurface,target,2,NULL,0,NULL,NULL,record,&size));
    struct VIMFramebufferLayout layout={1,61,37,256,0,32};
    struct VIMFramebufferLayout bad=layout;bad.width++;
    assert(DGBeginBackingRender(surface,&bad)==-1 && !c->backing_loan.serial);
    if(getenv("VIM_OWNER_WRITER")){qualify_writers(surface,c,&key,observer);return 0;}
    for(unsigned round=0;round<(future_mode?32u:4u);++round){
        assert(DGMakeCurrent(c,&key,GL_UNSIGNED_BYTE,61,37));
        assert(DGBeginBackingRender(surface,&layout)==1 && c->backing_loan.serial);
        uint64_t serial=c->backing_loan.serial,epoch=c->backing_loan.backing.state.content_epoch;
        assert(DGBeginBackingRender(surface,&layout)==-1 && c->backing_loan.serial==serial);
        struct VIMSurfaceMapping blocked={0};size=sizeof(blocked);
        assert(IOConnectCallStructMethod(observer,kIOVM2DLockSurfaceBacking,NULL,0,&blocked,&size)==kIOReturnCannotLock);
        glClearColor(round&1?0:1,0,round&1?1:0,1);glClear(GL_COLOR_BUFFER_BIT);
        if(round==1){
            struct DGPacket p=DGPacketNew(c,DG_BACKING_RENDER_MATERIALIZE);p.message.args[0]=serial;
            struct DGReply r=DGSubmit(c,&p,1);assert(p.valid_reply && !r.error && r.result==1);
            glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
        }
        struct VIMSurfaceState committed={0};
        assert(DGEndBackingRender(c,&committed)==1 && committed.content_epoch==epoch+1 && !c->backing_loan.serial);
        if(future_mode){
            assert(committed.flags==(VIM_SURFACE_STATE_GPU_PENDING|VIM_SURFACE_STATE_GPU_FUTURE_SEALED));
            consume_pending(observer,c,consumer,&consumer_key,&committed,
                round==1?0xff00ff00u:round&1?0xff0000ffu:0xffff0000u);
        }
        observe(observer,round==1?0xff00ff00u:round&1?0xff0000ffu:0xffff0000u);
        assert(DGEndBackingRender(c,NULL)==-1 && !glGetError());
    }
    if(future_mode){assert(future_reads);printf("PASS actual pending GPU reads=%u: exact native pixels, denied CPU/write APIs, independent read-loan retirement\n",future_reads);}
    assert(DGMakeCurrent(c,&key,GL_UNSIGNED_BYTE,61,37));
    assert(DGBeginBackingRender(surface,&layout)==1);
    glClearColor(1,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    struct DGPacket acknowledged=DGPacketNew(c,DG_ERROR);DGSubmit(c,&acknowledged,1);
    assert(acknowledged.valid_reply);acknowledged.failed=1;DGSubmit(c,&acknowledged,1);
    assert(!acknowledged.valid_reply && glGetError()==GL_INVALID_OPERATION); // no stale completion ACK
    DGDestroyContext(c);observe(observer,0xffffff00u); // public typed owner retirement
    if(consumer)DGDestroyContext(consumer);
    assert(!IOServiceClose(observer));puts("PASS typed backing owner: same endpoint pin, native GPU, metadata reuse, exclusion, materialization, committed epochs and destruction");
    return 0;
}
