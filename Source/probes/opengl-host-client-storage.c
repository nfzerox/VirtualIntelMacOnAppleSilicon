/* Bounded host-only experiment: can Apple's shared client texture storage
 * provide a bidirectional CPU/GPU backing for already-shared guest VRAM?
 * A retained client pointer alone proves nothing; observe both directions.
 * GPU waits/readbacks here are diagnostic, never a presentation strategy. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static void fill(uint32_t *p,size_t count,uint32_t color)
{for(size_t i=0;i<count;++i)p[i]=color;}
static int croppedView(void)
{
    const int width=640,height=480,x=3,y=7,w=251,h=197;
    uint32_t *memory=NULL;size_t bytes=(size_t)width*height*4;
    if(posix_memalign((void **)&memory,16384,bytes))return 0;
    fill(memory,bytes/4,0xff0000ff);
    GLuint textures[2]={0},fbos[2]={0};glGenTextures(2,textures);glGenFramebuffersEXT(2,fbos);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[0]);
    glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB,(GLsizei)bytes,memory);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_STORAGE_HINT_APPLE,GL_STORAGE_SHARED_APPLE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH,width);glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_TRUE);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,width,height,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,memory);
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_FALSE);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB,textures[1]);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,w,h,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,NULL);
    int good=1;
    for(unsigned i=0;i<2;++i){
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbos[i]);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,textures[i],0);
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        good=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT && good;
    }
    if(good){
        glDisable(GL_SCISSOR_TEST);glDisable(GL_FRAMEBUFFER_SRGB_EXT);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbos[0]);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,fbos[1]);
        glBlitFramebufferEXT(x,y,x+w,y+h,0,0,w,h,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        /* One partial GPU update leaves the rest of the view's blue contents
         * intact. Copy the complete view back to its original guest location. */
        glEnable(GL_SCISSOR_TEST);glScissor(11,13,101,67);glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbos[1]);glBindFramebufferEXT(GL_DRAW_FRAMEBUFFER_EXT,fbos[0]);
        glBlitFramebufferEXT(0,0,w,h,x,y,x+w,y+h,GL_COLOR_BUFFER_BIT,GL_NEAREST);glFinish();
        good=!glGetError();
        for(int row=0;row<height;++row)for(int col=0;col<width;++col){
            int green=col>=x+11 && col<x+112 && row>=y+13 && row<y+80;
            if(memory[(size_t)row*width+col]!=(green?0xff00ff00u:0xff0000ffu))good=0;
        }
    }
    printf("cropped view GPU blit to/from aligned shared backing, preserved partial update, no pixel readback: %s\n",good?"PASS":"FAIL");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(2,fbos);glDeleteTextures(2,textures);
    glFinish();free(memory);return good;
}
static int experiment(GLenum hint,int w,int h,int pitch,int x,int y)
{
    size_t size=(size_t)pitch*(h+y+1)*4;
    uint32_t *memory=NULL;if(posix_memalign((void **)&memory,16384,size))return 0;
    fill(memory,size/4,0x11223344);
    uint32_t *view=memory+(size_t)y*pitch+x;
    for(int row=0;row<h;++row)fill(view+(size_t)row*pitch,w,0xff0000ff);
    GLuint texture=0,fbo=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_RECTANGLE_ARB,texture);
    glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE_ARB,(GLsizei)size,memory);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_STORAGE_HINT_APPLE,hint);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT,4);glPixelStorei(GL_UNPACK_ROW_LENGTH,pitch);
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_TRUE);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB,0,GL_RGBA8,w,h,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,view);
    glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE,GL_FALSE);
    glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_TEXTURE_RECTANGLE_ARB,texture,0);
    GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT),error=glGetError();
    unsigned initial=0,withoutUpload=0,updated=0,gpuWritten=0,gpuRead=0;
    if(status==GL_FRAMEBUFFER_COMPLETE_EXT && !error){
        glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glReadPixels(0,0,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&initial);glFinish();
        for(int row=0;row<h;++row)fill(view+(size_t)row*pitch,w,0xffff0000);
        glReadPixels(0,0,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&withoutUpload);
        glTexSubImage2D(GL_TEXTURE_RECTANGLE_ARB,0,0,0,w,h,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,view);
        glReadPixels(0,0,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&updated);
        glDisable(GL_SCISSOR_TEST);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        glClearColor(0,1,0,1);glClear(GL_COLOR_BUFFER_BIT);glFinish();
        /* Inspect client memory BEFORE any readback of the rendered image. */
        gpuWritten=view[0];
        glReadPixels(0,0,1,1,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,&gpuRead);error=glGetError();
    }
    int guards=1;
    for(int row=0;row<h+y+1;++row)for(int col=0;col<pitch;++col){
        int inside=row>=y && row<y+h && col>=x && col<x+w;
        if(!inside && memory[(size_t)row*pitch+col]!=0x11223344)guards=0;
    }
    int coherent=guards && !error && initial==0xff0000ff && withoutUpload==0xffff0000 &&
        updated==0xffff0000 && gpuWritten==0xff00ff00 && gpuRead==0xff00ff00;
    printf("hint=%x view=%dx%d pitch=%d offset=%d,%d framebuffer=%x error=%x initial=%08x CPU-no-upload=%08x CPU-after-subimage=%08x GPU-client-before-readback=%08x GPU-read=%08x guards=%d bidirectional=%s\n",
        hint,w,h,pitch,x,y,status,error,initial,withoutUpload,updated,gpuWritten,gpuRead,guards,coherent?"YES":"NO");
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
    glDeleteTextures(1,&texture);glFinish();free(memory);return coherent;
}
int main(int argc,char **argv)
{
    int requireIntel=0,rendererOnly=0;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--require-intel"))requireIntel=1;
        else if(!strcmp(argv[i],"--renderer-only"))rendererOnly=1;
        else {fprintf(stderr,"Usage: %s [--require-intel] [--renderer-only]\n",argv[0]);return 2;}
    }
    /* Do not wake a discrete GPU just by creating this reference context. */
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count) || CGLCreateContext(pf,NULL,&c))return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(c);
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    const char *vendor=(const char *)glGetString(GL_VENDOR);
    GLint rendererID=0;CGLGetParameter(c,kCGLCPCurrentRendererID,&rendererID);
    printf("vendor=%s renderer=%s rendererID=0x%x\n",vendor?vendor:"(unknown)",
        renderer?renderer:"(unknown)",(unsigned)rendererID);
    if(requireIntel && (!renderer || !strstr(renderer,"Intel") || !vendor || !strstr(vendor,"Intel"))){
        fputs("REFUSED: physical reference must use accelerated Intel graphics\n",stderr);
        CGLSetCurrentContext(NULL);CGLDestroyContext(c);return 3;
    }
    if(rendererOnly){CGLSetCurrentContext(NULL);CGLDestroyContext(c);return 0;}
    const char *extensions=(const char *)glGetString(GL_EXTENSIONS);
    if(!extensions || !strstr(extensions,"GL_APPLE_client_storage") || !strstr(extensions,"GL_APPLE_texture_range")){
        puts("Client storage/range unavailable; direct memory path unqualified");return 0;
    }
    int shared=experiment(GL_STORAGE_SHARED_APPLE,256,256,256,0,0);
    shared=experiment(GL_STORAGE_SHARED_APPLE,251,197,640,3,7) && shared;
    shared=experiment(GL_STORAGE_SHARED_APPLE,1280,22,2560,0,0) && shared;
    shared=experiment(GL_STORAGE_SHARED_APPLE,517,333,2560,123,27) && shared;
    shared=experiment(GL_STORAGE_SHARED_APPLE,2560,1600,2560,0,0) && shared;
    experiment(GL_STORAGE_CACHED_APPLE,256,256,256,0,0);experiment(GL_STORAGE_PRIVATE_APPLE,256,256,256,0,0);
    int crop=croppedView();
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    puts(shared?"Shared client backing qualifies the tested layouts; lifetime/concurrency still unqualified":
        "Direct client storage does not cover every tested view layout; unaligned views require another path");
    if(crop)puts("Aligned full backing plus GPU-only cropped views remains a qualified candidate");
    return 0;
}
