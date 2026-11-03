/* Real native immutable-version sealing, not a mock/desktop FPS benchmark.
 * Exercise the production code, retain old GPU images through later copies,
 * and prove that cached binding objects retain no image or caller state. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
#include <mach/mach_time.h>
static int compare_double(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
static void benchmark(struct DGHostContext *c,struct DGSurfaceLease *p,uint64_t epoch)
{
    double times[2][120];unsigned used[2]={0,0};mach_timebase_info_data_t clock;mach_timebase_info(&clock);
    for(unsigned trial=0;trial<140;++trial)for(unsigned step=0;step<2;++step){
        unsigned reuse=(trial+step)&1;dg_snapshot_framebuffer_reuse=reuse;
        uint64_t begin=mach_absolute_time();struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreate(c,p,epoch);
        uint64_t end=mach_absolute_time();assert(v && v->completion);
        if(trial>=20)times[reuse][used[reuse]++]=(end-begin)*(double)clock.numer/clock.denom/1e3;
        // Deliberately outside the measured production submission. No trial
        // inherits the previous trial's GPU queue or deferred storage cleanup.
        glFinish();assert(!glGetError());DGSurfaceSnapshotDestroy(v);
    }
    for(unsigned reuse=0;reuse<2;++reuse){
        assert(used[reuse]==120);qsort(times[reuse],120,sizeof(double),compare_double);
        printf("TIMING seal reuse=%u %dx%d CPU_submission_us median=%.3f p95=%.3f (NOT GPU/FPS)\n",
            reuse,p->resource->width,p->resource->height,times[reuse][60],times[reuse][114]);
    }
    dg_snapshot_framebuffer_reuse=1;
}
static void empty(GLuint framebuffer)
{
    GLint saved=0,attachment=-1;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&saved);
    glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,framebuffer);
    glGetFramebufferAttachmentParameterivEXT(GL_DRAW_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT,&attachment);
    assert(attachment==GL_NONE);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,(GLuint)saved);
    assert(!glGetError());
}
static void run(unsigned profile)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0,0,0};
    const char *renderer=getenv("VIM_PASSTHROUGH_OPENGL_RENDERER_ID");
    if(renderer){attrs[5]=kCGLPFARendererID;attrs[6]=(CGLPixelFormatAttribute)strtoul(renderer,NULL,0);}
    CGLPixelFormatObj format=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&format,&count) && format);
    assert(!CGLCreateContext(format,NULL,&c.cgl));CGLDestroyPixelFormat(format);
    assert(!CGLSetCurrentContext(c.cgl));assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    printf("renderer=%s profile=%x\n",glGetString(GL_RENDERER),profile);
    c.profile=profile;c.draw_mode=c.read_mode=GL_FRONT;c.surface_namespace[0]=0x7365616c66626fULL;c.surface_namespace[1]=profile;
    assert(DGCreateAliases(&c,NULL));assert(DGSelectDrawable(&c,1,173,151));
    uint32_t name=DGSurfaceAcquire(&c,64001,1,173,151);assert(name);
    struct DGSurfaceLease *p=DGSurfaceFind(&c,name);assert(p);
    GLuint read=0,draw=0,texture=0;glGenFramebuffersEXT(1,&read);glGenFramebuffersEXT(1,&draw);glGenTextures(1,&texture);
    struct DGSurfaceSnapshot *versions[16]={0};uint64_t epoch=0;
    for(unsigned phase=0;phase<2;++phase){
        dg_snapshot_framebuffer_reuse=phase;
        uint64_t allocations=dg_snapshot_seal_framebuffer_allocations,reuses=dg_snapshot_seal_framebuffer_reuses;
        for(unsigned i=0;i<16;++i){
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
            glClearColor((i+1)/255.f,phase/255.f,71/255.f,1);glClear(GL_COLOR_BUFFER_BIT);
            struct DGMessage publish={.op=DG_SURFACE_PUBLISH,.args={name,0,0,++epoch}};struct DGReply reply={0};
            assert(!DGSurfaceCommand(&c,&publish,&reply) && !reply.error);
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glEnable(GL_SCISSOR_TEST);glScissor(0,0,0,0);
            versions[i]=DGSurfaceSnapshotCreate(&c,p,epoch);assert(versions[i] && versions[i]->completion);
            GLint value=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&value);assert((GLuint)value==read);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&value);assert((GLuint)value==draw);
            glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&value);assert((GLuint)value==texture);
            assert(glIsEnabled(GL_SCISSOR_TEST) && !glGetError());
            if(phase)empty(c.snapshot_draw_framebuffer);
            glDisable(GL_SCISSOR_TEST);
        }
        assert(dg_snapshot_seal_framebuffer_allocations-allocations==(phase?1u:16u));
        assert(dg_snapshot_seal_framebuffer_reuses-reuses==(phase?15u:0u));
        for(unsigned i=0;i<16;++i){
            glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);
            glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
                GL_TEXTURE_RECTANGLE_ARB,versions[i]->native_texture,0);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
            unsigned char pixel[4]={0};glReadPixels(70,80,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            assert(pixel[0]==i+1 && pixel[1]==phase && pixel[2]==71 && pixel[3]==255 && !glGetError());
            glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
            DGSurfaceSnapshotDestroy(versions[i]);versions[i]=NULL;
        }
        printf("PASS seal reuse=%u 16 retained independent GPU epochs allocations=%llu reuses=%llu\n",phase,
            (unsigned long long)(dg_snapshot_seal_framebuffer_allocations-allocations),
            (unsigned long long)(dg_snapshot_seal_framebuffer_reuses-reuses));
    }
    // An explicitly borrowed scratch binding must fall back without erasing
    // the caller's attachment, whether it is bound for reading OR drawing.
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,7,9,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    for(unsigned reading=0;reading<2;++reading){
        GLenum target=reading?GL_READ_FRAMEBUFFER_EXT:GL_DRAW_FRAMEBUFFER_EXT;
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
        glBindFramebufferEXT(target,c.snapshot_draw_framebuffer);
        glFramebufferTexture2DEXT(target,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
        uint64_t allocations=dg_snapshot_seal_framebuffer_allocations,reuses=dg_snapshot_seal_framebuffer_reuses;
        struct DGSurfaceSnapshot *v=DGSurfaceSnapshotCreate(&c,p,epoch);assert(v);
        GLint value=0;glGetIntegerv(reading?GL_READ_FRAMEBUFFER_BINDING_EXT:GL_DRAW_FRAMEBUFFER_BINDING_EXT,&value);
        assert((GLuint)value==c.snapshot_draw_framebuffer);
        glGetFramebufferAttachmentParameterivEXT(target,GL_COLOR_ATTACHMENT0_EXT,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT,&value);
        assert((GLuint)value==texture && dg_snapshot_seal_framebuffer_allocations==allocations+1 &&
            dg_snapshot_seal_framebuffer_reuses==reuses);
        glFramebufferTexture2DEXT(target,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
        DGSurfaceSnapshotDestroy(v);assert(!glGetError());
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,draw);
    // Real rejected native source; failure must also leave cached storage
    // detached. Never expose this invalid lease to the VM or public wire.
    GLuint source=p->framebuffer;p->framebuffer=0;
    struct DGSurfaceSnapshot *failed=DGSurfaceSnapshotCreate(&c,p,epoch);p->framebuffer=source;
    assert(!failed);empty(c.snapshot_draw_framebuffer);
    puts("PASS caller-bound scratch read/draw fallback and failed seal detach");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);benchmark(&c,p,epoch);
    assert(DGSelectDrawable(&c,2,2560,1540));
    uint32_t large_name=DGSurfaceAcquire(&c,64002,1,2560,1540);assert(large_name);
    struct DGSurfaceLease *large=DGSurfaceFind(&c,large_name);assert(large);
    glClearColor(.25,.5,.75,1);glClear(GL_COLOR_BUFFER_BIT);
    struct DGMessage publish={.op=DG_SURFACE_PUBLISH,.args={large_name,0,0,1}};struct DGReply reply={0};
    assert(!DGSurfaceCommand(&c,&publish,&reply) && !reply.error);benchmark(&c,large,1);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,0);
    glDeleteFramebuffersEXT(1,&read);glDeleteFramebuffersEXT(1,&draw);glDeleteTextures(1,&texture);
    GLuint scratch=c.snapshot_draw_framebuffer;DGDestroySurfaceLeases(&c);
    assert(!c.snapshot_draw_framebuffer && !glIsFramebufferEXT(scratch));
    DGDestroyDrawables(&c);DGDestroyAliases(&c);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    DGSnapshotCleanupDrain();assert(!dg_snapshot_bytes && !dg_snapshot_spare_bytes && !dg_surface_count && !dg_surface_leases);
    puts("PASS context teardown releases empty scratch and every version/lease");
}
int main(void){run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);return 0;}
