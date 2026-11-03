/* Own-context native Tiger dispatch-route observation; no table mutation. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <dlfcn.h>
#include <stdio.h>
static void Route(const char *name,void *address)
{
    Dl_info info={0};dladdr(address,&info);
    printf("%s=%p image=%s symbol=%s\n",name,address,info.dli_fname?info.dli_fname:"?",info.dli_sname?info.dli_sname:"?");
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
    CGLError error=CGLChoosePixelFormat(attrs,&pf,&count);
    if(!error)error=CGLCreateContext(pf,NULL,&c);
    if(!error)error=CGLSetCurrentContext(c);
    if(error){printf("CGL error=%d\n",error);return 1;}
    Route("bind_texture",(void *)c->disp.bind_texture);
    Route("gen_textures",(void *)c->disp.gen_textures);
    Route("get_integerv",(void *)c->disp.get_integerv);
    GLint rectangle=-1,two=-1;
    c->disp.get_integerv(c->rend,GL_TEXTURE_BINDING_RECTANGLE_ARB,&rectangle);
    c->disp.get_integerv(c->rend,GL_TEXTURE_BINDING_2D,&two);
    printf("initial rectangle=%ld two=%ld error=%lx\n",rectangle,two,glGetError());
    CGLSetCurrentContext(NULL);CGLDestroyContext(c);CGLDestroyPixelFormat(pf);return 0;
}
