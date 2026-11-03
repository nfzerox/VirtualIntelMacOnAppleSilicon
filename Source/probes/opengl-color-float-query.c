/* Public CGL/APPLE_float_pixels scalar contract; own context, no window. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
int main(void)
{
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    if(!error)error=CGLCreateContext(format,NULL,&context);
    if(!error)error=CGLSetCurrentContext(context);
    int result=1;
    if(!error){
        GLboolean boolean=2;GLint integer=-1;
        glGetBooleanv(GL_COLOR_FLOAT_APPLE,&boolean);GLenum first=glGetError();
        glGetIntegerv(GL_COLOR_FLOAT_APPLE,&integer);GLenum second=glGetError();
        printf("renderer=%s color_float_boolean=%u error=%x integer=%d error=%x\n",
            glGetString(GL_RENDERER),(unsigned)boolean,(unsigned)first,(int)integer,(unsigned)second);
        result=first||second||(boolean!=GL_FALSE&&boolean!=GL_TRUE)||integer!=(GLint)boolean;
    }else printf("CGL error=%d\n",(int)error);
    CGLSetCurrentContext(NULL);if(context)CGLDestroyContext(context);if(format)CGLDestroyPixelFormat(format);
    return result;
}
