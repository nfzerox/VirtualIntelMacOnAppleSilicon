/* Polygon stipple readback: skipped storage is inaccessible, not payload. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void pack(GLint skip,int lsb,int explicit_stride)
{
    glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,41);
    glPixelStorei(GL_PACK_SKIP_ROWS,2);glPixelStorei(GL_PACK_SKIP_PIXELS,skip);
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,explicit_stride?7:0);glPixelStorei(GL_PACK_LSB_FIRST,lsb);
}
static void expected(unsigned char *out,const unsigned char *bits,int lsb,unsigned stride,unsigned shift)
{
    memset(out,0xa5,512);
    for(unsigned row=0;row<32;++row)for(unsigned x=0;x<32;++x){
        unsigned source=(bits[row*4+x/8]>>(7-x%8))&1u,destination=x+shift;
        unsigned mask=1u<<(lsb?destination%8:7-destination%8);
        unsigned char *p=out+2*stride+row*stride+destination/8;
        *p=(unsigned char)((*p&~mask)|(source?mask:0));
    }
}
int main(int argc,char **argv)
{
    int ordinary=argc==2&&!strcmp(argv[1],"--ordinary-only");
    if(argc>1&&!ordinary)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"stipple error-control framebuffer");
    /* Also accommodate a native driver ignoring ROW_BYTES_APPLE and using
     * the aligned eight-byte stride, so observing that defect stays safe. */
    unsigned char bits[128],tight[128],want[512],actual[512];
    for(unsigned i=0;i<sizeof(bits);++i)bits[i]=(unsigned char)(13+i*37);
    glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPolygonStipple(bits);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glGetPolygonStipple(tight);
    check(!glGetError()&&!memcmp(tight,bits,sizeof(bits)),"tight stipple source control");
    size_t prefix=ordinary?0:64u*1024u*1024u,page=(size_t)getpagesize();
    printf("protected-prefix=%lu\n",(unsigned long)prefix);
    unsigned char *guard=mmap(NULL,prefix+page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(guard==MAP_FAILED||mprotect(guard+prefix,page,PROT_READ|PROT_WRITE))return 2;
    for(int shift=0;shift<=3;shift+=3)for(int explicit_stride=0;explicit_stride<2;++explicit_stride)for(int lsb=0;lsb<2;++lsb){
        expected(want,bits,lsb,explicit_stride?7:8,(unsigned)shift);
        memset(actual,0xa5,sizeof(actual));pack(shift,lsb,explicit_stride);
        glGetPolygonStipple(actual);
        printf("ordinary lsb=%d explicit-stride=%d bit-shift=%d bytes=",lsb,explicit_stride,shift);
        for(unsigned i=0;i<32;++i)printf("%02x",actual[i]);puts("");
        check(!glGetError()&&!memcmp(actual,want,sizeof(actual)),"ordinary stipple read bit skip and padding");
        memset(guard+prefix,0xa5,512);pack((GLint)(prefix*8+shift),lsb,explicit_stride);
        glGetPolygonStipple(guard);
        check(!glGetError()&&!memcmp(guard+prefix,want,512),"guarded stipple read bit skip and padding");
        GLint skip=0,binding=-1,row=0,alignment=0;
        glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip);glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);
        glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&row);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
        check(skip==(GLint)(prefix*8+shift)&&binding==0&&row==(explicit_stride?7:0)&&alignment==8&&!glGetError(),"stipple read restores native pack state");
        memset(guard+prefix,0xa5,512);glEnable(0xffffffffu);glGetPolygonStipple(guard);
        check(glGetError()==GL_INVALID_ENUM&&!glGetError()&&!memcmp(guard+prefix,want,512),"stipple read retains prior error and pixels");
        glNewList(27,GL_COMPILE);memset(guard+prefix,0xa5,512);glGetPolygonStipple(guard);glEndList();
        check(!glGetError()&&!memcmp(guard+prefix,want,512),"stipple read executes during display-list compilation");
        glDeleteLists(27,1);
        glBegin(GL_POINTS);glGetPolygonStipple(guard);glEnd();
        check(glGetError()==GL_INVALID_OPERATION&&!glGetError(),"stipple read rejects Begin-End");
    }
    munmap(guard,prefix+page);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("guarded-stipple-read failures=%u\n",failures);return failures?1:0;
}
