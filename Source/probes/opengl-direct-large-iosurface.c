/* Real GPU import/readback over the packaged interpreter's in-process ABI.
 * Large pitched uploads exercise borrowed input after a queued command;
 * odd dimensions, changing CPU pixels and GPU writes must survive exactly.
 * Build with hidden wrappers and export only main, as other wire fixtures do.
 */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
#include <time.h>
enum {W=1537,H=769,Stride=W*4+28,Span=(H-1)*Stride+W*4,Tight=W*H*4};
static void pattern(unsigned char *p,unsigned x,unsigned y,unsigned phase)
{
    p[0]=(unsigned char)(x*17+y*3+phase);
    p[1]=(unsigned char)(x*5+y*11+phase*7);
    p[2]=(unsigned char)(x+y*19+phase*13);p[3]=255;
}
__attribute__((visibility("default"))) int main(int argc,char **argv)
{
    unsigned phases=argc==2?(unsigned)strtoul(argv[1],NULL,10):3;
    assert(argc<=2&&phases>=3&&phases<=1000);
    uint64_t import_ns=0;
    unsigned char shadow[16]={0};
    unsigned char *input=malloc(Span+32),*output=malloc(Tight+32),*published=malloc(Span+32);
    assert(input&&output&&published);
    struct DGContext *c=DGCreateContext(NULL,NULL);assert(c);
    assert(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,2,2));
    printf("transport=%s renderer=%s large-import=%ux%u stride=%u\n",DGTransportName(),glGetString(GL_RENDERER),W,H,Stride);
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    for(unsigned phase=0;phase<phases;++phase){
        memset(input,0xa5,Span+32);memset(output,0xa5,Tight+32);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)pattern(input+16+y*Stride+x*4,x,y,phase);
        struct timespec before,after;assert(!clock_gettime(CLOCK_MONOTONIC,&before));
        assert(DGImportIOSurfaceTexture(GL_RGBA8,W,H,input+16,Stride)==1);
        assert(!clock_gettime(CLOCK_MONOTONIC,&after));
        import_ns+=(uint64_t)(after.tv_sec-before.tv_sec)*1000000000+after.tv_nsec-before.tv_nsec;
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
        assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
        glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glReadPixels(0,0,W,H,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,output+16);
        assert(!glGetError());
        for(unsigned y=0;y<H;++y)assert(!memcmp(output+16+y*W*4,input+16+y*Stride,W*4));
        for(unsigned i=0;i<16;++i)assert(input[i]==0xa5&&input[16+Span+i]==0xa5&&output[i]==0xa5&&output[16+Tight+i]==0xa5);
    }
    glEnable(GL_SCISSOR_TEST);glScissor(0,0,W/2,H/2);glClearColor(1,0,1,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    uint32_t draw=0;unsigned count=0;struct DGIosurfaceWrite writes[2];
    assert(DGFlushIOSurfaceState(1,&draw,writes,2,&count)==1&&count==1&&writes[0].texture==texture);
    memset(published,0xa5,Span+32);
    assert(DGReadIOSurfaceTexture(texture,writes[0].serial,W,H,published+16,Stride));
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
        unsigned char expected[4];pattern(expected,x,y,phases-1);
        if(x<W/2&&y<H/2){expected[0]=255;expected[1]=0;expected[2]=255;expected[3]=255;}
        assert(!memcmp(published+16+y*Stride+x*4,expected,4));
    }
    for(unsigned i=0;i<16;++i)assert(published[i]==0xa5&&published[16+Span+i]==0xa5);
    for(unsigned y=0;y<H-1;++y)for(unsigned x=W*4;x<Stride;++x)assert(published[16+y*Stride+x]==0xa5);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);assert(!glGetError()&&!c->connection->dead);
    DGDestroyContext(c);assert(!dg_inprocess_api->live_count());
    free(input);free(output);free(published);
    printf("imports=%u mean_import_ms=%.3f\n",phases,(double)import_ns/phases/1000000);
    puts("PASS large pitched CPU replacements, actual GPU pixels/writes, reply guards, row padding and endpoint retirement");
    return 0;
}
