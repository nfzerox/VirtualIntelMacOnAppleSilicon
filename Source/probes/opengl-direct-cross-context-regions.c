/* Read a retained family through another context's already-created FBO.
 * Sampling/rebinding an imported texture is covered separately. This checks
 * the actual region-import, immutable-version and framebuffer-copy consumers,
 * after producer flush, without an intervening CPU read or glFinish.
 * All resources belong to this process; no guest/WindowServer state changes.
 */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>

static void check_pixel(unsigned x,unsigned y,unsigned r,unsigned g,unsigned b)
{
    unsigned char pixel[4]={0};
    glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if(pixel[0]!=r || pixel[1]!=g || pixel[2]!=b || pixel[3]!=255){
        fprintf(stderr,"pixel %u,%u = %u,%u,%u,%u expected %u,%u,%u,255\n",
            x,y,pixel[0],pixel[1],pixel[2],pixel[3],r,g,b);
        abort();
    }
    assert(!glGetError());
}
static void run(unsigned profile)
{
    const int rebind_source=getenv("VIM_TEST_REBIND_SOURCE")!=NULL;
    const int set_viewport=getenv("VIM_TEST_NONZERO_VIEWPORT")!=NULL;
    const int source_read=getenv("VIM_TEST_SOURCE_READ")!=NULL;
    struct DGHostContext producer={0},consumer={0};
    producer.profile=consumer.profile=profile;
    producer.surface_namespace[0]=consumer.surface_namespace[0]=getpid();
    producer.surface_namespace[1]=consumer.surface_namespace[1]=profile;
    producer.draw_mode=producer.read_mode=consumer.draw_mode=consumer.read_mode=GL_FRONT;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf);
    assert(!CGLCreateContext(pf,NULL,&producer.cgl));
    assert(!CGLCreateContext(pf,NULL,&consumer.cgl)); // Independent name spaces.
    CGLDestroyPixelFormat(pf);
    assert(!CGLSetCurrentContext(producer.cgl));
    printf("profile=%x renderer=%s\n",profile,glGetString(GL_RENDERER));fflush(stdout);
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    assert(DGCreateAliases(&producer,NULL) && DGSelectDrawable(&producer,1,64,64));
    unsigned source=DGSurfaceAcquire(&producer,0x781,1,64,64);assert(source);
    struct DGSurfaceLease *published=DGSurfaceFind(&producer,source);assert(published);
    assert(!CGLSetCurrentContext(consumer.cgl));
    assert(DGCreateAliases(&consumer,NULL) && DGSelectDrawable(&consumer,1,32,32));
    unsigned imported=DGSurfaceAcquire(&consumer,0x781,1,64,64);assert(imported);
    struct DGSurfaceLease *cached=DGSurfaceFind(&consumer,imported);
    assert(cached && cached->resource==published->resource);
    for(unsigned endpoint=0;endpoint<3;++endpoint)for(unsigned step=1;step<=64;++step){
        unsigned iteration=endpoint*64+step;
        if(step==1){printf("endpoint=%u (0=region-import, 1=snapshot, 2=framebuffer-copy)\n",endpoint);fflush(stdout);}
        unsigned r=iteration*37%256,g=iteration*71%256,b=iteration*113%256;
        assert(!CGLSetCurrentContext(producer.cgl));
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,producer.framebuffer);
        if(set_viewport)glViewport(0,0,64,64);
        glDisable(GL_SCISSOR_TEST);glClearColor(r/255.f,g/255.f,b/255.f,1);
        glClear(GL_COLOR_BUFFER_BIT);DGMarkDrawableDamage(&producer);
        if(source_read){
            fprintf(stderr,"producer local drawable after clear\n");
            check_pixel(0,0,r,g,b);
        }
        assert(!DGSurfacePublish(&producer,published,0,0));
        if(source_read){
            fprintf(stderr,"producer canonical resource after publication\n");
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,published->framebuffer);
            check_pixel(0,0,r,g,b);
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,producer.framebuffer);
        }
        published->resource->epoch=iteration*2;
        // No CPU read, fence wait or finish before switching consumers.
        assert(!CGLSetCurrentContext(consumer.cgl));
        GLuint sentinel=0;glGenTextures(1,&sentinel);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,sentinel);
        /* Diagnostic control only: compare the public producer-flush /
         * consumer-bind contract against the unmodified production endpoint. */
        if(rebind_source){
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,cached->texture);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,sentinel);
        }
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,consumer.framebuffer);
        if(set_viewport)glViewport(0,0,32,32);
        glEnable(GL_SCISSOR_TEST);glScissor(1,2,3,4);glEnable(GL_FRAMEBUFFER_SRGB_EXT);
        if(endpoint==0){
        struct DGMessage import={.op=DG_SURFACE_IMPORT_REGION};
        import.args[0]=imported;import.args[1]=iteration*2;
        import.args[2]=7;import.args[3]=11;import.args[4]=32;import.args[5]=32;
        struct DGReply reply={0};assert(!DGSurfaceCommand(&consumer,&import,&reply) && !reply.error);
        GLint read=0,draw=0,texture=0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
        glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);
        assert((GLuint)read==consumer.framebuffer && read==draw && (GLuint)texture==sentinel);
        assert(glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_FRAMEBUFFER_SRGB_EXT));
        check_pixel(0,0,r,g,b);check_pixel(31,31,r,g,b);
        }else if(endpoint==1){
        struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreate(&consumer,cached,iteration*2);assert(v);
        assert(DGSurfaceSnapshotCopyTexture(&consumer,v,GL_RGBA8,64,64)==GL_NO_ERROR);
        GLuint verify=0;glGenFramebuffersEXT(1,&verify);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,verify);
        glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,sentinel,0);
        assert(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);check_pixel(0,0,r,g,b);check_pixel(63,63,r,g,b);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,consumer.framebuffer);glDeleteFramebuffersEXT(1,&verify);
        DGSurfaceSnapshotDestroy(v);
        }else{
        // CopyRegion takes a snapshot of the independently published family.
        // Uniform colors check freshness, not move geometry/overlap semantics
        // (the asymmetric surface-copy fixture covers those separately).
        struct {struct DGMessage header;struct DGSurfaceRegion region;} move={0};
        move.header.op=DG_SURFACE_COPY_REGIONS;move.header.bytes=sizeof(move.region);
        move.header.args[0]=imported;move.header.args[1]=iteration*2;move.header.args[2]=iteration*2+1;
        move.header.args[3]=1;move.header.args[6]=1;
        move.region=(struct DGSurfaceRegion){0,0,32,32,16,16};
        struct DGReply reply={0};assert(!DGSurfaceCommand(&consumer,&move.header,&reply) && !reply.error);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,cached->framebuffer);
        check_pixel(16,16,r,g,b);check_pixel(47,47,r,g,b);
        }
        glDeleteTextures(1,&sentinel);DGForgetAliases(&consumer,1,&sentinel);
    }
    assert(!consumer.error && !glGetError());
    DGDestroySurfaceLeases(&consumer);DGDestroyDrawables(&consumer);DGDestroyAliases(&consumer);
    assert(!CGLSetCurrentContext(producer.cgl));
    DGDestroySurfaceLeases(&producer);DGDestroyDrawables(&producer);DGDestroyAliases(&producer);
    assert(!CGLSetCurrentContext(NULL));CGLDestroyContext(consumer.cgl);CGLDestroyContext(producer.cgl);
    DGSnapshotCleanupDrain();
    printf("PASS profile=%x: 64 updates per endpoint, independent contexts, freshness and import state; explicit-rebind-control=%d\n",
        profile,rebind_source);
}
int main(void)
{
    run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);return 0;
}
