/* Finite boot-choice probe. GL_RESOURCES is for private bringup only; installed
 * use needs neither this variable nor this diagnostic executable. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv)
{
    if(argc!=3)return 64;
    void *image=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if(!image){fprintf(stderr,"%s\n",dlerror());return 1;}
    int (*choice)(void)=dlsym(image,"VIMOpenGLBootSelection");
    int selected=choice?choice():-1;
    printf("boot-selection=%d expected=%d\n",selected,atoi(argv[2]));
    if(selected!=atoi(argv[2]))return 1;
    CGLPixelFormatAttribute attributes[]={0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attributes,&format,&count);
    if(!error)error=CGLCreateContext(format,NULL,&context);
    if(format)CGLDestroyPixelFormat(format);
    if(!error)error=CGLSetCurrentContext(context);
    printf("context-error=%d\n",error);
    if(!error)printf("renderer=%s\nversion=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    if(context){CGLSetCurrentContext(NULL);CGLDestroyContext(context);}
    return error?1:0;
}
