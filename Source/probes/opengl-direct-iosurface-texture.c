/* Exercise the actual native host import, not a reimplementation of it. */
#define main DGExecutorMain
#include "../PassthroughOpenGL/passthrough-opengl-host.c"
#undef main
#include <assert.h>
static void checkPixel(unsigned alpha)
{
    unsigned char p[4]={0};glReadPixels(17,23,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    assert(!glGetError() && p[0]==11 && p[1]==73 && p[2]==191 && p[3]==alpha);
}
static void checkComponents(struct DGHostContext *c,unsigned elements)
{
    enum {W=184,H=240};unsigned Stride=192*elements,Bytes=(H-1)*Stride+W*elements;
    struct DGMessage *m=calloc(1,sizeof(*m)+Bytes);assert(m);
    m->flags=DG_REPLY|DG_INPUT_ONLY;m->bytes=Bytes;
    m->args[0]=elements==1?GL_LUMINANCE8:GL_LUMINANCE8_ALPHA8;m->args[1]=W;m->args[2]=H;m->args[3]=Stride;
    unsigned char *pixels=(void *)(m+1);memset(pixels,0xa5,Bytes);
    GLuint source=0,output=0,fbo=0;glGenTextures(1,&source);glGenTextures(1,&output);glGenFramebuffersEXT(1,&fbo);
    glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    IOSurfaceRef identity=NULL;
    for(unsigned round=0;round<3;++round){
        unsigned char level=(unsigned char)(37+round*73);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            pixels[y*Stride+x*elements]=level;
            if(elements==2)pixels[y*Stride+x*elements+1]=83;
        }
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        struct DGReply reply={0};assert(!DGIOSurfaceTexture(c,m,&reply)&&reply.result==1&&!reply.error);
        IOSurfaceRef surface=c->alias_group->aliases->owned_storage;
        assert(surface&&IOSurfaceGetBytesPerElement(surface)==elements);
        if(identity)assert(identity==surface);identity=surface;
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
        assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
        glViewport(0,0,W,H);glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glBegin(GL_QUADS);
        glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(W,0);glVertex2f(1,-1);
        glTexCoord2i(W,H);glVertex2f(1,1);glTexCoord2i(0,H);glVertex2f(-1,1);glEnd();
        unsigned char result[4]={0};glReadPixels(17,23,1,1,GL_RGBA,GL_UNSIGNED_BYTE,result);
        assert(!glGetError()&&result[0]==level&&result[1]==level&&result[2]==level&&result[3]==(elements==1?255:83));
        for(unsigned y=0;y<H-1;++y)for(unsigned x=W*elements;x<Stride;++x)assert(pixels[y*Stride+x]==0xa5);
    }
    struct DGReply bad={0};--m->bytes;assert(DGIOSurfaceTexture(c,m,&bad)==-1);++m->bytes;
    assert(c->alias_group->aliases->owned_storage==identity);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,source,0);
    if(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT){
        glClearColor(1,1,1,1);glClear(GL_COLOR_BUFFER_BIT);c->iosurface_draw_dirty=1;DGMarkIOSurfaceWrites(c);
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
        struct {struct DGMessage m;struct DGIosurfaceWrite record;} pending={0};
        pending.m.flags=DG_REPLY|DG_OUTPUT_ONLY;pending.m.bytes=sizeof(pending.record);
        pending.m.args[1]=1;pending.m.args[2]=1;
        struct DGReply observed={0};assert(!DGFlushIOSurfaceState(c,&pending.m,&observed)&&!observed.error);
        assert((observed.result>>32)==1&&pending.record.texture==source&&pending.record.serial);
        struct DGMessage *read=calloc(1,sizeof(*read)+Bytes);assert(read);
        read->flags=DG_REPLY;read->bytes=Bytes;read->args[0]=source;read->args[1]=pending.record.serial;
        read->args[2]=W;read->args[3]=H;read->args[4]=Stride;read->args[5]=1;read->args[6]=elements;
        unsigned char *result=(void *)(read+1);memset(result,0xa5,Bytes);
        struct DGReply copied={0};assert(!DGReadIOSurfaceTexture(c,read,&copied)&&!copied.error&&copied.result==1);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W*elements;++x)assert(result[y*Stride+x]==255);
        for(unsigned y=0;y<H-1;++y)for(unsigned x=W*elements;x<Stride;++x)assert(result[y*Stride+x]==0xa5);
        read->args[6]=4;assert(DGReadIOSurfaceTexture(c,read,&copied)==-1);free(read);
        printf("PASS %u-component GPU writer publication and padded readback\n",elements);
    }else printf("Native %u-component attachment is not framebuffer-renderable; no GPU-write claim for that format\n",elements);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteTextures(1,&source);DGForgetAliases(c,1,&source);
    glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fbo);free(m);
    assert(!glGetError());printf("PASS native %u-component sampling, pitched CPU updates, padding, reuse and malformed input rejection\n",elements);
}
static void checkPacked(struct DGHostContext *c)
{
    enum {W=4,H=2,Stride=16,Bytes=(H-1)*Stride+W*2};
    struct DGMessage *m=calloc(1,sizeof(*m)+Bytes);assert(m);
    m->op=DG_IOSURFACE_TEXTURE_PACKED;m->flags=DG_REPLY|DG_INPUT_ONLY;m->bytes=Bytes;
    m->args[0]=GL_RGB;m->args[1]=W;m->args[2]=H;m->args[3]=Stride;
    assert(DGHeaderValid(m));
    GLuint source=0,output=0,fbo=0;glGenTextures(1,&source);glGenTextures(1,&output);glGenFramebuffersEXT(1,&fbo);
    glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    unsigned char *pixels=(void *)(m+1);
    for(unsigned raw=0;raw<2;++raw)for(unsigned reverse=0;reverse<2;++reverse){
        m->args[6]=raw?0x8a1f:GL_YCBCR_422_APPLE;
        m->args[7]=reverse?GL_UNSIGNED_SHORT_8_8_REV_APPLE:GL_UNSIGNED_SHORT_8_8_APPLE;
        IOSurfaceRef identity=NULL;
        for(unsigned round=0;round<2;++round){
            const unsigned char levels[]={16,235,81,145};memset(pixels,0xa5,Bytes);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                pixels[y*Stride+x*2+(reverse?0:1)]=levels[(x+round)%W];
                pixels[y*Stride+x*2+(reverse?1:0)]=raw?(x&1?192:80):128;
            }
            glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            struct DGReply reply={0};assert(!DGIOSurfaceTexture(c,m,&reply)&&reply.result==1&&!reply.error);
            IOSurfaceRef surface=c->alias_group->aliases->owned_storage;
            assert(surface&&IOSurfaceGetBytesPerElement(surface)==2);
            if(identity)assert(identity==surface);identity=surface;
            glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
            glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
            assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
            glViewport(0,0,W,H);glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
            glBegin(GL_QUADS);glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(W,0);glVertex2f(1,-1);
            glTexCoord2i(W,H);glVertex2f(1,1);glTexCoord2i(0,H);glVertex2f(-1,1);glEnd();
            unsigned char result[W*4]={0};glReadPixels(0,0,W,1,GL_RGBA,GL_UNSIGNED_BYTE,result);assert(!glGetError());
            const int converted[]={0,255,76,150};
            for(unsigned x=0;x<W;++x){
                for(unsigned channel=0;channel<3;++channel){
                    int expected=raw?(channel==1?levels[(x+round)%W]:channel==0?192:80):converted[(x+round)%W];
                    if(abs((int)result[x*4+channel]-expected)>5){
                        fprintf(stderr,"packed raw=%u reverse=%u round=%u x=%u channel=%u got=%u expected=%d\n",raw,reverse,round,x,channel,result[x*4+channel],expected);abort();
                    }
                }
                assert(result[x*4+3]==255);
            }
            for(unsigned x=W*2;x<Stride;++x)assert(pixels[x]==0xa5);
            struct DGReply bad={0};--m->bytes;assert(DGIOSurfaceTexture(c,m,&bad)==-1);++m->bytes;
            m->args[1]=3;assert(DGIOSurfaceTexture(c,m,&bad)==-1);m->args[1]=W;
            uint64_t type=m->args[7];m->args[7]=GL_UNSIGNED_BYTE;assert(DGIOSurfaceTexture(c,m,&bad)==-1);m->args[7]=type;
            assert(c->alias_group->aliases->owned_storage==identity);
        }
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteTextures(1,&source);DGForgetAliases(c,1,&source);
    glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fbo);free(m);
    assert(!glGetError());puts("PASS native YCbCr422 and raw RGB422, both byte orders, pitched updates, retained storage and malformed input rejection");
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;GLint count=0;struct DGHostContext c={0};
    assert(!CGLChoosePixelFormat(attrs,&pf,&count) && pf && !CGLCreateContext(pf,NULL,&c.cgl));
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c.cgl);c.profile=kCGLOGLPVersion_Legacy;
    printf("renderer=%s\n",glGetString(GL_RENDERER));
    assert(DGCreateAliases(&c,NULL));
    enum {W=64,H=64,Stride=280,Bytes=(H-1)*Stride+W*4};
    struct DGMessage *m=calloc(1,sizeof(*m)+Bytes);assert(m);
    m->op=DG_IOSURFACE_TEXTURE;m->flags=DG_REPLY|DG_INPUT_ONLY;m->bytes=Bytes;
    m->args[1]=W;m->args[2]=H;m->args[3]=Stride;
    unsigned char *pixels=(void *)(m+1);memset(pixels,0xa5,Bytes);
    GLuint source=0,output=0,fbo=0;glGenTextures(1,&source);glGenTextures(1,&output);glGenFramebuffersEXT(1,&fbo);
    glBindTexture(GL_TEXTURE_2D,output);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    const GLenum formats[]={GL_RGB,GL_RGB8,GL_RGBA,GL_RGBA8};
    IOSurfaceRef identity=NULL;
    for(unsigned format=0;format<4;++format)for(unsigned round=0;round<3;++round){
        unsigned alpha=37+round*41;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            unsigned char *p=pixels+y*Stride+x*4;p[0]=191;p[1]=73;p[2]=11;p[3]=alpha;
        }
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);m->args[0]=formats[format];
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,7);glPixelTransferf(GL_ALPHA_SCALE,.25f);
        struct DGReply reply={0};assert(!DGIOSurfaceTexture(&c,m,&reply) && reply.result==1 && !reply.error);
        GLint row=0;GLfloat scale=0;glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);glGetFloatv(GL_ALPHA_SCALE,&scale);
        assert(row==7 && scale==.25f);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelTransferf(GL_ALPHA_SCALE,1);
        assert(c.alias_group->owned_surface_count==1 && c.alias_group->aliases->owned_storage);
        if(identity)assert(identity==c.alias_group->aliases->owned_storage);
        identity=c.alias_group->aliases->owned_storage;
        GLint internal=0,bits=0;glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_INTERNAL_FORMAT,&internal);
        glGetTexLevelParameteriv(GL_TEXTURE_RECTANGLE_ARB,0,GL_TEXTURE_ALPHA_SIZE,&bits);
        assert(internal==(format<2?GL_RGB8:GL_RGBA8) && bits==(format<2?0:8));
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
        assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
        glViewport(0,0,W,H);glEnable(GL_TEXTURE_RECTANGLE_ARB);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
        glBegin(GL_QUADS);
        glTexCoord2i(0,0);glVertex2f(-1,-1);glTexCoord2i(W,0);glVertex2f(1,-1);
        glTexCoord2i(W,H);glVertex2f(1,1);glTexCoord2i(0,H);glVertex2f(-1,1);glEnd();
        checkPixel(format<2?255:alpha);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,source,0);
        checkPixel(alpha); // The native RGB sampling/attachment distinction.
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)assert(pixels[y*Stride+x*4+3]==alpha);
        for(unsigned y=0;y<H-1;++y)for(unsigned x=W*4;x<Stride;++x)assert(pixels[y*Stride+x]==0xa5);
    }
    /* Actual writer metadata survives a later FBO-zero bind. This fixture
     * invokes native GL directly, so explicitly invalidate its attachment
     * cache just as the generated typed bind/attachment wrappers do. */
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);c.iosurface_draw_dirty=1;
    glClearColor(1,1,0,1);glClear(GL_COLOR_BUFFER_BIT);DGMarkIOSurfaceWrites(&c);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);c.iosurface_draw_dirty=1;
    struct {struct DGMessage m;struct DGIosurfaceWrite record;} pending={0};
    pending.m.flags=DG_REPLY|DG_OUTPUT_ONLY;pending.m.bytes=sizeof(pending.record);
    pending.m.args[1]=1;pending.m.args[2]=1;
    struct DGReply observed={0};assert(!DGFlushIOSurfaceState(&c,&pending.m,&observed)&&!observed.error);
    assert((observed.result>>32)==1&&!(uint32_t)observed.result&&pending.record.texture==source&&pending.record.serial);
    uint64_t serial=pending.record.serial;
    struct DGMessage *read=calloc(1,sizeof(*read)+Bytes);assert(read);
    read->flags=DG_REPLY;read->bytes=Bytes;read->args[0]=source;read->args[1]=serial;
    read->args[2]=W;read->args[3]=H;read->args[4]=Stride;read->args[5]=1;
    unsigned char *result=(void *)(read+1);memset(result,0xa5,Bytes);
    struct DGReply copied={0};assert(!DGReadIOSurfaceTexture(&c,read,&copied)&&!copied.error&&copied.result==1);
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        const unsigned char *p=result+y*Stride+x*4;assert(!p[0]&&p[1]==255&&p[2]==255&&p[3]==255);
    }
    for(unsigned y=0;y<H-1;++y)for(unsigned x=W*4;x<Stride;++x)assert(result[y*Stride+x]==0xa5);
    GLint bound=-1;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&bound);assert(!bound);
    observed=(struct DGReply){0};assert(!DGFlushIOSurfaceState(&c,&pending.m,&observed)&&pending.record.serial==serial);
    read->args[1]=serial+1;memset(result,0xa5,Bytes);copied=(struct DGReply){0};
    assert(!DGReadIOSurfaceTexture(&c,read,&copied)&&!copied.error&&!copied.result);
    for(unsigned i=0;i<Bytes;++i)assert(result[i]==0xa5);
    free(read);puts("PASS unbound IOSurface writer serial, native CPU ownership, padded read and stale observation rejection");
    // Malformed updates must not replace or release the valid backing.
    struct DGReply bad={0};--m->bytes;assert(DGIOSurfaceTexture(&c,m,&bad)==-1);++m->bytes;
    assert(c.alias_group->aliases->owned_storage==identity && c.alias_group->owned_surface_count==1);
    // Native IOSurface texture storage rejects ordinary glTexImage2D.
    // A failed public redefinition MUST retain the live allocation.
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,source);GLuint redefining=DGBeginAliasRedefinition(&c,GL_TEXTURE_RECTANGLE_ARB,0);
    assert(redefining==source);glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,W,H,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
    DGEndAliasRedefinition(&c,redefining);
    assert(c.error==GL_INVALID_OPERATION && c.alias_group->aliases->owned_storage==identity);
    c.error=0;
    glDeleteTextures(1,&source);DGForgetAliases(&c,1,&source);
    assert(!c.alias_group->aliases && !c.alias_group->owned_surface_bytes && !c.alias_group->owned_surface_count&&
        !c.alias_group->iosurface_import_count);
    glDeleteTextures(1,&output);glDeleteFramebuffersEXT(1,&fbo);
    checkComponents(&c,1);checkComponents(&c,2);checkPacked(&c);
    assert(!glGetError());DGDestroyAliases(&c);CGLSetCurrentContext(NULL);CGLDestroyContext(c.cgl);free(m);
    puts("PASS native RGB/RGBA sampling, raw attachment alpha, pitched CPU updates, state/padding, reuse and redefinition lifetime");return 0;
}
