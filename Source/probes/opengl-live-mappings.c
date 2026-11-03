/* Maps keeps >256 small buffer mappings live simultaneously.
 * Exercise native allocation, guest pointer ownership and real publication. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAOpenGLProfile,
        (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&n);
    if(!e&&pf)e=CGLCreateContext(pf,NULL,&c);
    if(pf)CGLDestroyPixelFormat(pf);if(e||!c)return 1;
    CGLSetCurrentContext(c);
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s version=%s\n",renderer,glGetString(GL_VERSION));
    if(!renderer||!strstr(renderer,"Intel"))return 77;
    enum { count=512,bytes=4096 };GLuint names[count];unsigned char *pointers[count];
    memset(pointers,0,sizeof(pointers));glGenBuffers(count,names);
    unsigned created=0,verified=0;GLenum error=GL_NO_ERROR;
    for(unsigned i=0;i<count;i++){
        glBindBuffer(GL_ARRAY_BUFFER,names[i]);glBufferData(GL_ARRAY_BUFFER,bytes,NULL,GL_STATIC_DRAW);
        pointers[i]=glMapBufferRange(GL_ARRAY_BUFFER,0,bytes,
            GL_MAP_WRITE_BIT|GL_MAP_FLUSH_EXPLICIT_BIT|GL_MAP_UNSYNCHRONIZED_BIT);
        error=glGetError();if(!pointers[i]||error)break;
        memset(pointers[i],(unsigned char)(i*37+11),bytes);created++;
    }
    printf("live-mappings=%u requested=%u bytes=%u error=%x\n",created,count,created*bytes,error);
    for(unsigned i=0;i<created;i++){
        glBindBuffer(GL_ARRAY_BUFFER,names[i]);
        glFlushMappedBufferRange(GL_ARRAY_BUFFER,0,bytes);
        if(!glUnmapBuffer(GL_ARRAY_BUFFER)||glGetError())continue;
        unsigned char actual[bytes];glGetBufferSubData(GL_ARRAY_BUFFER,0,bytes,actual);
        int good=!glGetError();for(unsigned j=0;j<bytes&&good;j++)good=actual[j]==(unsigned char)(i*37+11);
        verified+=good;
    }
    glDeleteBuffers(count,names);int good=created==count&&verified==count&&!glGetError();
    printf("%s published mappings=%u\n",good?"PASS":"FAIL",verified);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);return good?0:1;
}
