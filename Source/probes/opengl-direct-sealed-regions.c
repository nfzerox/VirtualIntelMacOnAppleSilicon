/* Actual production compound command, native Intel GPU, both GL profiles.
 * One submission/fence, exact old/new pixels, immutable retained versions,
 * and rejection before mutation. ReadPixels is verification only. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
static unsigned flushes,fences;
static void TestFlush(void){++flushes;glFlush();}
static GLsync TestFence(GLenum c,GLbitfield f){++fences;return glFenceSync(c,f);}
#define glFlush TestFlush
#define glFenceSync TestFence
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#undef glFlush
#undef glFenceSync
#include <assert.h>
static void pixel(unsigned x,unsigned y,unsigned red,unsigned green,unsigned blue)
{
    unsigned char p[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    assert(p[0]==red && p[1]==green && p[2]==blue && p[3]==255 && !glGetError());
}
static void run(unsigned profile)
{
    struct DGHostContext c={0};c.profile=profile;c.draw_mode=c.read_mode=GL_FRONT;
    c.surface_namespace[0]=getpid();c.surface_namespace[1]=profile;
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;GLint n=0;
    assert(!CGLChoosePixelFormat(a,&pf,&n) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));
    printf("profile=%x renderer=%s\n",profile,glGetString(GL_RENDERER));
    assert(strstr((const char *)glGetString(GL_VENDOR),"Intel"));
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,32,32));
    uint32_t lease=DGSurfaceAcquire(&c,23001,1,64,64);assert(lease);
    struct DGSurfaceLease *p=DGSurfaceFind(&c,lease);assert(p);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,p->framebuffer);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);p->resource->epoch=1;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    struct {struct DGMessage m;struct DGSurfaceRegion regions[2];} q={0};
    q.m.op=DG_SURFACE_SEALED_REGIONS;q.m.flags=DG_INPUT_ONLY|DG_REPLY;q.m.bytes=sizeof(q.regions);
    q.m.args[0]=lease;q.m.args[1]=1;q.m.args[2]=2;q.m.args[3]=2;q.m.args[6]=1;
    q.regions[0]=(struct DGSurfaceRegion){0,0,8,8,0,0};
    q.regions[1]=(struct DGSurfaceRegion){8,8,8,8,32,32};
    assert(DGHeaderValid(&q.m));
    unsigned before=flushes,before_fences=fences;struct DGReply r={0};
    assert(!DGSurfaceCommand(&c,&q.m,&r) && !r.error);
    struct DGSurfaceSnapshot *held=p->resource->published_version;
    assert(held && held->completion && held->epoch==2 && p->resource->epoch==2);
    assert(flushes==before+1 && fences==before_fences+1);__sync_add_and_fetch(&held->refs,1);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    pixel(0,0,0,0,255);pixel(32,32,0,0,255);pixel(16,16,255,0,0);
    q.m.args[1]=2;q.m.args[2]=3;q.m.args[7]=1;
    q.regions[0]=(struct DGSurfaceRegion){0,0,32,32,32,0};
    q.regions[1]=(struct DGSurfaceRegion){32,0,32,32,0,0}; // overlap-safe swap
    before=flushes;before_fences=fences;r=(struct DGReply){0};
    assert(!DGSurfaceCommand(&c,&q.m,&r) && !r.error && p->resource->epoch==3);
    assert(p->resource->published_version->epoch==3 && flushes==before+1 && fences==before_fences+1);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    pixel(0,0,255,0,0);pixel(32,0,0,0,255);pixel(32,32,0,0,255);
    GLuint verify=0;glGenFramebuffersEXT(1,&verify);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,verify);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,held->native_texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);pixel(0,0,0,0,255);pixel(32,0,255,0,0);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,0,0);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c.framebuffer);glDeleteFramebuffersEXT(1,&verify);
    before=flushes;before_fences=fences;q.m.args[2]=4;r=(struct DGReply){0};
    assert(!DGSurfaceCommand(&c,&q.m,&r) && r.error==GL_INVALID_OPERATION && p->resource->epoch==3);
    q.m.args[1]=3;q.regions[1].destination_x=31;r=(struct DGReply){0};
    assert(!DGSurfaceCommand(&c,&q.m,&r) && r.error==GL_INVALID_VALUE && p->resource->epoch==3);
    assert(flushes==before && fences==before_fences);
    q.m.args[7]=2;r=(struct DGReply){0};
    assert(!DGSurfaceCommand(&c,&q.m,&r) && r.error==GL_INVALID_VALUE);
    DGSurfaceSnapshotDestroy(held);DGDestroySurfaceLeases(&c);DGDestroyDrawables(&c);DGDestroyAliases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);DGSnapshotCleanupDrain();
    assert(!dg_snapshot_bytes && !dg_snapshot_spare_bytes && !dg_surface_count && !dg_surface_leases);
    puts("PASS sealed publication/copy: one Flush and fence, overlap-safe pixels, held epoch, stale/invalid rejection, cleanup");
}
int main(void){run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);return 0;}
