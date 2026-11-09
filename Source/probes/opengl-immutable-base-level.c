/* Isolate native immutable mip generation before applying any adapter. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
int main(int argc,char **argv)
{
    GLenum internal=GL_SRGB8_ALPHA8;
    if(argc==2&&!strcmp(argv[1],"--linear"))internal=GL_RGBA8;else if(argc!=1)return 2;
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;CGLDestroyPixelFormat(pf);
    printf("renderer=%s internal=%x\n",glGetString(GL_RENDERER),internal);
    typedef void (*Storage1)(GLenum,GLsizei,GLenum,GLsizei);
    typedef void (*Storage2)(GLenum,GLsizei,GLenum,GLsizei,GLsizei);
    typedef void (*Storage3)(GLenum,GLsizei,GLenum,GLsizei,GLsizei,GLsizei);
    Storage1 one=(Storage1)dlsym(RTLD_DEFAULT,"glTexStorage1D");
    Storage2 two=(Storage2)dlsym(RTLD_DEFAULT,"glTexStorage2D");
    Storage3 three=(Storage3)dlsym(RTLD_DEFAULT,"glTexStorage3D");
    if(!one||!two||!three)return 77;
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    const GLenum targets[]={GL_TEXTURE_1D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_CUBE_MAP};
    for(unsigned t=0;t<4;++t)for(int skip=0;skip<2;++skip){
        GLenum target=targets[t];int h=t==0?1:4,d=t==2?4:1,faces=t==3?6:1;
        GLuint tex=0;glGenTextures(1,&tex);glBindTexture(target,tex);
        if(t==0)one(target,5,internal,16);
        else if(t==2)three(target,5,internal,16,16,16);
        else two(target,5,internal,16,16);
        unsigned char pixels[256],before[6][256],after[256];
        for(int f=0;f<faces;++f){
            for(int i=0;i<4*h*d;++i){pixels[i*4]=(unsigned char)(20+f*10);pixels[i*4+1]=80;pixels[i*4+2]=140;pixels[i*4+3]=255;}
            GLenum image=faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target;
            if(t==0)glTexSubImage1D(target,2,0,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            else if(t==2)glTexSubImage3D(target,2,0,0,0,4,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
            else glTexSubImage2D(image,2,0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
        }
        glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,2);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,4);
        glTexParameteri(target,GL_TEXTURE_SRGB_DECODE_EXT,skip?GL_SKIP_DECODE_EXT:GL_DECODE_EXT);
        for(int f=0;f<faces;++f)glGetTexImage(faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,2,GL_RGBA,GL_UNSIGNED_BYTE,before[f]);
        GLuint fbo=0;glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbo);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        for(int base=2;base>=0;base-=2){
            glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);
            for(int f=0;f<faces;++f){
                if(t==0)glFramebufferTexture1DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,target,tex,2);
                else if(t==2)glFramebufferTextureLayerEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,tex,2,0);
                else glFramebufferTexture2DEXT(GL_READ_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,tex,2);
                GLenum status=glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER_EXT);
                glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,after);
                printf("fbo target=%x base=%d face=%d status=%x pixel=%u,%u,%u,%u error=%x\n",target,base,f,status,after[0],after[1],after[2],after[3],glGetError());
            }
        }
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
        glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,2);
        GLenum initial=glGetError();glGenerateMipmapEXT(target);GLenum generated=glGetError();
        for(int f=0;f<faces;++f){
            GLenum image=faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target;
            glGetTexImage(image,2,GL_RGBA,GL_UNSIGNED_BYTE,after);
            printf("target=%x skip=%d face=%d initial_error=%x generation_error=%x base_before=%u,%u,%u,%u base_after=%u,%u,%u,%u unchanged=%d\n",
                target,skip,f,initial,generated,before[f][0],before[f][1],before[f][2],before[f][3],after[0],after[1],after[2],after[3],!memcmp(before[f],after,4*h*d*4));
            glGetTexImage(image,3,GL_RGBA,GL_UNSIGNED_BYTE,after);
            printf("child=%u,%u,%u,%u error=%x\n",after[0],after[1],after[2],after[3],glGetError());
        }
        GLuint proxy=0;glGenTextures(1,&proxy);glBindTexture(target,proxy);
        if(t==0)glTexImage1D(target,0,GL_RGBA8,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        else if(t==2)glTexImage3D(target,0,GL_RGBA8,4,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        else for(int f=0;f<faces;++f)glTexImage2D(faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
        glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,2);
        glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,fbo);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        for(int layer=0;layer<(t==2?4:faces);++layer){
            DGMipmapAttach(target,tex,2,layer);
            glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,after);
            printf("copy-source target=%x layer=%d pixel=%u,%u,%u,%u error=%x\n",target,layer,after[0],after[1],after[2],after[3],glGetError());
            DGMipmapCopy(target,0,layer,4,h);
        }
        for(int f=0;f<faces;++f){
            glGetTexImage(faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,0,GL_RGBA,GL_UNSIGNED_BYTE,after);
            printf("proxy target=%x face=%d base=%u,%u,%u,%u error=%x\n",target,f,after[0],after[1],after[2],after[3],glGetError());
        }
        glGenerateMipmapEXT(target);
        for(int f=0;f<faces;++f){
            glGetTexImage(faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,1,GL_RGBA,GL_UNSIGNED_BYTE,after);
            printf("proxy target=%x face=%d child=%u,%u,%u,%u error=%x\n",target,f,after[0],after[1],after[2],after[3],glGetError());
        }
        glBindTexture(target,tex);
        for(int layer=0;layer<(t==2?2:faces);++layer){
            DGMipmapAttach(target,proxy,1,layer);DGMipmapCopy(target,3,layer,2,t==0?1:2);
        }
        for(int f=0;f<faces;++f){
            glGetTexImage(faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target,3,GL_RGBA,GL_UNSIGNED_BYTE,after);
            printf("copied target=%x face=%d child=%u,%u,%u,%u error=%x\n",target,f,after[0],after[1],after[2],after[3],glGetError());
        }
        glBindFramebufferEXT(GL_READ_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteTextures(1,&proxy);
        for(int base=2;base>=0;base-=2){
            glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);
            for(int f=0;f<faces;++f){
                for(unsigned i=0;i<sizeof(pixels);i+=4){pixels[i]=30;pixels[i+1]=40;pixels[i+2]=50;pixels[i+3]=255;}
                GLenum image=faces==6?GL_TEXTURE_CUBE_MAP_POSITIVE_X+f:target;
                if(t==0)glTexSubImage1D(target,3,0,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
                else if(t==2)glTexSubImage3D(target,3,0,0,0,2,2,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
                else glTexSubImage2D(image,3,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
                glGetTexImage(image,3,GL_RGBA,GL_UNSIGNED_BYTE,after);
                printf("update target=%x base=%d face=%d pixel=%u,%u,%u,%u error=%x\n",target,base,f,after[0],after[1],after[2],after[3],glGetError());
            }
        }
        glDeleteTextures(1,&tex);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);return 0;
}
