/* Real-GPU framebuffer move qualification. Pixel reads are assertions only;
 * the implementation transports rectangle metadata, not CPU image data. */
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
static unsigned copy_flushes,copy_fences;
static void CopyTestFlush(void) { ++copy_flushes;glFlush(); }
static GLsync CopyTestFence(GLenum condition,GLbitfield flags)
{ ++copy_fences;return glFenceSync(condition,flags); }
#define glFlush CopyTestFlush
#define glFenceSync CopyTestFence
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#undef glFlush
#undef glFenceSync
#include <assert.h>
static void run(unsigned profile)
{
    struct DGHostContext c={0};c.profile=profile;c.surface_namespace[0]=getpid();c.surface_namespace[1]=profile;
    uint64_t reuse_before=dg_snapshot_storage_reuses;
    uint64_t allocations_before=dg_snapshot_storage_allocations;
    c.draw_mode=c.read_mode=GL_FRONT;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));
    printf("profile=%x native renderer=%s\n",profile,glGetString(GL_RENDERER));fflush(stdout);
    assert(strstr((const char *)glGetString(GL_VENDOR),"Intel"));
    assert(DGCreateAliases(&c,NULL) && DGSelectDrawable(&c,1,32,32));
    uint32_t lease=DGSurfaceAcquire(&c,17,1,64,64);assert(lease);
    struct DGSurfaceLease *p=DGSurfaceFind(&c,lease);assert(p);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,p->framebuffer);glEnable(GL_SCISSOR_TEST);
    unsigned char expected[64*64*4],observed[sizeof(expected)],original[sizeof(expected)];
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        glScissor(x*8,y*8,8,8);glClearColor(x*32/255.f,y*32/255.f,17/255.f,1);glClear(GL_COLOR_BUFFER_BIT);
    }
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,original);
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
        unsigned i=(y*64+x)*4;expected[i]=(x/8)*32;expected[i+1]=(y/8)*32;expected[i+2]=17;expected[i+3]=255;
    }
    assert(!memcmp(original,expected,sizeof(expected)));p->resource->epoch=7;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);glScissor(1,2,3,4);
    GLuint caller_texture=0;glGenTextures(1,&caller_texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,caller_texture);
    struct {struct DGMessage header;struct DGSurfaceRegion regions[2];} request={0};
    request.header.op=DG_SURFACE_COPY_REGIONS;request.header.bytes=sizeof(request.regions);
    request.header.args[0]=lease;request.header.args[1]=7;request.header.args[2]=8;
    request.header.args[3]=2;request.header.args[6]=1;
    request.regions[0]=(struct DGSurfaceRegion){0,0,48,32,16,0};
    request.regions[1]=(struct DGSurfaceRegion){48,0,16,32,0,0};
    for(unsigned n=0;n<2;++n) {
        const struct DGSurfaceRegion *r=&request.regions[n];
        for(int y=0;y<r->height;++y)memcpy(expected+((r->destination_y+y)*64+r->destination_x)*4,
            original+((r->y+y)*64+r->x)*4,(size_t)r->width*4);
    }
    unsigned before_flushes=copy_flushes,before_fences=copy_fences;
    struct DGReply reply={0};DGSurfaceCommand(&c,&request.header,&reply);
    assert(!reply.error && p->resource->epoch==8 && copy_flushes==before_flushes+1 && copy_fences==before_fences);
    GLint read=0,draw=0,texture=0,box[4]={0};
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);
    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&texture);glGetIntegerv(GL_SCISSOR_BOX,box);
    assert((GLuint)read==c.framebuffer && (GLuint)draw==c.framebuffer && (GLuint)texture==caller_texture &&
        glIsEnabled(GL_SCISSOR_TEST) && box[0]==1 && box[1]==2 && box[2]==3 && box[3]==4);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,observed);assert(!memcmp(observed,expected,sizeof(expected)));
    request.header.args[2]=9;reply=(struct DGReply){0};DGSurfaceCommand(&c,&request.header,&reply);
    assert(reply.error==GL_INVALID_OPERATION && p->resource->epoch==8);
    request.header.args[1]=8;request.regions[1].destination_x=1;
    reply=(struct DGReply){0};DGSurfaceCommand(&c,&request.header,&reply);
    assert(reply.error==GL_INVALID_VALUE && p->resource->epoch==8);
    request.regions[1].destination_x=64;
    reply=(struct DGReply){0};DGSurfaceCommand(&c,&request.header,&reply);
    assert(reply.error==GL_INVALID_VALUE && p->resource->epoch==8);
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,observed);assert(!memcmp(observed,expected,sizeof(expected)));
    /* The same storage can also be this context's default drawable. An
     * internal copy must invalidate its live destination-alias serial. */
    assert(DGSelectDrawable(&c,2,64,64));
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);assert(!DGSurfaceDrawable(&c,p));
    uint64_t serial=c.drawable->write_serial;memcpy(original,expected,sizeof(expected));
    request.header.args[1]=8;request.header.args[2]=9;request.regions[1].destination_x=0;
    for(unsigned n=0;n<2;++n) {
        const struct DGSurfaceRegion *r=&request.regions[n];
        for(int y=0;y<r->height;++y)memcpy(expected+((r->destination_y+y)*64+r->destination_x)*4,
            original+((r->y+y)*64+r->x)*4,(size_t)r->width*4);
    }
    reply=(struct DGReply){0};DGSurfaceCommand(&c,&request.header,&reply);
    assert(!reply.error && p->resource->epoch==9 && c.drawable->write_serial==serial+1 && c.drawable->alias_dirty);
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,observed);assert(!memcmp(observed,expected,sizeof(expected)));
    before_flushes=copy_flushes;before_fences=copy_fences;
    struct DGSurfaceSnapshot *held=DGSurfaceSnapshotCreate(&c,p,9);assert(held && held->completion);
    assert(copy_flushes==before_flushes+1 && copy_fences==before_fences+1);
    unsigned char held_expected[sizeof(expected)];memcpy(held_expected,expected,sizeof(expected));
    /* Reuse the producer-local scratch many times with NO intervening CPU
     * read/Finish/fence. Native queue order must preserve every overlapping
     * move and the independently held/exported old version. */
    for(unsigned iteration=0;iteration<31;++iteration){
        memcpy(original,expected,sizeof(expected));
        for(unsigned n=0;n<2;++n){
            const struct DGSurfaceRegion *r=&request.regions[n];
            for(int y=0;y<r->height;++y)memcpy(expected+((r->destination_y+y)*64+r->destination_x)*4,
                original+((r->y+y)*64+r->x)*4,(size_t)r->width*4);
        }
        request.header.args[1]=9+iteration;request.header.args[2]=10+iteration;
        before_flushes=copy_flushes;before_fences=copy_fences;
        reply=(struct DGReply){0};DGSurfaceCommand(&c,&request.header,&reply);
        assert(!reply.error && p->resource->epoch==10+iteration &&
            copy_flushes==before_flushes+1 && copy_fences==before_fences);
    }
    assert(memcmp(expected,held_expected,sizeof(expected)));
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,observed);assert(!memcmp(observed,expected,sizeof(expected)));
    for(unsigned i=0;i<12;++i){
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,p->framebuffer);glDisable(GL_SCISSOR_TEST);
        glClearColor(i&1,0,!(i&1),1);glClear(GL_COLOR_BUFFER_BIT);p->resource->epoch=41+i;
        struct DGSurfaceSnapshot *temporary=DGSurfaceSnapshotCreate(&c,p,41+i);assert(temporary);
        assert(temporary->storage!=held->storage && temporary->native_texture!=held->native_texture);
        DGSurfaceSnapshotDestroy(temporary);
    }
    GLuint verification=0;glGenFramebuffersEXT(1,&verification);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,verification);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,held->native_texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,observed);
    assert(!memcmp(observed,held_expected,sizeof(held_expected)) && dg_snapshot_storage_allocations-allocations_before<=2);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c.framebuffer);glDeleteFramebuffersEXT(1,&verification);
    DGSurfaceSnapshotDestroy(held);
    glDeleteTextures(1,&caller_texture);DGDestroySurfaceLeases(&c);DGDestroyDrawables(&c);DGDestroyAliases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);DGSnapshotCleanupDrain();
    assert(!dg_snapshot_bytes && !dg_surface_bytes && !dg_surface_count && !dg_surface_leases);
    assert(dg_snapshot_storage_reuses>reuse_before && !dg_snapshot_spare_bytes);
    printf("PASS overlap-safe GPU moves: 31 queued scratch reuses without intermediate CPU read/fence, one Flush per copy, exported fence preserved, held-version immunity, pixels/state/epochs, two allocations, cleanup\n");
}
int main(void){run(kCGLOGLPVersion_Legacy);run(kCGLOGLPVersion_3_2_Core);return 0;}
