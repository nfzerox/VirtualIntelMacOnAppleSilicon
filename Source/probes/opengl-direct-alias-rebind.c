/* Same production executor, actual Intel pixels/lifetime, no guest UI.
 * Completed native binding cost is NOT animation FPS. Repeated imports are
 * compared on the same drawable and context; both keep producer flush and
 * consumer rebind. Run on current Intel AND the physical Mavericks Intel.
 */
#define VIM_ALIAS_REBIND_EXPERIMENT 1
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
#include <mach/mach_time.h>

static double elapsed(uint64_t start)
{
    mach_timebase_info_data_t t;mach_timebase_info(&t);
    return (double)(mach_absolute_time()-start)*t.numer/t.denom/1000;
}
static void bind_view(struct DGHostContext *c,struct DGMessage *m)
{
    struct DGReply r={0};assert(!DGBindDrawableTexture(c,m,&r)&&!r.error);
    assert(!c->error&&!glGetError());
}
static void pixel(GLuint texture,unsigned red,unsigned green,unsigned blue)
{
    GLint saved=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&saved);
    GLuint f=0;glGenFramebuffersEXT(1,&f);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,f);
    glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);unsigned char p[4]={0};
    glReadPixels(16,16,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    if(p[0]!=red||p[1]!=green||p[2]!=blue||p[3]!=255){
        fprintf(stderr,"pixel=%u,%u,%u,%u wanted=%u,%u,%u,255\n",p[0],p[1],p[2],p[3],red,green,blue);abort();
    }
    glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,(GLuint)saved);glDeleteFramebuffersEXT(1,&f);
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint n=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&n)&&pf&&!CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);assert(!CGLSetCurrentContext(c.cgl));
    printf("renderer=%s\n",glGetString(GL_RENDERER));assert(strstr((const char *)glGetString(GL_RENDERER),"Intel"));
    c.profile=kCGLOGLPVersion_Legacy;c.draw_mode=c.read_mode=GL_FRONT;
    assert(DGCreateAliases(&c,NULL)&&DGSelectDrawable(&c,1,129,65));
    glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);DGMarkDrawableDamage(&c);
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    struct DGMessage m={.op=DG_BIND_DRAWABLE_TEXTURE,.args={GL_RGBA8,129,65,0}};
    bind_view(&c,&m);pixel(texture,255,0,0);
    uint64_t hits=dg_drawable_alias_reuses,imports=dg_drawable_alias_imports;
    bind_view(&c,&m);assert(dg_drawable_alias_reuses==hits+1&&dg_drawable_alias_imports==imports);
    glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);DGMarkDrawableDamage(&c);
    bind_view(&c,&m);pixel(texture,0,255,0);
    // Error ordering and actual read/draw/texture bindings must survive hits.
    glEnable(0xffffffff);struct DGReply r={0};assert(!DGBindDrawableTexture(&c,&m,&r)&&!r.error);
    assert(c.error==GL_INVALID_ENUM);c.error=0;assert(!glGetError());
    GLint read=0,draw=0,bound=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING_EXT,&read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&draw);glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE_ARB,&bound);
    assert((GLuint)read==c.framebuffer&&read==draw&&(GLuint)bound==texture);
    // Public redefinition uses the exact production helpers. Native imported
    // storage may reject TexImage: preserve that failure and its live alias.
    GLuint redefining=DGBeginAliasRedefinition(&c,GL_TEXTURE_RECTANGLE_ARB,0);assert(redefining==texture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,129,65,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
    DGEndAliasRedefinition(&c,redefining);
    printf("native imported texture redefinition error=%x\n",c.error);
    assert(!c.error||c.error==GL_INVALID_OPERATION);
    int rejected=c.error!=0;c.error=0;assert(!!c.alias_group->aliases==rejected);
    imports=dg_drawable_alias_imports;bind_view(&c,&m);
    assert(dg_drawable_alias_imports==imports+(rejected?0:1));pixel(texture,0,255,0);
    // An opaque list may replace storage behind the same texture/name/shape.
    GLuint list=glGenLists(1);assert(list);glNewList(list,GL_COMPILE);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,129,65,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
    glEndList();assert(!glGetError());
    struct DGMessage call={.op=8,.args={list}};struct DGReply called={0};
    assert(!DGExecute(&c,&call,&called)&&!called.error); // audited SDK call_list slot
    GLenum list_error=glGetError();assert(!list_error||list_error==GL_INVALID_OPERATION);
    imports=dg_drawable_alias_imports;bind_view(&c,&m);assert(dg_drawable_alias_imports==imports+1);pixel(texture,0,255,0);
    glDeleteLists(list,1);
    // View and format changes are misses even on this same backing.
    imports=dg_drawable_alias_imports;m.args[1]=64;bind_view(&c,&m);
    m.args[0]=GL_RGB;bind_view(&c,&m);assert(dg_drawable_alias_imports==imports+2);
    m.args[0]=GL_RGBA8;m.args[1]=129;bind_view(&c,&m);
    // A shared producer's changed pixels are visible after our normal flush
    // and consumer bind. No replacement texture, image readback or CPU copy.
    CGLContextObj producer=NULL;assert(!CGLCreateContext(CGLGetPixelFormat(c.cgl),c.cgl,&producer));
    assert(!CGLSetCurrentContext(producer));GLuint f=0;glGenFramebuffersEXT(1,&f);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,f);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,c.color,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glClearColor(0,1,1,1);glClear(GL_COLOR_BUFFER_BIT);glFlush();
    assert(!CGLSetCurrentContext(c.cgl));bind_view(&c,&m);pixel(texture,0,255,255);
    assert(!CGLSetCurrentContext(producer));glDeleteFramebuffersEXT(1,&f);
    assert(!CGLSetCurrentContext(c.cgl));CGLDestroyContext(producer);
    // Bounded native cost only. Verify pixels after each completed phase.
    for(unsigned phase=0;phase<4;++phase){
        dg_drawable_alias_reuse=(phase==1||phase==2);uint64_t start=mach_absolute_time();
        for(unsigned i=0;i<256;++i)bind_view(&c,&m);
        glFinish();double us=elapsed(start);pixel(texture,0,255,255);
        printf("phase=%u reuse=%d total_us=%.2f per_bind_us=%.2f\n",phase,dg_drawable_alias_reuse,us,us/256);
    }
    dg_drawable_alias_reuse=1;
    GLuint old=IOSurfaceGetID(c.color_storage);DGDropDrawable(&c,1);
    assert(DGSelectDrawable(&c,2,129,65)&&IOSurfaceGetID(c.color_storage)!=old);
    glClearColor(0,0,1,1);glClear(GL_COLOR_BUFFER_BIT);DGMarkDrawableDamage(&c);
    pixel(texture,0,255,255); // retained old alias must not follow a new producer
    imports=dg_drawable_alias_imports;bind_view(&c,&m);assert(dg_drawable_alias_imports==imports+1);pixel(texture,0,0,255);
    glDeleteTextures(1,&texture);DGForgetAliases(&c,1,&texture);
    assert(!c.alias_group->aliases&&!glGetError()&&!c.error);
    DGDestroyDrawables(&c);DGDestroyAliases(&c);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);
    puts("PASS exact live-alias reuse, new pixels/shared producer, native errors/bindings, redefinition/list invalidation, view/format changes, backing replacement and deletion");return 0;
}
