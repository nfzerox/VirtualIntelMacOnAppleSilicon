/* A 64-byte transfer with a >64 MiB pixel-store prefix. Skipped storage is
 * deliberately inaccessible; native GL must not read or write that prefix. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
static void store(int pack,GLint row,GLint alignment,GLint skiprows,GLint skippixels,GLint rowbytes)
{
    glPixelStorei(pack?GL_PACK_ROW_LENGTH:GL_UNPACK_ROW_LENGTH,row);
    glPixelStorei(pack?GL_PACK_ALIGNMENT:GL_UNPACK_ALIGNMENT,alignment);
    glPixelStorei(pack?GL_PACK_SKIP_ROWS:GL_UNPACK_SKIP_ROWS,skiprows);
    glPixelStorei(pack?GL_PACK_SKIP_PIXELS:GL_UNPACK_SKIP_PIXELS,skippixels);
    glPixelStorei(pack?GL_PACK_ROW_BYTES_APPLE:GL_UNPACK_ROW_BYTES_APPLE,rowbytes);
}
static void bitmap(unsigned char *p,size_t prefix,GLuint texture)
{
    glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,3,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glViewport(0,0,16,3);
    GLuint stencil=0;glGenRenderbuffers(1,&stencil);glBindRenderbuffer(GL_RENDERBUFFER,stencil);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8_EXT,16,3);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,stencil);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,stencil);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"bitmap stencil framebuffer");
    glClearStencil(1);glClear(GL_STENCIL_BUFFER_BIT);
    for(int lsb=0;lsb<2;++lsb){
        unsigned char tight[32];memset(tight,0xa5,sizeof(tight));store(1,0,1,0,0,0);
        glPixelStorei(GL_PACK_LSB_FIRST,lsb);glReadPixels(0,0,10,3,GL_STENCIL_INDEX,GL_BITMAP,tight);
        printf("tight bitmap lsb=%d error=%x bytes=",lsb,glGetError());
        for(unsigned i=0;i<sizeof(tight);++i)printf("%02x",tight[i]);puts("");
        memset(p+prefix,0xa5,64);glPixelStorei(GL_PACK_LSB_FIRST,lsb);
        store(1,17,8,2,(GLint)(prefix*8+3),5);
        glReadPixels(0,0,10,3,GL_STENCIL_INDEX,GL_BITMAP,p);GLenum error=glGetError();
        printf("bitmap read lsb=%d error=%x bytes=%x,%x\n",lsb,error,p[prefix+10],p[prefix+11]);
        unsigned char expected_bytes[64];memset(expected_bytes,0xa5,sizeof(expected_bytes));
        for(unsigned row=0;row<3;++row){expected_bytes[10+row*5]=lsb?0xfd:0xbf;expected_bytes[11+row*5]=lsb?0xbf:0xfd;}
        check(!error&&!memcmp(p+prefix,expected_bytes,64),
            "bitmap read preserves first/last partial bytes");
        store(1,0,1,0,0,0);memset(p+prefix,0xa5,64);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);
        for(unsigned row=0;row<3;++row){p[prefix+10+row*5]=(unsigned char)(0xa5^(row*0x33));
            p[prefix+11+row*5]=(unsigned char)(0x5a+row*31);}
        glPixelStorei(GL_UNPACK_LSB_FIRST,lsb);store(0,17,8,2,(GLint)(prefix*8+3),5);
        glRasterPos2f(-1,-1);glDrawPixels(10,3,GL_STENCIL_INDEX,GL_BITMAP,p);error=glGetError();
        check(!error,"bitmap draw accepts protected prefix and bit skip");
        unsigned char actual[30]={0};glReadPixels(0,0,10,3,GL_STENCIL_INDEX,GL_UNSIGNED_BYTE,actual);
        int good=!glGetError();for(unsigned row=0;row<3;++row)for(unsigned i=0;i<10;++i){unsigned bit=i+3;
            unsigned char value=p[prefix+10+row*5+bit/8];
            unsigned char expected=(unsigned char)((value>>(lsb?bit%8:7-bit%8))&1u);if(actual[row*10+i]!=expected)good=0;}
        check(good,"bitmap draw uses native bit order");store(0,0,1,0,0,0);
        glClearStencil(1);glClear(GL_STENCIL_BUFFER_BIT);
    }
    glPixelStorei(GL_PACK_LSB_FIRST,0);glPixelStorei(GL_UNPACK_LSB_FIRST,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_STENCIL_ATTACHMENT,GL_RENDERBUFFER,0);
    glDeleteRenderbuffers(1,&stencil);
}
static void large(GLuint texture,int core)
{
    const int width=4097,height=4096;size_t row=(size_t)width*4;
    size_t stride=(((size_t)(width+3)*4+7)/8)*8;
    size_t extent=stride+(size_t)(height-1)*stride+4+row;
    unsigned char *actual=malloc(extent+16),*expected=malloc(extent+16);
    check(actual&&expected,"large raster allocations");if(!actual||!expected){free(actual);free(expected);return;}
    glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"large raster framebuffer");
    glViewport(0,0,width,height);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    memset(actual,0xa5,extent+16);memset(expected,0xa5,extent+16);
    for(int y=0;y<height;++y){uint32_t *out=(void *)(expected+stride+(size_t)y*stride+4);
        for(int x=0;x<width;++x)out[x]=0xff0000ffu;}
    store(1,width+3,8,1,1,0);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    check(!glGetError()&&!memcmp(actual,expected,extent+16),"large strided read preserves every pixel and gap");
    GLint state=0;glGetIntegerv(GL_PACK_ROW_LENGTH,&state);
    check(state==width+3&&!glGetError(),"large read restores pack state");store(1,0,1,0,0,0);
    if(!core){
        store(0,width+3,8,1,1,0);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glRasterPos2f(-1,-1);
        glDrawPixels(width,height,GL_RGBA,GL_UNSIGNED_BYTE,expected);
        check(!glGetError(),"large strided draw accepted");store(0,0,1,0,0,0);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,actual);
        int good=!glGetError();for(int y=0;y<height&&good;++y)
            good=!memcmp(actual+(size_t)y*row,expected+stride+(size_t)y*stride+4,row);
        check(good,"large draw renders every pixel");
    }
    free(actual);free(expected);
}
int main(int argc,char **argv)
{
    int core=argc==2&&!strcmp(argv[1],"--core");if(argc>1&&!core)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(core?kCGLOGLPVersion_GL4_Core:kCGLOGLPVersion_Legacy),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;
    CGLDestroyPixelFormat(pf);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"framebuffer complete");
    glViewport(0,0,16,1);glClearColor(1,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    size_t prefix=64u*1024u*1024u,page=(size_t)getpagesize();
    unsigned char *p=mmap(NULL,prefix+page,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(p==MAP_FAILED||mprotect(p+prefix,page,PROT_READ|PROT_WRITE))return 2;
    unsigned char expected[64];for(unsigned i=0;i<64;i+=4){expected[i]=255;expected[i+1]=0;expected[i+2]=0;expected[i+3]=255;}
    memset(p+prefix,0xa5,64);glPixelStorei(GL_PACK_SKIP_PIXELS,(GLint)(prefix/4));
    glReadPixels(0,0,16,1,GL_RGBA,GL_UNSIGNED_BYTE,p);GLenum error=glGetError();
    printf("guarded ReadPixels error=%x\n",error);
    check(!error&&!memcmp(p+prefix,expected,64),"guarded-prefix ReadPixels bytes");
    GLint state=0;glGetIntegerv(GL_PACK_SKIP_PIXELS,&state);
    check(state==(GLint)(prefix/4)&&!glGetError(),"guarded read preserves pack skip");
    glEnable(0xffffffffu);glReadPixels(0,0,16,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    check(glGetError()==GL_INVALID_ENUM&&!glGetError()&&!memcmp(p+prefix,expected,64),"guarded read preserves prior error and data");
    glReadBuffer(GL_NONE);memset(p+prefix,0xa5,64);glReadPixels(0,0,16,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
    int untouched=1;for(unsigned i=0;i<64;++i)if(p[prefix+i]!=0xa5)untouched=0;
    check(glGetError()==GL_INVALID_OPERATION&&!glGetError()&&untouched,"failed guarded read leaves output untouched");
    glReadBuffer(GL_COLOR_ATTACHMENT0);glPixelStorei(GL_PACK_SKIP_PIXELS,(GLint)(prefix/2));glPixelStorei(GL_PACK_SWAP_BYTES,1);
    glReadPixels(0,0,16,1,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,p);
    int swapped=!glGetError();for(unsigned i=0;i<32;i+=2)if(p[prefix+i]!=0xf8||p[prefix+i+1]!=0)swapped=0;
    check(swapped,"guarded packed read preserves native byte swapping");glPixelStorei(GL_PACK_SWAP_BYTES,0);
    glPixelStorei(GL_PACK_SKIP_PIXELS,0);
    if(!core){memcpy(p+prefix,expected,64);
    glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);glRasterPos2f(-1,-1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,(GLint)(prefix/4));
    glDrawPixels(16,1,GL_RGBA,GL_UNSIGNED_BYTE,p);error=glGetError();
    printf("guarded DrawPixels error=%x\n",error);check(!error,"guarded-prefix DrawPixels accepted");
    unsigned char actual[64]={0};glReadPixels(0,0,16,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    check(!glGetError()&&!memcmp(actual,expected,64),"guarded-prefix DrawPixels rendered bytes");
    glGetIntegerv(GL_UNPACK_SKIP_PIXELS,&state);GLint binding=-1;glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);
    check(state==(GLint)(prefix/4)&&binding==0&&!glGetError(),"guarded draw restores unpack state and binding");
    glNewList(37,GL_COMPILE);glRasterPos2f(-1,-1);glDrawPixels(16,1,GL_RGBA,GL_UNSIGNED_BYTE,p);glEndList();
    check(!glGetError(),"guarded draw compiles into display list");
    memset(p+prefix,0,64);glClear(GL_COLOR_BUFFER_BIT);glCallList(37);
    glReadPixels(0,0,16,1,GL_RGBA,GL_UNSIGNED_BYTE,actual);
    check(!glGetError()&&!memcmp(actual,expected,64),"display list retains original pixel bytes");glDeleteLists(37,1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);bitmap(p,prefix,texture);}
    munmap(p,prefix+page);large(texture,core);
    glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    printf("guarded-raster failures=%u\n",failures);return failures?1:0;
}
