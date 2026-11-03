/* GPU-only region publication, with the real retained-surface implementation.
 * Readback here verifies test results; it is not part of region publication. */
#include <OpenGL/gl.h>
static unsigned publication_flushes;
static void PublicationTestFlush(void) { ++publication_flushes;glFlush(); }
#define glFlush PublicationTestFlush
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#undef glFlush
static int pixel(int x,int y,unsigned r,unsigned g,unsigned b)
{
    unsigned char p[4]={0};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    return p[0]==r && p[1]==g && p[2]==b;
}
int main(int argc,char **argv)
{
    int publication_only=argc==2 && !strcmp(argv[1],"--publication-only");
    if(argc!=1 && !publication_only)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    if(CGLChoosePixelFormat(attrs,&pf,&count) || CGLCreateContext(pf,NULL,&c.cgl))return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);
    printf("native GPU: vendor=%s renderer=%s\n",glGetString(GL_VENDOR),glGetString(GL_RENDERER));
    if(getenv("VIM_REQUIRE_INTEL") && !strstr((const char *)glGetString(GL_VENDOR),"Intel"))return 2;
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;c.surface_namespace[0]=1;
    if(!DGCreateAliases(&c,NULL) || !DGSelectDrawable(&c,1,32,32))return 2;
    uint32_t lease=DGSurfaceAcquire(&c,1,1,64,64);
    struct DGSurfaceLease *p=DGSurfaceFind(&c,lease);if(!p)return 2;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,p->framebuffer);
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);glScissor(0,16,32,16);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    /* Deliberately leave a restrictive caller scissor: publication must
     * preserve it, but must not accidentally clip the private GPU copy. */
    glScissor(0,0,1,1);
    int good=DGSurfacePublishRect(&c,p,0,0,32,32,8,8,0,0)==GL_NO_ERROR;
    GLint binding=0,box[4]={0};glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&binding);
    glGetIntegerv(GL_SCISSOR_BOX,box);
    good=(GLuint)binding==c.framebuffer && glIsEnabled(GL_SCISSOR_TEST) && box[2]==1 && box[3]==1 && good;
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    good=pixel(0,0,255,0,0) && pixel(7,8,255,0,0) && pixel(40,8,255,0,0) &&
         pixel(8,8,0,255,0) && pixel(8,39,0,0,255) && good;
    good=DGSurfacePublishRect(&c,p,0,0,8,32,48,16,1,0)==GL_NO_ERROR && good;
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    good=pixel(48,16,0,0,255) && pixel(48,47,0,255,0) && pixel(47,16,255,0,0) && good;
    good=DGSurfacePublishRect(&c,p,0,0,32,32,48,48,0,0)==GL_INVALID_OPERATION && good;
    good=DGSurfacePublishRect(&c,p,-1,0,8,8,0,0,0,0)==GL_INVALID_OPERATION && good;
    good=DGSurfacePublish(&c,p,0,0)==GL_INVALID_OPERATION && good;
    /* Preserve the already-shipped full-surface publication contract. */
    good=DGSelectDrawable(&c,2,64,64) && good;glDisable(GL_SCISSOR_TEST);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);
    glClearColor(0,1,1,1);glClear(GL_COLOR_BUFFER_BIT);
    good=DGSurfacePublish(&c,p,0,0)==GL_NO_ERROR && good;
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    good=pixel(0,0,0,255,255) && pixel(63,63,0,255,255) && good;
    struct DGMessage initialize={.op=DG_SURFACE_PUBLISH};struct DGReply reply={0};
    initialize.args[0]=lease;initialize.args[3]=1;DGSurfaceCommand(&c,&initialize,&reply);
    good=!reply.error && p->resource->epoch==1 && good;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);
    struct { struct DGMessage header;struct DGSurfaceRegion regions[2]; } batch={0};
    batch.header.op=DG_SURFACE_PUBLISH_REGIONS;batch.header.bytes=sizeof(batch.regions);
    batch.header.args[0]=lease;batch.header.args[1]=1;batch.header.args[2]=2;
    batch.header.args[3]=2;batch.header.args[6]=1;
    batch.regions[0]=(struct DGSurfaceRegion){0,0,8,8,0,0};
    batch.regions[1]=(struct DGSurfaceRegion){0,0,8,8,63,63};
    unsigned before_flushes=publication_flushes;
    reply=(struct DGReply){0};DGSurfaceCommand(&c,&batch.header,&reply);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    good=reply.error==GL_INVALID_VALUE && publication_flushes==before_flushes &&
        p->resource->epoch==1 && pixel(0,0,0,255,255) && good;
    batch.regions[1].destination_x=32;batch.regions[1].destination_y=32;
    before_flushes=publication_flushes;
    reply=(struct DGReply){0};DGSurfaceCommand(&c,&batch.header,&reply);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    good=!reply.error && publication_flushes==before_flushes+1 &&
         p->resource->epoch==2 && pixel(0,0,255,0,255) && pixel(32,32,255,0,255) &&
         pixel(16,16,0,255,255) && good;
    /* Stale writers and overlapping clip pieces cannot advance ownership. */
    batch.header.args[2]=3;reply=(struct DGReply){0};DGSurfaceCommand(&c,&batch.header,&reply);
    good=reply.error==GL_INVALID_OPERATION && p->resource->epoch==2 && good;
    batch.header.args[1]=2;batch.regions[1].destination_x=4;batch.regions[1].destination_y=4;
    reply=(struct DGReply){0};DGSurfaceCommand(&c,&batch.header,&reply);
    good=reply.error==GL_INVALID_VALUE && p->resource->epoch==2 && good;
    if(publication_only){
        printf("%s publication-only: pixels, flip, preserved state, epochs, atomic validation, one Flush per valid batch\n",good?"PASS":"FAIL");
        DGDestroySurfaceLeases(&c);DGDestroyDrawables(&c);DGDestroyAliases(&c);
        CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);return !good;
    }
    /* A native composition attachment is a cropped view of a persistent
     * display family, not a CPU snapshot. Exercise both row orientations,
     * stale epochs, bounds and caller-state preservation on the real GPU. */
    printf("pre-import checks=%d error=%x\n",good,glGetError());
    good=DGSelectDrawable(&c,3,32,32) && good;
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,c.framebuffer);
    glEnable(GL_SCISSOR_TEST);glScissor(1,2,3,4);
    struct DGMessage import={.op=DG_SURFACE_IMPORT_REGION};
    import.args[0]=lease;import.args[1]=2;import.args[4]=32;import.args[5]=32;
    if(getenv("VIM_REGION_DIAG_FINISH"))glFinish();
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,p->framebuffer);
    unsigned char source_pixel[4]={0};glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,source_pixel);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,c.framebuffer);
    printf("crop source=%u,%u,%u read=%x draw=%x\n",source_pixel[0],source_pixel[1],source_pixel[2],
        glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT),glCheckFramebufferStatusEXT(GL_DRAW_FRAMEBUFFER_EXT));
    reply=(struct DGReply){0};DGSurfaceCommand(&c,&import,&reply);
    if(getenv("VIM_REGION_DIAG_FINISH"))glFinish();
    GLint import_read=0,import_draw=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&import_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&import_draw);glGetIntegerv(GL_SCISSOR_BOX,box);
    unsigned char import_pixels[12]={0};
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,import_pixels);
    glReadPixels(0,31,1,1,GL_RGBA,GL_UNSIGNED_BYTE,import_pixels+4);
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,import_pixels+8);
    printf("crop import error=%x fb=%u/%u expected=%u scissor=%d box=%d,%d %dx%d pixels=%u,%u,%u/%u,%u,%u/%u,%u,%u\n",
        reply.error,import_read,import_draw,c.framebuffer,glIsEnabled(GL_SCISSOR_TEST),box[0],box[1],box[2],box[3],
        import_pixels[0],import_pixels[1],import_pixels[2],import_pixels[4],import_pixels[5],import_pixels[6],
        import_pixels[8],import_pixels[9],import_pixels[10]);
    good=!reply.error && (GLuint)import_read==c.framebuffer && (GLuint)import_draw==c.framebuffer &&
        glIsEnabled(GL_SCISSOR_TEST) && box[0]==1 && box[1]==2 && box[2]==3 && box[3]==4 &&
        pixel(0,0,255,0,255) && pixel(0,31,0,255,255) && pixel(16,16,0,255,255) && good;
    import.args[6]=1;reply=(struct DGReply){0};DGSurfaceCommand(&c,&import,&reply);
    good=!reply.error && pixel(0,0,0,255,255) && pixel(0,31,255,0,255) && good;
    import.args[1]=1;reply=(struct DGReply){0};DGSurfaceCommand(&c,&import,&reply);
    good=reply.error==GL_INVALID_OPERATION && pixel(0,31,255,0,255) && good;
    import.args[1]=2;import.args[2]=33;reply=(struct DGReply){0};DGSurfaceCommand(&c,&import,&reply);
    good=reply.error==GL_INVALID_VALUE && pixel(0,31,255,0,255) && good;
    printf("%s GPU family crop import: orientation, exact epochs, bounds, caller state\n",good?"PASS":"FAIL");
    good=DGSelectDrawable(&c,2,64,64) && good;
    good=glGetError()==GL_NO_ERROR && good;
    printf("%s GPU region publication: offsets, preserved pixels, flip, bounds, state, atomic validation, epochs, overlaps\n",good?"PASS":"FAIL");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,p->framebuffer);glDisable(GL_SCISSOR_TEST);
    glClearColor(0.2f,0.4f,0.8f,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();
    p->resource->epoch=17;
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,1,1);
    struct DGSurfaceSnapshot *version=DGSurfaceSnapshotCreate(&c,p,17);
    struct DGSurfaceSnapshot *stale=DGSurfaceSnapshotCreate(&c,p,16);
    GLint saved_read=0,saved_draw=0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&saved_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&saved_draw);
    good=version && !stale && glIsEnabled(GL_SCISSOR_TEST) &&
        (GLuint)saved_read==p->framebuffer && (GLuint)saved_draw==p->framebuffer && good;
    if(stale)DGSurfaceSnapshotDestroy(stale);
    GLuint consumer_texture=0;glGenTextures(1,&consumer_texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,consumer_texture);
    GLenum copy_error=DGSurfaceSnapshotCopyTexture(&c,version,GL_RGBA8,64,64);
    printf("consumer copy error=%x\n",copy_error);
    good=copy_error==GL_NO_ERROR && good;
    unsigned char copied[64*64*4]={0};
    GLuint verification=0;glGenFramebuffersEXT(1,&verification);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,verification);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,consumer_texture,0);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadPixels(0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,copied);
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)saved_read);glDeleteFramebuffersEXT(1,&verification);
    printf("consumer first pixel=%u,%u,%u,%u error=%x\n",copied[0],copied[1],copied[2],copied[3],glGetError());
    for(unsigned i=0;i<64*64;++i)good=copied[i*4]==51 && copied[i*4+1]==102 && copied[i*4+2]==204 && copied[i*4+3]==255 && good;
    printf("consumer pixels valid=%d\n",good);
    for(unsigned i=0;i<64*64;++i)if(copied[i*4]!=51 || copied[i*4+1]!=102 || copied[i*4+2]!=204 || copied[i*4+3]!=255) {
        printf("first mismatch index=%u rgba=%u,%u,%u,%u\n",i,copied[i*4],copied[i*4+1],copied[i*4+2],copied[i*4+3]);break;
    }
    // Editing the consumer must not mutate the sealed producer version.
    memset(copied,0,sizeof(copied));glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,64,64,GL_RGBA,GL_UNSIGNED_BYTE,copied);
    /* Diagnose the real driver's storage transition, independently of guest
     * GLI bookkeeping. An imported CGS texture must retain IOSurface semantics. */
    GLenum edit_error=glGetError();
    CGLError rebind_error=CGLTexImageIOSurface2D(c.cgl,GL_TEXTURE_RECTANGLE_ARB,
        GL_RGBA8,64,64,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,p->resource->surface,0);
    GLenum rebind_gl_error=glGetError();
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    GLenum redefine_error=glGetError();
    printf("storage transition: consumer edit=%x IOSurface rebind=%d GL=%x redefine=%x\n",
        edit_error,rebind_error,rebind_gl_error,redefine_error);
    good=!edit_error && !rebind_error && !rebind_gl_error && redefine_error==GL_INVALID_OPERATION && good;
    glDeleteTextures(1,&consumer_texture);
    glDisable(GL_SCISSOR_TEST);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();
    DGDestroySurfaceLeases(&c);DGDestroyDrawables(&c);DGDestroyAliases(&c);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    // Verification read only: snapshot creation itself never CPU-locks pixels.
    if(version && IOSurfaceLock(version->storage,kIOSurfaceLockReadOnly,NULL)==kIOReturnSuccess) {
        const unsigned char *base=IOSurfaceGetBaseAddress(version->storage);
        size_t pitch=IOSurfaceGetBytesPerRow(version->storage);
        good=base && version->epoch==17 && good;
        unsigned mismatches=0;
        if(base)for(int y=0;y<version->height;++y)for(int x=0;x<version->width;++x) {
            const unsigned char *q=base+y*pitch+x*4;
            if(q[0]!=204 || q[1]!=102 || q[2]!=51 || q[3]!=255)++mismatches;
            good=q[0]==204 && q[1]==102 && q[2]==51 && q[3]==255 && good;
        }
        printf("snapshot mismatches=%u\n",mismatches);
        good=IOSurfaceUnlock(version->storage,kIOSurfaceLockReadOnly,NULL)==kIOReturnSuccess && good;
    } else good=0;
    DGSurfaceSnapshotDestroy(version);DGSnapshotCleanupDrain();good=!dg_snapshot_bytes && good;
    printf("%s immutable GPU version: full pixels, state preservation, stale epoch rejection, producer overwrite/destruction, budget cleanup\n",good?"PASS":"FAIL");
    return !good;
}
