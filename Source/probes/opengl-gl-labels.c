/* Compare Mavericks' private accounting label ABI with the direct adapter.
 * No drawable, injection, system modification, or rendering flags required. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <unistd.h>
static int failures;
static void label(CGLContextObj c,const char *test,GLenum type,GLuint name,GLenum expected)
{
    c->disp.label_object_with_responsible_process_APPLE(c->rend,type,name,getpid());
    GLenum error=glGetError();if(error!=expected) ++failures;
    printf("%s type=%x name=%u error=%x expected=%x\n",test,type,name,error,expected);
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint n=0;
    CGLError e=CGLChoosePixelFormat(attrs,&pf,&n);
    if(!e && pf) e=CGLCreateContext(pf,NULL,&c);
    if(e || !c) { printf("context=%d\n",e);return 1; }
    CGLSetCurrentContext(c);printf("renderer=%s\n",glGetString(GL_RENDERER));
    GLuint texture=0,buffer=0,shader=glCreateShader(GL_VERTEX_SHADER),program=glCreateProgram();
    glGenTextures(1,&texture);
    // Retain the original EXT-label comparison; it has different error
    // semantics from the private responsible-process accounting operation.
    glLabelObjectEXT(GL_TEXTURE,texture,0,"reserved texture");
    GLenum error=glGetError();if(error!=GL_INVALID_OPERATION) ++failures;
    printf("reserved EXT label error=%x expected=502\n",error);
    label(c,"generated texture",GL_TEXTURE,texture,GL_INVALID_VALUE);
    glBindTexture(GL_TEXTURE_2D,texture);
    glLabelObjectEXT(GL_TEXTURE,texture,0,"bound texture");
    error=glGetError();if(error) ++failures;printf("bound EXT label error=%x expected=0\n",error);
    label(c,"bound texture",GL_TEXTURE,texture,0);
    label(c,"missing texture",GL_TEXTURE,0xffffffffU,GL_INVALID_VALUE);
    label(c,"invalid type",0xffffffffU,texture,GL_INVALID_ENUM);
    glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);
    label(c,"bound buffer",GL_BUFFER_OBJECT_EXT,buffer,0);
    label(c,"shader",GL_SHADER_OBJECT_EXT,shader,0);label(c,"program",GL_PROGRAM_OBJECT_EXT,program,0);
    glDeleteTextures(1,&texture);label(c,"deleted texture",GL_TEXTURE,texture,GL_INVALID_VALUE);
    glDeleteBuffers(1,&buffer);glDeleteShader(shader);glDeleteProgram(program);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(pf);return failures ? 1 : 0;
}
