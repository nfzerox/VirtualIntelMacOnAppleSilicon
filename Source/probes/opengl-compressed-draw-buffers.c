/* High Sierra Maps/WebKit GLI gaps. Run unchanged against native Intel GL
 * and the guest driver; no screenshots, network or application data needed. */
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <OpenGL/CGLRenderers.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "opengl-reference-renderer.h"
#endif
static int failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);failures+=!good;fflush(stdout);}
static void block(unsigned char *p,unsigned size,unsigned short color)
{
    memset(p,0,size);
    if(size==16){memset(p,255,8);p+=8;}
    p[0]=color&255;p[1]=color>>8;
}
static void sample(GLuint source,unsigned char *pixels)
{
    GLuint output=0,fbo=0;glGenTextures(1,&output);glBindTexture(GL_TEXTURE_2D,output);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_2D,output,0);
    glBindTexture(GL_TEXTURE_2D,source);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glEnable(GL_TEXTURE_2D);
    glViewport(0,0,8,8);glColor4f(1,1,1,1);
    glMatrixMode(GL_PROJECTION);glLoadIdentity();glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(0,0);glVertex2f(-1,-1);glTexCoord2f(1,0);glVertex2f(1,-1);
    glTexCoord2f(1,1);glVertex2f(1,1);glTexCoord2f(0,1);glVertex2f(-1,1);glEnd();
    glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glDisable(GL_TEXTURE_2D);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&output);glBindTexture(GL_TEXTURE_2D,source);
}
int main(int argc,char **argv)
{
    (void)argv;int reference=argc>1;
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
    (void)reference;unsigned char shadow[8*8*4]={0};int attrs[]={0x56494d01,1,0};
    struct DGContext *c=DGCreateContext(attrs,NULL);
    check(c&&DGMakeCurrent(c,shadow,GL_UNSIGNED_BYTE,8,8),"accelerated context");if(!c)return 1;
#else
    GLint renderer=reference?VIMReferenceIntelHD5000():0;
    if(reference&&!renderer){fprintf(stderr,"No native Intel reference renderer\n");return 2;}
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFADoubleBuffer,
        kCGLPFAColorSize,32,kCGLPFARendererID,(CGLPixelFormatAttribute)renderer,0};
    if(!reference)attrs[6]=0;
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!e&&pf)e=CGLCreateContext(pf,NULL,&c);
    if(pf)CGLDestroyPixelFormat(pf);
    check(!e&&c,"accelerated context");if(!c)return 1;CGLSetCurrentContext(c);
#endif
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLint bits[3]={-1,0x1234,0x5678};glGetIntegerv(GL_SUBPIXEL_BITS,bits);
    check(bits[0]>=0&&bits[1]==0x1234&&bits[2]==0x5678&&!glGetError(),"SUBPIXEL_BITS scalar query");
    GLuint texture=0,pbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    const GLenum formats[]={GL_COMPRESSED_RGBA_S3TC_DXT1_EXT,GL_COMPRESSED_RGBA_S3TC_DXT3_EXT,GL_COMPRESSED_RGBA_S3TC_DXT5_EXT};
    for(unsigned f=0;f<3;++f){
        unsigned size=f?16:8;unsigned char red[64],green[16],pixels[8*8*4];
        for(unsigned b=0;b<4;++b)block(red+b*size,size,0xf800);
        block(green,size,0x07e0);
        glCompressedTexImage2D(GL_TEXTURE_2D,0,formats[f],8,8,0,size*4,red);
        sample(texture,pixels);
        check(!glGetError()&&pixels[0]>240&&pixels[1]<8,"compressed DXT allocation/sample");
        glCompressedTexSubImage2D(GL_TEXTURE_2D,0,4,4,4,4,formats[f],size,green);
        sample(texture,pixels);
        unsigned char *p=pixels+(4*8+4)*4;
        check(!glGetError()&&p[0]<8&&p[1]>240&&pixels[0]>240,"compressed DXT subimage preserves surrounding blocks");
        unsigned char storage[80]={0};memcpy(storage+16,green,size);
        glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(storage),storage,GL_STATIC_DRAW);
        glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,formats[f],size,(void *)16);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
        sample(texture,pixels);
        check(!glGetError()&&pixels[0]<8&&pixels[1]>240,"compressed PBO offset upload");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glBufferSubData(GL_PIXEL_UNPACK_BUFFER,0,size,green);
        glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,formats[f],size,NULL);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);sample(texture,pixels);
        check(!glGetError()&&pixels[0]<8&&pixels[1]>240,"compressed PBO zero offset is not a null allocation");
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
        glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,formats[f],size,(void *)80);
        check(glGetError()==GL_INVALID_OPERATION,"compressed PBO out-of-bounds rejected");
        void *mapped=glMapBuffer(GL_PIXEL_UNPACK_BUFFER,GL_READ_ONLY);
        glCompressedTexSubImage2D(GL_TEXTURE_2D,0,0,0,4,4,formats[f],size,0);
        check(mapped&&glGetError()==GL_INVALID_OPERATION,"compressed mapped PBO rejected");
        if(mapped)glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&pbo);
    }
    glDeleteTextures(1,&texture);
    GLuint fbo=0,textures[2]={0};glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glGenTextures(2,textures);
    GLenum buffers[]={GL_COLOR_ATTACHMENT0_EXT,GL_COLOR_ATTACHMENT1_EXT};
    for(unsigned i=0;i<2;++i){
        glBindTexture(GL_TEXTURE_2D,textures[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,buffers[i],GL_TEXTURE_2D,textures[i],0);
    }
    glDrawBuffersARB(2,buffers);glClearColor(0.25,0.5,0.75,1);glClear(GL_COLOR_BUFFER_BIT);
    for(unsigned i=0;i<2;++i){
        unsigned char pixel[4]={0};glReadBuffer(buffers[i]);glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        GLint selected=0;glGetIntegerv(GL_DRAW_BUFFER0_ARB+i,&selected);
        check(!glGetError()&&selected==(GLint)buffers[i]&&pixel[0]>=63&&pixel[0]<=64&&pixel[1]>=127&&pixel[1]<=128&&pixel[2]>=191&&pixel[2]<=192,"MRT clear/query/readback");
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);
    glDrawBuffer(GL_FRONT);GLint single=0;glGetIntegerv(GL_DRAW_BUFFER0_ARB,&single);
    check(!glGetError()&&single==GL_FRONT,"single draw-buffer alias query preserved");
    GLenum native[]={GL_FRONT_LEFT,GL_BACK_LEFT};glDrawBuffersARB(2,native);
    GLint selected[2]={0};glGetIntegerv(GL_DRAW_BUFFER0_ARB,selected);glGetIntegerv(GL_DRAW_BUFFER1_ARB,selected+1);
    check(!glGetError()&&selected[0]==GL_FRONT_LEFT&&selected[1]==GL_BACK_LEFT,"default framebuffer draw-buffer translation");
    GLenum back=GL_BACK;glDrawBuffersARB(1,&back);
    check(glGetError()==GL_INVALID_OPERATION,"legacy GL_BACK rejected like native Intel");
    glDrawBuffersARB(0,NULL);glGetIntegerv(GL_DRAW_BUFFER0_ARB,selected);
    check(!glGetError()&&selected[0]==GL_NONE,"zero-count draw buffers ignores pointer");
    glDrawBuffersARB(-1,(const GLenum *)1);
    check(glGetError()==GL_INVALID_VALUE,"negative draw-buffer count ignores pointer");
    glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(2,textures);
#ifdef VIM_PASSTHROUGH_OPENGL_INPROCESS_CLIENT
    DGDestroyContext(c);
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
#endif
    return failures?1:0;
}
