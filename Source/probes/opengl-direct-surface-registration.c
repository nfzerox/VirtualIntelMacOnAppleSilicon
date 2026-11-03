/* Actual native retained-surface attachment lifetime controls. No VM, Float,
 * fake GL, private OS offsets or changed synchronization. Run on current
 * Intel and physical Mavericks Intel in BOTH widths. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>

static void pixel(unsigned r,unsigned g,unsigned b)
{
    unsigned char p[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    assert(p[0]==r && p[1]==g && p[2]==b && p[3]==255 && !glGetError());
}
static void lease_pixel(struct DGSurfaceLease *p,unsigned r,unsigned g,unsigned b)
{
    GLint read=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    pixel(r,g,b);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)read);
}
int main(int argc,char **argv)
{
    if(argc!=2 || (strcmp(argv[1],"0") && strcmp(argv[1],"1")))return 2;
    dg_surface_registration_reuse=atoi(argv[1]);
    dg_storage_reuse=1;
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(a,&format,&count) && format);
    assert(!CGLCreateContext(format,NULL,&c.cgl));CGLDestroyPixelFormat(format);
    assert(!CGLSetCurrentContext(c.cgl));
    printf("renderer=%s reuse=%d\n",glGetString(GL_RENDERER),dg_surface_registration_reuse);
    assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;
    c.surface_namespace[0]=0x7265676973746572ULL;c.surface_namespace[1]=1;
    assert(DGCreateAliases(&c,NULL));
    uint32_t name=DGSurfaceAcquire(&c,701,1,173,151);assert(name);
    struct DGSurfaceLease *p=DGSurfaceFind(&c,name);assert(p && p->registration);
    for(unsigned i=1;i<=32;++i){
        assert(DGSelectDrawable(&c,i,173,151));
        assert(!DGSurfaceDrawable(&c,p));
        if(dg_surface_registration_reuse)assert(c.color==p->texture && c.drawable->surface_registration==p->registration);
        glClearColor(i%2,!(i%2),0,1);glClear(GL_COLOR_BUFFER_BIT);
        pixel(i%2?255:0,i%2?0:255,0);lease_pixel(p,i%2?255:0,i%2?0:255,0);
        DGDropDrawable(&c,i);
    }
    assert(c.storage_texture_reuses>=31 && c.spare_bytes<=32*1024*1024);
    assert(dg_surface_registration_stats[0]==(dg_surface_registration_reuse?1:33));
    assert(dg_surface_registration_stats[1]==(dg_surface_registration_reuse?32:0));
    assert(dg_surface_registration_stats[2]==32 && dg_surface_registration_stats[3]==1);
    /* Lease can go away FIRST. The drawable must keep a live GL name and the
     * exact pool identity, then a later independently imported lease sees it. */
    assert(DGSelectDrawable(&c,100,173,151) && !DGSurfaceDrawable(&c,p));
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);pixel(255,0,0);
    assert(DGSurfaceRelease(&c,name));pixel(255,0,0);
    assert(glIsTexture(c.color));glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);pixel(0,255,0);
    name=DGSurfaceAcquire(&c,701,1,173,151);p=DGSurfaceFind(&c,name);assert(p);
    lease_pixel(p,0,255,0);
    assert(DGSelectDrawable(&c,101,173,151) && !DGSurfaceDrawable(&c,p));
    DGDropDrawable(&c,100);pixel(0,255,0);
    /* An exported alias survives BOTH registration owners; recycling an
     * independent blue target must not overwrite this green storage. */
    GLuint alias=0;glGenTextures(1,&alias);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,alias);
    struct DGMessage bind={.args={GL_RGBA8,173,151}};struct DGReply reply={0};
    assert(!DGBindDrawableTexture(&c,&bind,&reply) && !reply.error);
    DGDropDrawable(&c,101);lease_pixel(p,0,255,0);assert(DGSurfaceRelease(&c,name));
    assert(!dg_surface_registration_stats[3]);
    assert(DGSelectDrawable(&c,102,173,151));glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);pixel(0,0,255);
    GLuint read=0;glGenFramebuffersEXT(1,&read);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,read);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,alias,0);
    assert(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);pixel(0,255,0);glDeleteFramebuffersEXT(1,&read);
    DGForgetAliases(&c,1,&alias);glDeleteTextures(1,&alias);
    DGDestroySurfaceLeases(&c);DGDestroyDrawables(&c);DGDestroyAliases(&c);
    assert(!dg_surfaces && !dg_surface_count && !dg_surface_leases && !dg_surface_registration_stats[3]);
    assert(!c.spare_bytes && !glGetError());CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    puts("PASS exact registration reuse, zero temporary reimports after warmup, both retirement orders, independent new storage, surviving exported alias and balanced destruction");
    return 0;
}
