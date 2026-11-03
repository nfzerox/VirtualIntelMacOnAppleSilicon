/* Actual typed client/interpreter publication path, with native host GL.
 * Build hidden/export-only-main to avoid interposing on native GL dlsym.
 * No guest installation, substitute renderer, or claimed desktop FPS. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
int main(void)
{
    enum {W=17,H=19,Stride=80,Bytes=(H-1)*Stride+W*4};
    struct DGContext *c=DGCreateContext(NULL,NULL);assert(c);
    unsigned char shadow[W*H*4]={0},input[Bytes],output[Bytes];
    assert(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H));
    printf("renderer=%s transport=%s\n",glGetString(GL_RENDERER),DGTransportName());
    assert(c->connection->capabilities&DG_CAP_IOSURFACE_WRITES);
    struct DGContext *consumer=DGCreateContext(NULL,c);assert(consumer);
    assert(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H));
    memset(input,0x35,sizeof(input));memset(output,0xa5,sizeof(output));
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,13);
    assert(DGImportIOSurfaceTexture(GL_RGBA8,W,H,input,Stride)==1);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    struct DGIosurfaceWrite writes[2]={{0}};unsigned count=0;uint32_t draw=UINT32_MAX;
    assert(DGMakeCurrent(consumer,shadow,GL_UNSIGNED_BYTE,W,H));
    assert(DGFlushIOSurfaceState(0,&draw,writes,2,&count)==1&&!count);
    assert(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H));
    int flushed=DGFlushIOSurfaceState(0,&draw,writes,2,&count);
    fprintf(stderr,"flush=%d count=%u draw=%u texture=%u expected=%u error=%x\n",flushed,count,draw,writes[0].texture,texture,c->error);
    assert(flushed==1&&count==1&&!draw&&writes[0].texture==texture);
    uint64_t serial=writes[0].serial;assert(serial);
    assert(DGReadIOSurfaceTexture(texture,serial,W,H,output,Stride));
    for(unsigned y=0;y<H;++y){
        for(unsigned x=0;x<W;++x){const unsigned char *p=output+y*Stride+x*4;assert(p[0]==255&&!p[1]&&p[2]==255&&p[3]==255);}
        if(y<H-1)for(unsigned x=W*4;x<Stride;++x)assert(output[y*Stride+x]==0xa5);
    }
    GLint actual=-1,row=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING_EXT,&actual);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);
    assert(!actual&&row==13&&!glGetError());
    assert(DGFlushIOSurfaceState(1,&draw,writes,2,&count)==1&&count==1&&writes[0].serial==serial);
    assert(DGMakeCurrent(consumer,shadow,GL_UNSIGNED_BYTE,W,H));
    assert(DGFlushIOSurfaceState(0,&draw,writes,2,&count)==1&&count==1&&writes[0].serial==serial);
    /* The next writer can be a different shared context. Its unsubmitted
     * update must invalidate the earlier readable generation, but an unrelated
     * context's Flush must not claim that the new version is CPU-current. */
    GLuint consumer_fbo=0;glGenFramebuffersEXT(1,&consumer_fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,consumer_fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
    glClearColor(0,1,1,1);glClear(GL_COLOR_BUFFER_BIT);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    assert(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H));
    assert(DGFlushIOSurfaceState(0,&draw,writes,2,&count)==1&&!count);
    memset(output,0xa5,sizeof(output));assert(!DGReadIOSurfaceTexture(texture,serial,W,H,output,Stride));
    for(unsigned i=0;i<sizeof(output);++i)assert(output[i]==0xa5);
    assert(DGMakeCurrent(consumer,shadow,GL_UNSIGNED_BYTE,W,H));
    glFlush(); /* Also qualify the ordinary typed native Flush codec. */
    assert(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,W,H));
    assert(DGFlushIOSurfaceState(0,&draw,writes,2,&count)==1&&count==1&&writes[0].serial!=serial);
    serial=writes[0].serial;assert(DGReadIOSurfaceTexture(texture,serial,W,H,output,Stride));
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const unsigned char *p=output+y*Stride+x*4;
        assert(p[0]==255&&p[1]==255&&!p[2]&&p[3]==255);}
    memset(output,0xa5,sizeof(output));assert(!DGReadIOSurfaceTexture(texture,serial+1,W,H,output,Stride));
    for(unsigned i=0;i<sizeof(output);++i)assert(output[i]==0xa5);
    /* A new CPU import supersedes that old writer generation. */
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);assert(DGImportIOSurfaceTexture(GL_RGBA8,W,H,input,Stride)==1);
    assert(DGFlushIOSurfaceState(0,&draw,writes,2,&count)==1&&!count);
    assert(!DGReadIOSurfaceTexture(texture,serial,W,H,output,Stride));
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);
    assert(!glGetError()&&!c->connection->dead);
    assert(DGMakeCurrent(consumer,shadow,GL_UNSIGNED_BYTE,W,H));glDeleteFramebuffersEXT(1,&consumer_fbo);
    assert(!glGetError());DGDestroyContext(consumer);DGDestroyContext(c);
    puts("PASS typed unbound/shared writer submission, actual GPU bytes, padding/state, stale rejection and CPU supersession");
    return 0;
}
