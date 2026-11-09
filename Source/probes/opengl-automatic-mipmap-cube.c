/* Public GL automatic cube-face generation. GL2.1 section3.8.8 replaces
 * only the modified image's derived arrays, not the other five faces.
 * Constant images avoid choosing a particular implementation's filter. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
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
static unsigned checks,failures;
static int partial,srgb,state_controls,copy_source,pbo_source,unnamed;
static void check(int good,const char *what)
{++checks;failures+=!good;printf("%s %s\n",good?"PASS":"FAIL",what);fflush(stdout);}
static void seed(unsigned char *pixels,unsigned size,unsigned face,int changed)
{
    const unsigned char value[4]={(unsigned char)((changed==2?29:changed?201:17)+face),
        (unsigned char)(changed==2?89:changed?101:71),(unsigned char)(changed==2?149:changed?51:131),
        (unsigned char)(changed==2?209:changed?171:191)};
    for(unsigned i=0;i<size*size;++i)memcpy(pixels+i*4,value,4);
}
static int pixels(GLenum image,int level,int size,const unsigned char *wanted)
{
    GLint width=0;unsigned char out[1024];glGetTexLevelParameteriv(image,level,GL_TEXTURE_WIDTH,&width);
    if(width!=size){printf("face=%x level=%d width=%d expected=%d\n",image,level,width,size);return 0;}
    glGetTexImage(image,level,GL_RGBA,GL_UNSIGNED_BYTE,out);GLenum error=glGetError();
    for(int i=0;i<size*size*4;++i)if(out[i]!=wanted[i%4]){
        printf("face=%x level=%d byte=%d actual=%u expected=%u error=%x\n",image,level,i,out[i],wanted[i%4],error);return 0;}
    return !error;
}
static void run(int base,unsigned changed,int subimage,GLenum mode)
{
    GLuint texture=0,list=0;unsigned char data[1024];
    if(!unnamed)glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_CUBE_MAP,texture);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_GENERATE_MIPMAP,GL_FALSE);
    if(unnamed)for(unsigned face=0;face<6;++face)for(int level=0;level<=4;++level)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,GL_RGBA8,0,0,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_BASE_LEVEL,base);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAX_LEVEL,base+2);
    for(unsigned face=0;face<6;++face)for(int level=0;level<=base+2;++level){
        if(partial&&face!=changed)continue;
        int size=16>>level; /* base0 uses16, base2 uses4. */
        seed(data,(unsigned)size,face,level>base?2:0);
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,level,srgb?GL_SRGB8_ALPHA8:GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_GENERATE_MIPMAP,GL_TRUE);
    check(!glGetError(),"complete automatic cube source accepted");
    int size=16>>base;GLenum image=GL_TEXTURE_CUBE_MAP_POSITIVE_X+changed;
    seed(data,(unsigned)size,changed,1);
    GLuint other=0,pack=0,unpack=0,framebuffer=0,color=0;
    if(copy_source){
        glGenFramebuffersEXT(1,&framebuffer);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,framebuffer);
        glGenRenderbuffersEXT(1,&color);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,color);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,size,size);
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,color);
        glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
        glClearColor(data[0]/255.f,data[1]/255.f,data[2]/255.f,data[3]/255.f);glClear(GL_COLOR_BUFFER_BIT);
        check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT&&!glGetError(),
            "cube copy source complete");
    }
    if(pbo_source){glGenBuffers(1,&unpack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,size*size*4,data,GL_STATIC_DRAW);}
    if(state_controls){
        glGenTextures(1,&other);glBindTexture(GL_TEXTURE_2D,other);
        glGenBuffers(1,&pack);glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);
        glBufferData(GL_PIXEL_PACK_BUFFER,4096,NULL,GL_STREAM_COPY);
        glPixelStorei(GL_PACK_ALIGNMENT,8);glPixelStorei(GL_PACK_ROW_LENGTH,7);
        glPixelStorei(GL_PACK_SKIP_PIXELS,2);glPixelStorei(GL_PACK_SKIP_ROWS,1);
    }
    printf("cube case base=%d face=%u subimage=%d mode=%x\n",base,changed,subimage,mode);fflush(stdout);
    if(mode){list=glGenLists(1);glNewList(list,mode);}
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    struct DGNativeMipmapList owner={0};
    struct DGNativeMipmapPair *pair=adapted&&mode?DGNativeMipmapBegin(copy_source?
        (subimage?DG_NATIVE_MIPMAP_COPYSUB2:DG_NATIVE_MIPMAP_COPY2):
        (subimage?DG_NATIVE_MIPMAP_SUB2:DG_NATIVE_MIPMAP_IMAGE2),image,base,1,0,mode,&owner):NULL;
    struct DGAutomaticMipmapScope scope=DGBeginAutomaticMipmapExecute(image,base,adapted,0,
        pair&&mode==GL_COMPILE_AND_EXECUTE?0:mode,&adapter_error,copy_source,1,&execution);
#endif
    if(copy_source){
        if(subimage)glCopyTexSubImage2D(image,base,0,0,0,0,size,size);
        else glCopyTexImage2D(image,base,srgb?GL_SRGB8_ALPHA8:GL_RGBA8,0,0,size,size,0);
    }else if(subimage)glTexSubImage2D(image,base,0,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,pbo_source?NULL:data);
    else glTexImage2D(image,base,srgb?GL_SRGB8_ALPHA8:GL_RGBA8,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,pbo_source?NULL:data);
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGEndAutomaticMipmap(&scope,&execution,&adapter_error);DGNativeMipmapEnd(pair);
    struct DGNativeMipmapPlayback playback={CGLGetCurrentContext(),&execution,&adapter_error,0,1};
    if(adapted)dg_native_mipmap_playback=&playback;
#endif
    if(mode){glEndList();glCallList(list);}
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    dg_native_mipmap_playback=NULL;
#endif
    check(!glGetError(),"cube base update accepted");
    if(state_controls){
        const GLenum keys[]={GL_TEXTURE_BINDING_2D,GL_TEXTURE_BINDING_CUBE_MAP,GL_PIXEL_PACK_BUFFER_BINDING,
            GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_PIXELS,GL_PACK_SKIP_ROWS,GL_PIXEL_UNPACK_BUFFER_BINDING};
        const GLint expected[]={(GLint)other,(GLint)texture,(GLint)pack,8,7,2,1,(GLint)unpack};
        for(unsigned i=0;i<8;++i){GLint value=-1;glGetIntegerv(keys[i],&value);
            check(value==expected[i],"cube generation preserves caller binding and pack state");}
        glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_PACK_ROW_LENGTH,0);
        glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_SKIP_ROWS,0);
    }
    for(unsigned face=0;face<6;++face){
        if(partial&&face!=changed){
            GLint width=-1;glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,base+1,GL_TEXTURE_WIDTH,&width);
            check(width==0&&!glGetError(),"automatic generation does not define a missing cube face");continue;
        }
        unsigned char wanted[4];seed(wanted,1,face,face==changed);
        check(pixels(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,base,size,wanted),"cube update changes only its own base face");
        seed(wanted,1,face,face==changed?1:2);
        check(pixels(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,base+1,size/2,wanted),"cube generation changes only its own first child");
        if(base==2||face==changed)
            check(pixels(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,4,1,wanted),"cube face derives its full chain regardless of maximum level");
        else{
            GLint width=-1;glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X+face,4,GL_TEXTURE_WIDTH,&width);
            check(width==0&&!glGetError(),"automatic generation does not add other faces' excluded levels");
        }
    }
    if(list)glDeleteLists(list,1);glDeleteTextures(1,&texture);
    if(other)glDeleteTextures(1,&other);if(pack)glDeleteBuffers(1,&pack);
    if(unpack)glDeleteBuffers(1,&unpack);
    if(framebuffer){glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&framebuffer);
        glDeleteRenderbuffersEXT(1,&color);}
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGNativeMipmapFreePairs(&owner);
#endif
    check(!glGetError(),"cube cleanup accepted");
}
int main(int argc,char **argv)
{
    GLenum mode=0;int base=-1;
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--list"))mode=GL_COMPILE;
        else if(!strcmp(argv[i],"--compile-execute"))mode=GL_COMPILE_AND_EXECUTE;
        else if(!strcmp(argv[i],"--base-zero"))base=0;
        else if(!strcmp(argv[i],"--partial"))partial=1;
        else if(!strcmp(argv[i],"--srgb"))srgb=1;
        else if(!strcmp(argv[i],"--state-controls"))state_controls=1;
        else if(!strcmp(argv[i],"--copy"))copy_source=1;
        else if(!strcmp(argv[i],"--pbo"))pbo_source=1;
        else if(!strcmp(argv[i],"--unnamed"))unnamed=1;
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
        else if(!strcmp(argv[i],"--adapted"))adapted=1;
#endif
        else if(!strcmp(argv[i],"--base-two"))base=2;else return 2;}
    if(copy_source&&pbo_source)return 2;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGMipmapCaptureExecution(context,&execution);
#endif
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(int b=0;b<=2;b+=2)if(base<0||base==b)for(unsigned face=0;face<6;++face)
        for(int subimage=0;subimage<2;++subimage)run(b,face,subimage,mode);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("automatic-cube checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
