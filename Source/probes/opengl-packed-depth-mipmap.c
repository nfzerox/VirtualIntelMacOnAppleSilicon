/* Packed depth/stencil through public GL. Generated stencil values are
 * undefined (EXT_packed_depth_stencil issue 12), so only preserved images
 * and explicit stencil clears have a stencil-value oracle. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#ifdef VIM_PACKED_MIPMAP_NATIVE_CONTROL
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
#include "../PassthroughOpenGL/passthrough-opengl-automatic-mipmap-host.h"
#include "opengl-native-mipmap-list-diagnostic.h"
static int adapted;
static GLenum adapter_error;
static struct DGMipmapExecution execution;
static GLenum control_error(void)
{GLenum error=glGetError();if(adapter_error){error=adapter_error;adapter_error=0;}return error;}
#define glGetError control_error
#endif
struct FloatStencil {float depth;uint32_t stencil;};
static unsigned checks,failures,unsupported;
static int automatic,core,pbo_input;
static GLenum list_mode;
static void check(int good,const char *what)
{++checks;failures+=!good;printf("%s %s\n",good?"PASS":"FAIL",what);fflush(stdout);}
static GLenum pixel_type(int floating){return floating?0x8dad:GL_UNSIGNED_INT_24_8;}
static unsigned pixel_bytes(int floating){return floating?8:4;}
static void fill(void *data,unsigned pixels,int floating,float depth,unsigned stencil)
{
    for(unsigned i=0;i<pixels;++i){
        if(floating){struct FloatStencil value={depth,stencil};memcpy((char *)data+i*8,&value,8);}
        else {uint32_t value=((uint32_t)llround((double)depth*16777215.0)<<8)|stencil;
            memcpy((char *)data+i*4,&value,4);}
    }
}
static int matches(const void *data,unsigned pixels,int floating,float depth,int stencil)
{
    for(unsigned i=0;i<pixels;++i){float actual;unsigned actual_stencil;
        if(floating){struct FloatStencil value;memcpy(&value,(const char *)data+i*8,8);
            actual=value.depth;actual_stencil=value.stencil&255;}
        else {uint32_t value;memcpy(&value,(const char *)data+i*4,4);
            actual=(float)((double)(value>>8)/16777215.0);actual_stencil=value&255;}
        if(!isfinite(actual)||fabsf(actual-depth)>2.f/16777215.f||
           (stencil>=0&&actual_stencil!=(unsigned)stencil)){
            printf("packed pixel=%u depth=%g expected=%g stencil=%u expected=%d\n",
                i,actual,depth,actual_stencil,stencil);return 0;}
    }return 1;
}
static void read_image(GLenum face,int level,unsigned size,int floating,float depth,int stencil)
{
    unsigned char data[2048];memset(data,0,sizeof(data));GLint width=0,internal=0,depth_bits=0,stencil_bits=0;
    glGetTexLevelParameteriv(face,level,GL_TEXTURE_WIDTH,&width);
    glGetTexLevelParameteriv(face,level,GL_TEXTURE_INTERNAL_FORMAT,&internal);
    glGetTexLevelParameteriv(face,level,GL_TEXTURE_DEPTH_SIZE,&depth_bits);
    glGetTexLevelParameteriv(face,level,0x88f1,&stencil_bits);
    check(!glGetError()&&width==(GLint)size&&internal==(floating?0x8cad:0x88f0)&&
        depth_bits==(floating?32:24)&&stencil_bits==8,"packed image retains geometry and component precision");
    glGetTexImage(face,level,GL_DEPTH_STENCIL,pixel_type(floating),data);
    check(!glGetError()&&matches(data,size*size,floating,depth,stencil),"packed image pixels match defined depth/stencil values");
}
static void gpu_child(GLuint texture,GLenum face,int level,int floating,float depth)
{
    GLuint fbo=0;unsigned char data[8]={0};glGenFramebuffersEXT(1,&fbo);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_DEPTH_ATTACHMENT_EXT,face,texture,level);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,GL_STENCIL_ATTACHMENT_EXT,face,texture,level);
    glReadBuffer(GL_NONE);glDrawBuffer(GL_NONE);
    GLenum status=glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT),error=glGetError();
    check(status==GL_FRAMEBUFFER_COMPLETE_EXT&&!error,"packed child is a usable depth/stencil GPU attachment");
    if(status==GL_FRAMEBUFFER_COMPLETE_EXT&&!error){
        glReadPixels(0,0,1,1,GL_DEPTH_STENCIL,pixel_type(floating),data);
        check(!glGetError()&&matches(data,1,floating,depth,-1),"GPU attachment has generated depth");
        glStencilMask(255);glClearStencil(165);glClear(GL_STENCIL_BUFFER_BIT);
        glReadPixels(0,0,1,1,GL_DEPTH_STENCIL,pixel_type(floating),data);
        check(!glGetError()&&matches(data,1,floating,depth,165),"child stencil clear works without changing depth");
    }
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);
}
static void run(int floating,int cube,int base)
{
    GLenum target=cube?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D,internal=floating?0x8cad:0x88f0;
    unsigned faces=cube?6:1;GLuint texture=0,list=0,pbo=0;unsigned char data[2048];
    printf("case float=%d cube=%d base=%d automatic=%d mode=%x pbo=%d\n",floating,cube,base,automatic,list_mode,pbo_input);fflush(stdout);
    glGenTextures(1,&texture);glBindTexture(target,texture);
    for(unsigned face=0;face<faces;++face)for(int level=0;level<=base+3;++level){
        unsigned size=level<=base?4u<<(base-level):level==base+1?2:1;
        fill(data,size*size,floating,level==base?.25f:.75f,level==base?55:129);
        glTexImage2D(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target,level,internal,
            (GLsizei)size,(GLsizei)size,0,GL_DEPTH_STENCIL,pixel_type(floating),data);
    }
    GLenum error=glGetError();
    if(error){++unsupported;printf("UNSUPPORTED packed source error=%x\n",error);glDeleteTextures(1,&texture);return;}
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+2);
    check(!glGetError(),"packed source range accepted");
    fill(data,16,floating,.5f,89);GLenum image=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target;
    if(pbo_input){glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,16*pixel_bytes(floating),data,GL_STATIC_DRAW);}
    if(automatic)glTexParameteri(target,GL_GENERATE_MIPMAP,GL_TRUE);
    if(list_mode){list=glGenLists(1);glNewList(list,list_mode);}
#ifdef VIM_PACKED_MIPMAP_NATIVE_CONTROL
    struct DGNativeMipmapList owner={0};
    struct DGNativeMipmapPair *pair=adapted&&list_mode?DGNativeMipmapBegin(
        DG_NATIVE_MIPMAP_SUB2,image,base,1,0,list_mode,&owner):NULL;
    struct DGAutomaticMipmapScope scope=DGBeginAutomaticMipmapExecute(image,base,adapted&&automatic,
        0,pair&&list_mode==GL_COMPILE_AND_EXECUTE?0:list_mode,&adapter_error,0,1,&execution);
#endif
    glTexSubImage2D(image,base,0,0,4,4,GL_DEPTH_STENCIL,pixel_type(floating),pbo_input?NULL:data);
#ifdef VIM_PACKED_MIPMAP_NATIVE_CONTROL
    DGEndAutomaticMipmap(&scope,&execution,&adapter_error);DGNativeMipmapEnd(pair);
    struct DGNativeMipmapPlayback playback={CGLGetCurrentContext(),&execution,&adapter_error,0,1};
    if(adapted)dg_native_mipmap_playback=&playback;
#endif
    if(list_mode){glEndList();glCallList(list);}
#ifdef VIM_PACKED_MIPMAP_NATIVE_CONTROL
    dg_native_mipmap_playback=NULL;
#endif
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    if(!automatic)glGenerateMipmapEXT(target);
    check(!glGetError(),"packed generation accepted");
    if(automatic)glTexParameteri(target,GL_GENERATE_MIPMAP,GL_FALSE);
    for(unsigned face=0;face<faces;++face){
        image=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target;
        float expected=face?(automatic?.75f:.25f):.5f;
        read_image(image,base,4,floating,face?.25f:.5f,face?55:89);
        read_image(image,base+1,2,floating,expected,face&&automatic?129:-1);
        read_image(image,base+2,1,floating,expected,face&&automatic?129:-1);
        read_image(image,base+3,1,floating,.75f,129);
        if(base)read_image(image,base-1,8,floating,.75f,129);
        gpu_child(texture,image,base+1,floating,expected);
    }
    GLint value=0;glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&value);
    check(!glGetError()&&value==base,"packed generation preserves base");
    glGetTexParameteriv(target,GL_TEXTURE_MAX_LEVEL,&value);
    check(!glGetError()&&value==base+2,"packed generation preserves maximum");
    if(list)glDeleteLists(list,1);if(pbo)glDeleteBuffers(1,&pbo);glDeleteTextures(1,&texture);
#ifdef VIM_PACKED_MIPMAP_NATIVE_CONTROL
    DGNativeMipmapFreePairs(&owner);
#endif
    check(!glGetError(),"packed cleanup leaves no error");
}
int main(int argc,char **argv)
{
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--automatic"))automatic=1;
        else if(!strcmp(argv[i],"--core"))core=1;
        else if(!strcmp(argv[i],"--list"))list_mode=GL_COMPILE;
        else if(!strcmp(argv[i],"--compile-execute"))list_mode=GL_COMPILE_AND_EXECUTE;
        else if(!strcmp(argv[i],"--pbo"))pbo_input=1;
#ifdef VIM_PACKED_MIPMAP_NATIVE_CONTROL
        else if(!strcmp(argv[i],"--adapted"))adapted=1;
#endif
        else return 2;}
    if((core&&automatic)||(list_mode&&!automatic))return 2;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        core?kCGLPFAOpenGLProfile:0,(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
#ifdef VIM_PACKED_MIPMAP_NATIVE_CONTROL
    DGMipmapCaptureExecution(context,&execution);
#endif
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(int floating=0;floating<2;++floating)for(int cube=0;cube<2;++cube)
        for(int base=0;base<=2;base+=2)run(floating,cube,base);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("packed-mipmap checks=%u failures=%u unsupported=%u\n",checks,failures,unsupported);
    return failures?1:unsupported==8?77:0;
}
