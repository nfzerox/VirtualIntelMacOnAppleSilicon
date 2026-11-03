/* Native Intel / real guest CGL fixture for the compositor's 200-byte wave
 * LUT contract. Also covers subimages and PBO offsets. Guard pages bound
 * the entire native row/pixel-skip prefix plus the final visible texels.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static int failures;
static void VIMCheck(int good,const char *name)
{fprintf(stderr,"%s %s error=%x\n",good?"PASS":"FAIL",name,glGetError());failures+=!good;}
static void verify(const unsigned char *expected,unsigned width,const char *name)
{
    unsigned char output[200*4];memset(output,0xa5,sizeof(output));
    glViewport(0,0,width,1);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glEnable(GL_TEXTURE_1D);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glBegin(GL_QUADS);glTexCoord1f(0);glVertex2f(-1,-1);glTexCoord1f(1);glVertex2f(1,-1);
    glTexCoord1f(1);glVertex2f(1,1);glTexCoord1f(0);glVertex2f(-1,1);glEnd();
    glReadPixels(0,0,width,1,GL_RGBA,GL_UNSIGNED_BYTE,output);
    GLenum error=glGetError();unsigned different=0;
    if(getenv("VIM_1D_DETAILS"))for(unsigned x=0;x<12;++x)
        fprintf(stderr,"sample[%u]=%u,%u,%u,%u expected=%u\n",x,output[x*4],output[x*4+1],output[x*4+2],output[x*4+3],expected[x]);
    for(unsigned x=0;x<width;++x)different+=output[x*4]!=expected[x] || output[x*4+1]!=expected[x] ||
        output[x*4+2]!=expected[x] || output[x*4+3]!=255;
    fprintf(stderr,"%s mismatched=%u/%u GL error=%x\n",name,different,width,error);VIMCheck(!different&&!error,name);
    glDisable(GL_TEXTURE_1D);
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf||CGLCreateContext(pf,NULL,&context)||CGLSetCurrentContext(context))return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);fprintf(stderr,"renderer=%s\n",renderer?renderer:"none");
    if(!renderer||!strstr(renderer,"Intel"))return 2;
    GLuint destination=0,fbo=0,texture=0,pbo=0;glGenTextures(1,&destination);glBindTexture(GL_TEXTURE_2D,destination);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,200,1,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,destination,0);
    VIMCheck(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT,"sample FBO complete");
    glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_1D,texture);
    glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    size_t page=(size_t)getpagesize();unsigned char *allocation=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);
    if(allocation==MAP_FAILED||mprotect(allocation+page,page,PROT_NONE))return 2;
    enum {prefix=3*256+3};
    unsigned char *pixels=allocation+page-prefix-200,expected[200];memset(pixels,0xa5,prefix+200);
    for(unsigned i=0;i<200;++i)pixels[i+prefix]=expected[i]=(i*47)%251;
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,3);glPixelStorei(GL_UNPACK_SKIP_ROWS,3);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,4096);glPixelStorei(GL_UNPACK_ROW_BYTES_APPLE,256);glPixelStorei(GL_UNPACK_ALIGNMENT,8);
    glTexImage1D(GL_TEXTURE_1D,0,GL_LUMINANCE,200,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,pixels);
    VIMCheck(glGetError()==GL_NO_ERROR,"1D wave allocation honors native row/pixel skips");verify(expected,200,"CPU wave pixels");
    for(unsigned i=0;i<13;++i)pixels[i+prefix]=expected[61+i]=31+i*9;
    glTexSubImage1D(GL_TEXTURE_1D,0,61,13,GL_LUMINANCE,GL_UNSIGNED_BYTE,pixels);
    VIMCheck(glGetError()==GL_NO_ERROR,"1D CPU subimage");verify(expected,200,"CPU subimage pixels");
    /* PBO has exactly offset + native skipped prefix + visible width. */
    unsigned char buffer[16+prefix+200];memset(buffer,0xa5,sizeof(buffer));
    for(unsigned i=0;i<200;++i)buffer[16+prefix+i]=expected[i]=255-i;
    glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(buffer),buffer,GL_STREAM_DRAW);
    glTexImage1D(GL_TEXTURE_1D,0,GL_LUMINANCE,200,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,(void *)16);
    VIMCheck(glGetError()==GL_NO_ERROR,"1D PBO allocation uses checked offset");verify(expected,200,"PBO wave pixels");
    for(unsigned i=0;i<17;++i)expected[71+i]=buffer[16+prefix+i];
    glTexSubImage1D(GL_TEXTURE_1D,0,71,17,GL_LUMINANCE,GL_UNSIGNED_BYTE,(void *)16);
    VIMCheck(glGetError()==GL_NO_ERROR,"1D PBO subimage uses checked offset");verify(expected,200,"PBO subimage pixels");
    glTexImage1D(GL_TEXTURE_1D,0,GL_LUMINANCE,200,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,(void *)17);
    VIMCheck(glGetError()==GL_INVALID_OPERATION,"out-of-bounds PBO span rejected");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glTexImage1D(GL_TEXTURE_1D,0,GL_LUMINANCE,-1,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,pixels);
    VIMCheck(glGetError()==GL_INVALID_VALUE,"negative width rejected");
    glPixelStorei(GL_UNPACK_SKIP_PIXELS,0);glTexImage1D(GL_TEXTURE_1D,0,GL_LUMINANCE,200,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,NULL);
    VIMCheck(glGetError()==GL_NO_ERROR,"null pointer allocates 1D storage");
    glTexSubImage1D(GL_TEXTURE_1D,0,0,0,GL_LUMINANCE,GL_UNSIGNED_BYTE,NULL);
    VIMCheck(glGetError()==GL_NO_ERROR,"zero-width null subimage accepted");
    glDeleteBuffers(1,&pbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&destination);glDeleteTextures(1,&texture);munmap(allocation,page*2);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);CGLDestroyPixelFormat(pf);
    fprintf(stderr,"texture1d failures=%d\n",failures);return failures?1:0;
}
