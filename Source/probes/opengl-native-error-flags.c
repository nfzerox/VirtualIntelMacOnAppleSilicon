/* Actual native Intel error retention, no driver injection or GL emulation. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    CGLPixelFormatAttribute a[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFASupportsAutomaticGraphicsSwitching,0};
    CGLPixelFormatObj format=NULL;CGLContextObj c=NULL;GLint count=0;
    if(CGLChoosePixelFormat(a,&format,&count) || !format)return 2;
    CGLError error=CGLCreateContext(format,NULL,&c);CGLDestroyPixelFormat(format);
    if(error || !c || CGLSetCurrentContext(c))return 2;
    const char *renderer=(const char *)glGetString(GL_RENDERER);
    printf("renderer=%s\n",renderer?renderer:"none");
    if(!renderer || !strstr(renderer,"Intel"))return 2;
    unsigned drain=0;while(glGetError() && ++drain<16){}
    if(drain==16)return 2;
    glEnable(0xffffffffu);glViewport(0,0,-1,20);
    GLenum first=glGetError(),second=glGetError(),third=glGetError();
    printf("native-error-flags=%x,%x,%x\n",first,second,third);
    int good=(first==GL_INVALID_ENUM && (second==GL_INVALID_VALUE || !second)) ||
        (first==GL_INVALID_VALUE && second==GL_INVALID_ENUM);
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);
    return good && !third ? 0 : 1;
}
