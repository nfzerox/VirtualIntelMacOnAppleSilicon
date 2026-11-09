/* Public compiler-language matrix, unchanged on physical Intel and the guest. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    CGLOpenGLProfile profile=kCGLOGLPVersion_GL4_Core;
    if(argc==2&&!strcmp(argv[1],"--core32"))profile=kCGLOGLPVersion_3_2_Core;
    else if(argc==2&&!strcmp(argv[1],"--legacy"))profile=kCGLOGLPVersion_Legacy;
    else if(argc!=1)return 64;
    const CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)profile,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("language matrix profile=%x renderer=%s GL=%s GLSL=%s\n",profile,
        glGetString(GL_RENDERER),glGetString(GL_VERSION),glGetString(GL_SHADING_LANGUAGE_VERSION));
    const unsigned versions[]={110,120,130,140,150,330,400,410};
    for(unsigned i=0;i<sizeof(versions)/sizeof(*versions);++i){
        unsigned version=versions[i];char source[1024],log[4096];
        snprintf(source,sizeof(source),"#version %u\n#if __VERSION__ != %u\n#error wrong language version\n#endif\n"
            "void main(){gl_Position=vec4(0,0,0,1);}",version,version);
        GLuint shader=glCreateShader(GL_VERTEX_SHADER);const char *text=source;
        glShaderSource(shader,1,&text,NULL);glCompileShader(shader);
        GLint compiled=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&compiled);
        glGetShaderInfoLog(shader,sizeof(log),NULL,log);
        printf("version=%u compiled=%d error=%x log=%s\n",version,compiled,glGetError(),log);
        glDeleteShader(shader);
    }
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);return 0;
}
