/* Compare native and installed core extension discovery by CGL selector. */
#ifdef VIM_CORE_EXTENSIONS_WIRE
#include "../PassthroughOpenGL/passthrough-opengl-client.c"
#define GL_NUM_EXTENSIONS 0x821d
#else
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#endif
#include <stdio.h>
#include <string.h>

int main(int argc,char **argv)
{
    const int gl4=argc>1 && !strcmp(argv[1],"--gl4");
#ifdef VIM_CORE_EXTENSIONS_WIRE
    const int attrs[]={0x56494d02,gl4?DG_CGL_PROFILE_GL4:0x3200,0};unsigned char screen[16*16*4]={0};
    struct DGContext *context=DGCreateContext(attrs,NULL);
    if(!context || !DGMakeCurrent(context,screen,GL_UNSIGNED_BYTE,16,16))return 2;
#else
    const CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)(gl4?kCGLOGLPVersion_GL4_Core:kCGLOGLPVersion_3_2_Core),0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint formats=0;
    if(CGLChoosePixelFormat(attrs,&format,&formats)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
#endif
    printf("selector=%s renderer=%s version=%s\n",gl4?"4.1":"3.2",
           glGetString(GL_RENDERER),glGetString(GL_VERSION));
    GLint count=-1;glGetIntegerv(GL_NUM_EXTENSIONS,&count);
    if(glGetError()!=GL_NO_ERROR || count<0)return 1;
    for(GLint i=0;i<count;++i){
        const GLubyte *name=glGetStringi(GL_EXTENSIONS,(GLuint)i);
        if(!name || glGetError()!=GL_NO_ERROR)return 1;
        puts((const char *)name);
    }
    if(glGetStringi(GL_EXTENSIONS,(GLuint)count) || glGetError()!=GL_INVALID_VALUE)return 1;
#ifdef VIM_CORE_EXTENSIONS_WIRE
    int live=!context->connection->dead;DGDestroyContext(context);return live?0:1;
#else
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);return 0;
#endif
}
