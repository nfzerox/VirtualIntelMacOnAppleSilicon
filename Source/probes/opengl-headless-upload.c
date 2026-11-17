/* Same-boot real guest/GPU comparison. The copied control changes only this
 * fixture connection's headless fast-path selector around the upload; no
 * device/global policy, guest installation or desktop FPS claim. */
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#include <assert.h>
enum {W=1537,H=769,Stride=W*4+28,Span=(H-1)*Stride+W*4,Trials=16};
static double elapsed(uint64_t start)
{mach_timebase_info_data_t t;mach_timebase_info(&t);return (mach_absolute_time()-start)*(double)t.numer/t.denom/1e6;}
static int compare(const void *a,const void *b)
{double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);}
__attribute__((visibility("default"))) int main(void)
{
    unsigned char shadow[16]={0},*input=malloc(Span+32),*output=malloc(W*H*4);
    struct DGContext *c=DGCreateContext(NULL,NULL);assert(input&&output&&c);
    assert(DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,2,2));
    struct DGConnection *connection=c->connection;
    assert(connection->headless&&connection->row_upload_supported);
    printf("ABI=%zu transport=%s renderer=%s bytes=%u\n",sizeof(void*)*8,DGTransportName(),glGetString(GL_RENDERER),Span);
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glGenFramebuffersEXT(1,&fbo);double times[2][Trials];unsigned char last=0;
    for(int trial=-2;trial<Trials;++trial)for(unsigned pass=0;pass<2;++pass){
        unsigned mode=(pass+(unsigned)(trial+2))%2;
        last=(unsigned char)(13+(trial+2)*2+mode);memset(input,0xa5,Span+32);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            unsigned char *p=input+16+y*Stride+x*4;p[0]=last;p[1]=73;p[2]=11;p[3]=255;
        }
        uint64_t before_bytes=connection->direct_write_bytes;
        connection->headless=mode;
        uint64_t start=mach_absolute_time();
        int okay=DGImportIOSurfaceTexture(GL_RGBA8,W,H,input+16,Stride);
        double duration=elapsed(start);connection->headless=1;
        assert(okay==1&&!connection->dead);
        assert(connection->direct_write_bytes-before_bytes==(mode?Span:0));
        if(trial>=0)times[mode][trial]=duration;
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
        assert(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT);
        glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
        for(unsigned corner=0;corner<4;++corner){
            unsigned char pixel[4]={0};glReadPixels(corner&1?W-1:0,corner&2?H-1:0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            assert(pixel[0]==11&&pixel[1]==73&&pixel[2]==last&&pixel[3]==255);
        }
        assert(!glGetError());
        for(unsigned i=0;i<16;++i)assert(input[i]==0xa5&&input[16+Span+i]==0xa5);
        for(unsigned y=0;y<H-1;++y)for(unsigned x=W*4;x<Stride;++x)assert(input[16+y*Stride+x]==0xa5);
    }
    glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,output);
    for(unsigned i=0;i<W*H;++i)assert(output[i*4]==11&&output[i*4+1]==73&&output[i*4+2]==last&&output[i*4+3]==255);
    assert(!glGetError());
    for(unsigned mode=0;mode<2;++mode){
        qsort(times[mode],Trials,sizeof(double),compare);
        printf("%s samples=%u median_ms=%.3f p95_ms=%.3f\n",mode?"prepared":"copied",Trials,times[mode][Trials/2],times[mode][Trials-1]);
    }
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&texture);DGDestroyContext(c);free(input);free(output);
    puts("PASS changing GPU pixels, pitched source guards, prepared bytes and connection retirement");return 0;
}
