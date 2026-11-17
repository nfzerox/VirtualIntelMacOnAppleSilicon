/* Compile a supplied period shader using native GL and the production adapter.
 * The proprietary resource is supplied at runtime, never embedded here. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include "../PassthroughOpenGL/passthrough-opengl-shader-compile.h"

int main(int argc,char **argv) {
    if(argc!=2)return 64;
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    if(fseek(file,0,SEEK_END))return 2;
    long bytes=ftell(file);if(bytes<1||bytes>1024*1024)return 2;
    rewind(file);
    const char prefix[]="#define highp\n#define mediump\n#define lowp\n";
    size_t length=sizeof(prefix)-1+(size_t)bytes;
    char *source=malloc(length+1),*after=malloc(length+1);
    if(!source||!after)return 2;
    memcpy(source,prefix,sizeof(prefix)-1);
    if(fread(source+sizeof(prefix)-1,1,(size_t)bytes,file)!=(size_t)bytes)return 2;
    fclose(file);source[length]=0;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_Legacy,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attributes,&format,&count)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    GLuint shader=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(shader,1,(const char **)&source,NULL);glCompileShader(shader);
    GLint native=0,adapted=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&native);
    char log[2048]={0};glGetShaderInfoLog(shader,sizeof(log),NULL,log);
    GLenum error=DGCompileAdaptedShader(shader,0);
    glGetShaderiv(shader,GL_COMPILE_STATUS,&adapted);
    GLsizei restored=0;glGetShaderSource(shader,(GLsizei)length+1,&restored,after);
    int same=restored==(GLsizei)length&&!memcmp(source,after,length+1);
    int good=adapted&&same&&error==GL_NO_ERROR&&glGetError()==GL_NO_ERROR;
    printf("renderer=%s native=%d adapted=%d original-source=%d\nnative log: %s\n",
        glGetString(GL_RENDERER),native,adapted,same,log);
    if(!adapted){glGetShaderInfoLog(shader,sizeof(log),NULL,log);fprintf(stderr,"adapted log: %s\n",log);}
    glDeleteShader(shader);CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    free(source);free(after);return good?0:1;
}
