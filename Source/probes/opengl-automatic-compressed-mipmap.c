/* Public SGIS controls with real precompressed S3TC image/subimage input.
 * Constant primary-color blocks avoid choosing a filter or encoder. GPU
 * sampling is forced to child LOD1; unrelated cube faces have blue sentinels. */
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
static GLenum list_mode;
static int pbo_input,linear_only,clipped_max;
static void check(int good,const char *what)
{++checks;failures+=!good;printf("%s %s\n",good?"PASS":"FAIL",what);fflush(stdout);}
static unsigned blocks(unsigned char *data,unsigned size,unsigned kind,unsigned color)
{
    unsigned stride=kind<2?8:16,offset=kind<2?0:8;
    const unsigned short colors[]={0xf800,0x07e0,0x001f};
    unsigned count=((size+3)/4)*((size+3)/4);
    memset(data,0,count*stride);
    for(unsigned b=0;b<count;++b){unsigned char *block=data+b*stride;
        if(kind==2)memset(block,255,8);
        if(kind==3)block[0]=255;
        block[offset]=(unsigned char)colors[color];block[offset+1]=(unsigned char)(colors[color]>>8);
    }
    return count*stride;
}
static int color_matches(const unsigned char *data,unsigned count,unsigned color)
{
    const unsigned char colors[][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255}};
    for(unsigned i=0;i<count*4;++i){int delta=(int)data[i]-colors[color][i%4];
        if(delta < -8||delta > 8){printf("color=%u byte=%u actual=%u expected=%u\n",color,i,data[i],colors[color][i%4]);return 0;}}
    return 1;
}
static void draw(GLenum target,unsigned face)
{
    static const GLfloat directions[][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    glEnable(target);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(target,GL_TEXTURE_MIN_LOD,1);glTexParameteri(target,GL_TEXTURE_MAX_LOD,1);
    if(target==GL_TEXTURE_CUBE_MAP)glTexCoord3fv(directions[face]);else glTexCoord2f(.5f,.5f);
    glClear(GL_COLOR_BUFFER_BIT);glBegin(GL_QUADS);
    glVertex2f(-1,-1);glVertex2f(1,-1);glVertex2f(1,1);glVertex2f(-1,1);glEnd();
    glDisable(target);
}
static void run(unsigned kind,int srgb,int cube,int base,int subimage)
{
    GLenum internal=(srgb?0x8c4c:0x83f0)+kind,target=cube?GL_TEXTURE_CUBE_MAP:GL_TEXTURE_2D;
    unsigned stride=kind<2?8:16,faces=cube?6:1;GLuint texture=0,list=0,pbo=0;
    unsigned char data[512],out[1024];glGenTextures(1,&texture);glBindTexture(target,texture);
    glTexParameteri(target,GL_TEXTURE_BASE_LEVEL,base);glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+(clipped_max?0:2));
    for(unsigned face=0;face<faces;++face)for(int level=0;level<=base+3;++level){
        unsigned size=level<=base?4u<<(base-level):level==base+1?2:1;
        unsigned bytes=blocks(data,size,kind,level==base?0:2);
        glCompressedTexImage2D(cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target,level,internal,
            (GLsizei)size,(GLsizei)size,0,(GLsizei)bytes,data);
    }
    glTexParameteri(target,GL_GENERATE_MIPMAP,GL_TRUE);
    check(!glGetError(),"compressed automatic source accepted");
    printf("compressed automatic kind=%u srgb=%d cube=%d base=%d subimage=%d mode=%x pbo=%d\n",
        kind,srgb,cube,base,subimage,list_mode,pbo_input);fflush(stdout);
    unsigned bytes=blocks(data,4,kind,1);
    if(pbo_input){glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);
        glBufferData(GL_PIXEL_UNPACK_BUFFER,bytes,data,GL_STATIC_DRAW);}
    GLenum image=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X:target;
    if(list_mode){list=glGenLists(1);glNewList(list,list_mode);}
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    struct DGNativeMipmapList owner={0};
    struct DGNativeMipmapPair *pair=adapted&&list_mode?DGNativeMipmapBegin(
        subimage?DG_NATIVE_MIPMAP_COMPSUB2:DG_NATIVE_MIPMAP_COMPIMAGE2,image,base,1,0,list_mode,&owner):NULL;
    struct DGAutomaticMipmapScope scope=DGBeginAutomaticMipmapExecute(image,base,adapted,0,
        pair&&list_mode==GL_COMPILE_AND_EXECUTE?0:list_mode,&adapter_error,0,1,&execution);
#endif
    if(subimage)glCompressedTexSubImage2D(image,base,0,0,4,4,internal,(GLsizei)bytes,pbo_input?NULL:data);
    else glCompressedTexImage2D(image,base,internal,4,4,0,(GLsizei)bytes,pbo_input?NULL:data);
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGEndAutomaticMipmap(&scope,&execution,&adapter_error);DGNativeMipmapEnd(pair);
    struct DGNativeMipmapPlayback playback={CGLGetCurrentContext(),&execution,&adapter_error,0,1};
    if(adapted)dg_native_mipmap_playback=&playback;
#endif
    if(list_mode){glEndList();glCallList(list);}
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    dg_native_mipmap_playback=NULL;
#endif
    check(!glGetError(),"compressed base update accepted");
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    if(clipped_max){
        GLint value=-1;glGetTexParameteriv(target,GL_TEXTURE_MAX_LEVEL,&value);
        check(!glGetError()&&value==base,"automatic generation retains clipped maximum");
        /* Make LOD1 accessible for the GPU oracle only AFTER generation. */
        glTexParameteri(target,GL_TEXTURE_MAX_LEVEL,base+2);
    }
    for(unsigned face=0;face<faces;++face){
        image=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X+face:target;
        glGetTexImage(image,base,GL_RGBA,GL_UNSIGNED_BYTE,out);
        check(!glGetError()&&color_matches(out,16,face?0:1),"compressed source pixels preserved");
        for(int level=1;level<=2;++level){
            GLint width=0,format=0,compressed=0,size=0;
            glGetTexLevelParameteriv(image,base+level,GL_TEXTURE_WIDTH,&width);
            glGetTexLevelParameteriv(image,base+level,GL_TEXTURE_INTERNAL_FORMAT,&format);
            glGetTexLevelParameteriv(image,base+level,GL_TEXTURE_COMPRESSED,&compressed);
            glGetTexLevelParameteriv(image,base+level,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,&size);
            check(!glGetError()&&width==(4>>level)&&format==(GLint)internal&&compressed==GL_TRUE&&size==(GLint)stride,
                "compressed child geometry format and block size retained");
            memset(out,0,sizeof(out));glGetTexImage(image,base+level,GL_RGBA,GL_UNSIGNED_BYTE,out);
            check(!glGetError()&&color_matches(out,(unsigned)(width*width),face?2:1),
                "compressed automatic generation updates only selected children");
        }
        glGetTexImage(image,base+3,GL_RGBA,GL_UNSIGNED_BYTE,out);
        check(!glGetError()&&color_matches(out,1,2),"compressed generation preserves excluded tail");
        draw(target,face);memset(out,0,4);glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,out);
        check(!glGetError()&&color_matches(out,1,face?2:1),"GPU samples the expected compressed child LOD");
    }
    GLint value=0;glGetTexParameteriv(target,GL_TEXTURE_BASE_LEVEL,&value);
    check(!glGetError()&&value==base,"compressed generation preserves selected base");
    if(list)glDeleteLists(list,1);if(pbo)glDeleteBuffers(1,&pbo);glDeleteTextures(1,&texture);
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGNativeMipmapFreePairs(&owner);
#endif
}
int main(int argc,char **argv)
{
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--list"))list_mode=GL_COMPILE;
        else if(!strcmp(argv[i],"--compile-execute"))list_mode=GL_COMPILE_AND_EXECUTE;
        else if(!strcmp(argv[i],"--pbo"))pbo_input=1;
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
        else if(!strcmp(argv[i],"--adapted"))adapted=1;
#endif
        else if(!strcmp(argv[i],"--linear-only"))linear_only=1;
        else if(!strcmp(argv[i],"--clipped-max"))clipped_max=1;else return 2;}
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
#ifdef VIM_AUTOMATIC_NATIVE_CONTROL
    DGMipmapCaptureExecution(context,&execution);
#endif
    GLuint fbo=0,color=0;glGenFramebuffersEXT(1,&fbo);glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,fbo);
    glGenRenderbuffersEXT(1,&color);glBindRenderbufferEXT(GL_RENDERBUFFER_EXT,color);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT,GL_RGBA8,1,1);
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT,GL_COLOR_ATTACHMENT0_EXT,GL_RENDERBUFFER_EXT,color);
    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);glViewport(0,0,1,1);
    check(glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT)==GL_FRAMEBUFFER_COMPLETE_EXT&&!glGetError(),"compressed sampling target complete");
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned kind=0;kind<4;++kind)for(int srgb=0;srgb<(linear_only?1:2);++srgb)
        for(int cube=0;cube<2;++cube)for(int base=0;base<=2;base+=2)
            for(int subimage=0;subimage<2;++subimage)run(kind,srgb,cube,base,subimage);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,0);glDeleteFramebuffersEXT(1,&fbo);glDeleteRenderbuffersEXT(1,&color);
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("automatic-compressed checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
