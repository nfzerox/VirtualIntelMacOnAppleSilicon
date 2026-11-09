/* Native cube validation and nonzero mutable no-child ranges. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
#include "../PassthroughOpenGL/passthrough-opengl-mipmap-host.h"
static unsigned failures,checks;
static int native_only;
static struct DGMipmapExecution execution;
static void check(int value,const char *label)
{printf("%s %s\n",value?"PASS":"FAIL",label);fflush(stdout);++checks;failures+=!value;}
static GLenum generate(GLenum target,int legacy)
{
    GLenum error=GL_NO_ERROR;
    if(native_only)glGenerateMipmapEXT(target);else error=DGGenerateSrgbMipmapExecute(target,legacy,0,&execution);
    if(!error)error=glGetError();return error;
}
static void cube(unsigned kind,int legacy)
{
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_CUBE_MAP,texture);
    unsigned char data[64],out[64];
    for(unsigned f=0;f<6;++f){
        const unsigned char seed[]={201,101,51,171};
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,3,GL_SRGB8_ALPHA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,seed);
        if((kind==0||kind==5)&&f==5)continue;
        for(unsigned i=0;i<sizeof(data);i+=4){data[i]=(unsigned char)(17+f);data[i+1]=71;data[i+2]=131;data[i+3]=191;}
        unsigned size=kind==1&&f==5?2:4;
        GLint internal=(kind==2||kind==6)&&f==5?GL_RGB8:GL_SRGB8_ALPHA8;
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,2,internal,size,size,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_BASE_LEVEL,2);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAX_LEVEL,kind>=4?2:3);
    check(!glGetError(),"cube validation source accepted");
    GLint metadata[6][3];
    for(unsigned f=0;f<6;++f){
        const GLenum keys[]={GL_TEXTURE_WIDTH,GL_TEXTURE_HEIGHT,GL_TEXTURE_INTERNAL_FORMAT};
        for(unsigned k=0;k<3;++k)glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,2,keys[k],metadata[f]+k);
        printf("cube kind=%u face=%u width=%d height=%d format=%x\n",kind,f,metadata[f][0],metadata[f][1],metadata[f][2]);
    }
    GLenum error=generate(GL_TEXTURE_CUBE_MAP,legacy);
    printf("cube kind=%u result=%x\n",kind,error);
    check(error==(kind<3||kind>=5?GL_INVALID_OPERATION:GL_NO_ERROR),"cube completeness error is native");
    check(!glGetError(),"cube validation leaves no extra error");
    for(unsigned f=0;f<6;++f){
        GLint width=0;glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,3,GL_TEXTURE_WIDTH,&width);
        glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X+f,3,GL_RGBA,GL_UNSIGNED_BYTE,out);
        if(kind!=3)check(width==1&&out[0]==201&&out[1]==101&&out[2]==51&&out[3]==171&&!glGetError(),
            "rejected or empty cube generation preserves children");
        else check(width==2&&out[0]==17+f&&out[1]==71&&out[2]==131&&out[3]==191&&!glGetError(),
            "complete cube replaces existing child geometry and pixels");
    }
    GLint value=0;glGetTexParameteriv(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_BASE_LEVEL,&value);
    check(value==2,"cube base state preserved");glDeleteTextures(1,&texture);
}
static void empty_range(int legacy)
{
    GLuint tex=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);
    const unsigned char base[]={17,71,131,191},child[]={201,101,51,171};
    unsigned char pixels[64],out[4];for(unsigned i=0;i<sizeof(pixels);i+=4)memcpy(pixels+i,base,4);
    glTexImage2D(GL_TEXTURE_2D,2,GL_SRGB8_ALPHA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    glTexImage2D(GL_TEXTURE_2D,3,GL_SRGB8_ALPHA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,child);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,2);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    check(!glGetError(),"mutable empty range accepted");check(!generate(GL_TEXTURE_2D,legacy),"mutable empty range is no-op");
    glGetTexImage(GL_TEXTURE_2D,3,GL_RGBA,GL_UNSIGNED_BYTE,out);
    check(!glGetError()&&!memcmp(out,child,4),"mutable empty range preserves child");
    glDeleteTextures(1,&tex);
}
int main(int argc,char **argv)
{
    int legacy=0;for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--legacy"))legacy=1;
        else if(!strcmp(argv[i],"--native")||!strcmp(argv[i],"--executor"))native_only=1;else return 2;}
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(legacy?kCGLOGLPVersion_Legacy:kCGLOGLPVersion_GL4_Core),0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&pf,&n)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&c)||CGLSetCurrentContext(c))return 2;CGLDestroyPixelFormat(pf);
    if(!native_only)DGMipmapCaptureExecution(c,&execution);
    printf("renderer=%s legacy=%d\n",glGetString(GL_RENDERER),legacy);
    glPixelStorei(GL_PACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ALIGNMENT,1);
    for(unsigned kind=0;kind<7;++kind)cube(kind,legacy);empty_range(legacy);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);printf("mipmap-validation checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
