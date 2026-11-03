/* Actual snapshot/import routines on real native Intel GL. Distinct CGL
 * share groups are tested, not inferred from protocol/resource identities.
 * Native pixel reads are assertions AFTER copying, not a rendering path.
 */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
static void pixel(unsigned red,unsigned green,unsigned blue)
{
    unsigned char p[4]={0};glReadPixels(1,1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    if(p[0]!=red || p[1]!=green || p[2]!=blue || p[3]!=255)
        fprintf(stderr,"pixel actual=%u,%u,%u,%u expected=%u,%u,%u,255\n",p[0],p[1],p[2],p[3],red,green,blue);
    assert(p[0]==red && p[1]==green && p[2]==blue && p[3]==255 && !glGetError());
}
static void read_texture(GLuint read,GLuint texture,unsigned red,unsigned green,unsigned blue)
{
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);pixel(red,green,blue);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
}
static void copy_texture(struct DGHostContext *c,struct DGSurfaceSnapshot *v,int width,int height,int should_share)
{
    GLuint targets[2]={0},read=0,draw=0;glGenTextures(2,targets);
    glGenFramebuffersEXT(1,&read);glGenFramebuffersEXT(1,&draw);
    uint64_t reused=dg_snapshot_source_reuses,imported=dg_snapshot_source_imports;
    unsigned calls=0;
    /* Same immutable source, repeated independent destination definitions.
     * CPU verification is outside the repeated copy loop; no per-copy Finish.
     */
    for(unsigned t=0;t<2;++t){
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,targets[t]);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
        glEnable(GL_SCISSOR_TEST);glScissor(0,0,0,0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,23);glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);
        for(unsigned i=0;i<8;++i){
            assert(!DGSurfaceSnapshotCopyTexture(c,v,GL_RGBA8,width,height));++calls;
            GLint value=0;glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&value);assert((GLuint)value==targets[t]);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&value);assert((GLuint)value==read);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&value);assert((GLuint)value==draw);
            glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value);assert(value==23);
            glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&value);assert(value==3);
            assert(glIsEnabled(GL_SCISSOR_TEST) && !glGetError());
        }
        glDisable(GL_SCISSOR_TEST);read_texture(read,targets[t],255,0,0);
    }
    if(should_share){assert(dg_snapshot_source_reuses-reused==calls && dg_snapshot_source_imports==imported);}
    else{assert(dg_snapshot_source_imports-imported==calls && dg_snapshot_source_reuses==reused);}
    /* Writing one consumer must not mutate either the sealed version OR a
     * different consumer image. Destination-storage reuse is NOT assumed. */
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,targets[1],0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    read_texture(read,targets[1],0,255,0);read_texture(read,targets[0],255,0,0);
    glFramebufferTexture2DEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,targets[1]);
    assert(!DGSurfaceSnapshotCopyTexture(c,v,GL_RGBA8,width,height));
    read_texture(read,targets[1],255,0,0);read_texture(read,targets[0],255,0,0);
    if(should_share)assert(glIsTexture(v->native_texture));
    if(!dg_snapshot_consumer_reuse || CGLGetShareGroup(c->cgl)!=CGLGetShareGroup(v->producer))assert(!v->recyclable);
    DGForgetAliases(c,2,targets);glDeleteTextures(2,targets);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&read);glDeleteFramebuffersEXT(1,&draw);
    assert(!glGetError());
}
static void run(unsigned profile)
{
    enum{W=173,H=151};CGLPixelFormatObj format=NULL;GLint count=0;
    const char *renderer=getenv("VIM_PASSTHROUGH_OPENGL_RENDERER_ID");
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0,0,0};
    if(renderer){attrs[5]=kCGLPFARendererID;attrs[6]=(CGLPixelFormatAttribute)strtoul(renderer,NULL,0);}
    assert(!CGLChoosePixelFormat(attrs,&format,&count) && format);
    struct DGHostContext c[3]={{0}};
    for(unsigned i=0;i<3;++i){
        assert(!CGLCreateContext(format,i==1?c[0].cgl:NULL,&c[i].cgl));
        assert(!CGLSetCurrentContext(c[i].cgl));c[i].profile=profile;c[i].draw_mode=c[i].read_mode=GL_FRONT;
        c[i].surface_namespace[0]=0x736e617073686f74ULL;c[i].surface_namespace[1]=profile;
        assert(DGCreateAliases(&c[i],i==1?&c[0]:NULL));
        assert(strstr((const char*)glGetString(GL_RENDERER),"Intel"));
    }
    CGLDestroyPixelFormat(format);assert(!CGLSetCurrentContext(c[0].cgl));
    printf("renderer=%s profile=%x\n",glGetString(GL_RENDERER),profile);
    assert(DGSelectDrawable(&c[0],1,W,H));glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    uint32_t lease=DGSurfaceAcquire(&c[0],9601,1,W,H);assert(lease);
    struct DGSurfaceLease *p=DGSurfaceFind(&c[0],lease);assert(p);
    struct DGMessage publish={.op=DG_SURFACE_PUBLISH,.args={lease,0,0,1}};struct DGReply reply={0};
    assert(!DGSurfaceCommand(&c[0],&publish,&reply) && !reply.error);
    struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreate(&c[0],p,1);assert(v && v->completion);
    /* Change the ORIGINAL backing after sealing, before consumer use. */
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);publish.args[3]=2;
    assert(!DGSurfaceCommand(&c[0],&publish,&reply) && !reply.error);
    for(unsigned phase=0;phase<4;++phase){
        dg_snapshot_shared_source=phase&1;
        dg_snapshot_framebuffer_reuse=phase>=2;
        for(unsigned i=0;i<3;++i){
            assert(!CGLSetCurrentContext(c[i].cgl));
            copy_texture(&c[i],v,W,H,dg_snapshot_shared_source && i!=2);
            copy_texture(&c[i],v,W-7,H-9,dg_snapshot_shared_source && i!=2);
            if(dg_snapshot_framebuffer_reuse){
                assert(glIsFramebufferEXT(c[i].snapshot_read_framebuffer));
                assert(glIsFramebufferEXT(c[i].snapshot_draw_framebuffer));
                GLint attached=-1;
                glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c[i].snapshot_read_framebuffer);
                glGetFramebufferAttachmentParameterivEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&attached);assert(attached==GL_NONE);
                glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,c[i].snapshot_draw_framebuffer);
                glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
                    GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&attached);assert(attached==GL_NONE);
                glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
            }
        }
    }
    /* The held version retains its producer CGL context independently of
     * endpoint retirement. Shared and foreign consumers must still work. */
    assert(!CGLSetCurrentContext(c[0].cgl));
    DGDestroySurfaceLeases(&c[0]);DGDestroyDrawables(&c[0]);DGDestroyAliases(&c[0]);
    assert(!c[0].snapshot_read_framebuffer && !c[0].snapshot_draw_framebuffer);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c[0].cgl);c[0].cgl=NULL;
    for(unsigned i=1;i<3;++i){
        assert(!CGLSetCurrentContext(c[i].cgl));copy_texture(&c[i],v,W,H,i==1);
        DGDestroySurfaceLeases(&c[i]);DGDestroyDrawables(&c[i]);DGDestroyAliases(&c[i]);
        CGLSetCurrentContext(NULL);CGLDestroyContext(c[i].cgl);
    }
    DGSurfaceSnapshotDestroy(v);DGSnapshotCleanupDrain();
    assert(!dg_snapshot_bytes && !dg_snapshot_spare_bytes && !dg_surface_count && !dg_surface_leases);
    puts("PASS shared/foreign contexts, independent source/FBO switches, detached cached bindings, immutable producer epochs, independent writable consumers, cropped views, preserved state and producer-close lifetime");
}
/* Hold every independent destination until ALL source epochs have been
 * overwritten. No CPU read/Finish/ClientWait between copies and reuse. This
 * tests the actual source-read -> next-producer-write dependency, not just
 * that a fence name exists or that the final newest image is correct. */
static void recycle(unsigned profile,int shared,int enabled)
{
    enum{W=769,H=433,N=32};CGLPixelFormatObj format=NULL;GLint count=0;
    const char *renderer=getenv("VIM_PASSTHROUGH_OPENGL_RENDERER_ID");
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0,0,0};
    if(renderer){attrs[5]=kCGLPFARendererID;attrs[6]=(CGLPixelFormatAttribute)strtoul(renderer,NULL,0);}
    assert(!CGLChoosePixelFormat(attrs,&format,&count) && format);
    struct DGHostContext producer={0},consumer={0};producer.profile=consumer.profile=profile;
    producer.draw_mode=producer.read_mode=consumer.draw_mode=consumer.read_mode=GL_FRONT;
    producer.surface_namespace[0]=consumer.surface_namespace[0]=0x72656379636c65ULL;
    producer.surface_namespace[1]=consumer.surface_namespace[1]=profile;
    assert(!CGLCreateContext(format,NULL,&producer.cgl));
    if(shared)assert(!CGLCreateContext(format,producer.cgl,&consumer.cgl));
    CGLDestroyPixelFormat(format);assert(!CGLSetCurrentContext(producer.cgl));
    assert(strstr((const char*)glGetString(GL_RENDERER),"Intel"));
    assert(DGCreateAliases(&producer,NULL) && DGSelectDrawable(&producer,1,W,H));
    if(shared){assert(!CGLSetCurrentContext(consumer.cgl));assert(DGCreateAliases(&consumer,&producer));}
    struct DGHostContext *reader=shared?&consumer:&producer;
    GLuint cold_images[4]={0};int pressure=enabled && getenv("VIM_TEST_SNAPSHOT_POOL_PRESSURE");
    if(pressure){
        // Populate all spare slots with legitimate but currently idle sizes.
        // They must not permanently prevent caching the repeatedly used size.
        assert(!CGLSetCurrentContext(producer.cgl));
        for(unsigned i=0;i<4;++i){
            uint32_t cold=DGSurfaceAcquire(&producer,9700+i,1,W-17*(i+1),H-11*(i+1));assert(cold);
            struct DGSurfaceLease *surface=DGSurfaceFind(&producer,cold);assert(surface);
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,surface->framebuffer);
            glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);surface->resource->epoch=1;
            struct DGSurfaceSnapshot *idle=DGSurfaceSnapshotCreate(&producer,surface,1);assert(idle);
            assert(!CGLSetCurrentContext(reader->cgl));glGenTextures(1,&cold_images[i]);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,cold_images[i]);
            assert(!DGSurfaceSnapshotCopyTexture(reader,idle,GL_RGBA8,idle->width,idle->height));
            assert(!CGLSetCurrentContext(producer.cgl));
            DGSurfaceSnapshotDestroy(idle);
        }
    }
    uint64_t allocations=dg_snapshot_storage_allocations,reuses=dg_snapshot_storage_reuses;
    uint64_t fences=dg_snapshot_consumer_fences,waits=dg_snapshot_consumer_waits;
    assert(!CGLSetCurrentContext(producer.cgl));
    uint32_t lease=DGSurfaceAcquire(&producer,9602,1,W,H);assert(lease);
    struct DGSurfaceLease *p=DGSurfaceFind(&producer,lease);assert(p);
    GLuint textures[N]={0},read=0;assert(!CGLSetCurrentContext(reader->cgl));glGenTextures(N,textures);
    dg_snapshot_consumer_reuse=enabled;
    for(unsigned i=0;i<N;++i){
        assert(!CGLSetCurrentContext(producer.cgl));glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,producer.framebuffer);
        glClearColor(i&1,!(i&1),0,1);glClear(GL_COLOR_BUFFER_BIT);
        struct DGMessage publish={.op=DG_SURFACE_PUBLISH,.args={lease,0,0,i+1}};struct DGReply reply={0};
        assert(!DGSurfaceCommand(&producer,&publish,&reply) && !reply.error);
        struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreate(&producer,p,i+1);assert(v);
        assert(!CGLSetCurrentContext(reader->cgl));glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[i]);
        assert(!DGSurfaceSnapshotCopyTexture(reader,v,GL_RGBA8,W,H));assert(v->recyclable==enabled);
        assert(!CGLSetCurrentContext(producer.cgl));DGSurfaceSnapshotDestroy(v);
    }
    if(enabled){
        assert(dg_snapshot_storage_reuses-reuses==N-1 && dg_snapshot_storage_allocations-allocations==1);
        assert(dg_snapshot_consumer_fences-fences==(shared?N:0));
        assert(dg_snapshot_consumer_waits-waits==(shared?N-1:0));
    }else assert(dg_snapshot_storage_reuses==reuses && dg_snapshot_storage_allocations-allocations==N);
    assert(!CGLSetCurrentContext(reader->cgl));glGenFramebuffersEXT(1,&read);
    for(unsigned i=0;i<N;++i)read_texture(read,textures[i],(i&1)?255:0,(i&1)?0:255,0);
    if(pressure){
        for(unsigned i=0;i<4;++i)read_texture(read,cold_images[i],0,0,255);
        DGForgetAliases(reader,4,cold_images);glDeleteTextures(4,cold_images);
    }
    DGForgetAliases(reader,N,textures);glDeleteTextures(N,textures);glDeleteFramebuffersEXT(1,&read);
    if(shared){DGDestroySurfaceLeases(&consumer);DGDestroyDrawables(&consumer);DGDestroyAliases(&consumer);
        CGLSetCurrentContext(NULL);CGLDestroyContext(consumer.cgl);}
    assert(!CGLSetCurrentContext(producer.cgl));DGDestroySurfaceLeases(&producer);DGDestroyDrawables(&producer);DGDestroyAliases(&producer);
    CGLSetCurrentContext(NULL);CGLDestroyContext(producer.cgl);DGSnapshotCleanupDrain();
    assert(!dg_snapshot_bytes && !dg_snapshot_spare_bytes && !dg_surface_count && !dg_surface_leases);
    printf("PASS queued immutable source recycle profile=%x shared=%d enabled=%d: all32 held consumer epochs correct, no intermediate CPU wait/read; allocations=%llu reuses=%llu\n",
        profile,shared,enabled,(unsigned long long)(dg_snapshot_storage_allocations-allocations),
        (unsigned long long)(dg_snapshot_storage_reuses-reuses));
    dg_snapshot_consumer_reuse=1;
}
static void reader_bound(unsigned limit)
{
    enum{N=DG_SNAPSHOT_READERS+1,W=61,H=37};
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFARendererID,0,0};
    const char *renderer=getenv("VIM_PASSTHROUGH_OPENGL_RENDERER_ID");
    if(renderer)attrs[4]=(CGLPixelFormatAttribute)strtoul(renderer,NULL,0);else attrs[3]=0;
    CGLPixelFormatObj format=NULL;GLint count=0;assert(!CGLChoosePixelFormat(attrs,&format,&count) && format);
    struct DGHostContext producer={0},consumers[N]={{0}};GLuint textures[N]={0};
    producer.draw_mode=producer.read_mode=GL_FRONT;
    assert(!CGLCreateContext(format,NULL,&producer.cgl) && !CGLSetCurrentContext(producer.cgl));
    assert(strstr((const char*)glGetString(GL_RENDERER),"Intel"));
    assert(DGCreateAliases(&producer,NULL) && DGSelectDrawable(&producer,1,W,H));
    producer.surface_namespace[0]=0x626f756e646564ULL;producer.surface_namespace[1]=1;
    uint32_t lease=DGSurfaceAcquire(&producer,9603,1,W,H);assert(lease);
    struct DGSurfaceLease *p=DGSurfaceFind(&producer,lease);assert(p);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    struct DGMessage publish={.op=DG_SURFACE_PUBLISH,.args={lease,0,0,1}};struct DGReply reply={0};
    assert(!DGSurfaceCommand(&producer,&publish,&reply) && !reply.error);
    struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreate(&producer,p,1);assert(v);
    uint64_t refusals=dg_snapshot_consumer_refusals,fences=dg_snapshot_consumer_fences;
    for(unsigned i=0;i<limit;++i){
        assert(!CGLCreateContext(format,producer.cgl,&consumers[i].cgl));
        assert(!CGLSetCurrentContext(consumers[i].cgl) && DGCreateAliases(&consumers[i],&producer));
        glGenTextures(1,&textures[i]);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[i]);
        assert(!DGSurfaceSnapshotCopyTexture(&consumers[i],v,GL_RGBA8,W,H));
        assert(v->recyclable==(i<DG_SNAPSHOT_READERS));
    }
    assert(dg_snapshot_consumer_fences-fences==DG_SNAPSHOT_READERS &&
        dg_snapshot_consumer_refusals==refusals+(limit>DG_SNAPSHOT_READERS));
    CGLDestroyPixelFormat(format);assert(!CGLSetCurrentContext(producer.cgl));
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);publish.args[3]=2;
    assert(!DGSurfaceCommand(&producer,&publish,&reply) && !reply.error);
    uint64_t reuses=dg_snapshot_storage_reuses,waits=dg_snapshot_consumer_waits;
    DGSurfaceSnapshotDestroy(v);v=DGSurfaceSnapshotCreate(&producer,p,2);assert(v);
    assert(dg_snapshot_storage_reuses-reuses==(limit==DG_SNAPSHOT_READERS));
    assert(dg_snapshot_consumer_waits-waits==(limit==DG_SNAPSHOT_READERS?DG_SNAPSHOT_READERS:0));
    for(unsigned i=0;i<limit;++i){
        assert(!CGLSetCurrentContext(consumers[i].cgl));GLuint read=0;glGenFramebuffersEXT(1,&read);
        read_texture(read,textures[i],255,0,0);glDeleteFramebuffersEXT(1,&read);
        DGForgetAliases(&consumers[i],1,&textures[i]);glDeleteTextures(1,&textures[i]);
        DGDestroySurfaceLeases(&consumers[i]);DGDestroyDrawables(&consumers[i]);DGDestroyAliases(&consumers[i]);
        CGLSetCurrentContext(NULL);CGLDestroyContext(consumers[i].cgl);
    }
    assert(!CGLSetCurrentContext(producer.cgl));DGSurfaceSnapshotDestroy(v);
    DGDestroySurfaceLeases(&producer);DGDestroyDrawables(&producer);DGDestroyAliases(&producer);
    CGLSetCurrentContext(NULL);CGLDestroyContext(producer.cgl);DGSnapshotCleanupDrain();
    assert(!dg_snapshot_bytes && !dg_snapshot_spare_bytes && !dg_surface_count && !dg_surface_leases);
    printf("PASS actual reader fanout=%u: bounded reuse=%d, all old pixels preserved across producer overwrite, retained contexts retired\n",
        limit,limit==DG_SNAPSHOT_READERS);
}
int main(void)
{
    run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);
    unsigned profiles[]={kCGLOGLPVersion_Legacy,kCGLOGLPVersion_3_2_Core};
    for(unsigned i=0;i<2;++i)for(int shared=0;shared<2;++shared)for(int enabled=0;enabled<2;++enabled)
        recycle(profiles[i],shared,enabled);
    reader_bound(DG_SNAPSHOT_READERS);reader_bound(DG_SNAPSHOT_READERS+1);
    return 0;
}
