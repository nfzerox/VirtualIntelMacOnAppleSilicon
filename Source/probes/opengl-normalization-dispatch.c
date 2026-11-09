/* Read the period SDK's named CGL dispatch fields in our own context.
 * No guessed offsets, injection, driver writes or another process's memory.
 * Observe direct/compiled routing before disassembling the actual image.
 */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLContext.h>
#include <dlfcn.h>
#include <stdio.h>
static void entry(const char *phase,const char *name,const void *function)
{
    Dl_info info={0};dladdr(function,&info);
    printf("DISPATCH %s %s function=%p image=%s symbol=%s offset=%lx code=",phase,name,function,
        info.dli_fname?info.dli_fname:"none",info.dli_sname?info.dli_sname:"none",
        (unsigned long)((const unsigned char *)function-(const unsigned char *)info.dli_fbase));
    if(function)for(unsigned i=0;i<32;++i)printf("%02x",((const unsigned char *)function)[i]);putchar('\n');
}
static void observe(CGLContextObj context,const char *phase)
{
#define ENTRY(field) entry(phase,#field,(const void *)context->disp.field)
    ENTRY(normal3b);ENTRY(normal3bv);ENTRY(normal3s);ENTRY(normal3sv);ENTRY(normal3fv);
    ENTRY(color4bv);ENTRY(vertex_attrib4Nsv_ARB);
#undef ENTRY
}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint n=0;
    if(CGLChoosePixelFormat(attrs,&format,&n)||!format)return 77;
    if(CGLCreateContext(format,NULL,&context)||CGLSetCurrentContext(context))return 2;
    CGLDestroyPixelFormat(format);
    printf("renderer=%s version=%s bits=%u\n",glGetString(GL_RENDERER),glGetString(GL_VERSION),(unsigned)sizeof(void *)*8);
    GLbyte b[]={0,0,0};GLshort s[]={0,0,0};
    glNormal3b(0,0,0);glNormal3bv(b);glNormal3s(0,0,0);glNormal3sv(s);
    observe(context,"execute");GLuint list=glGenLists(1);if(!list)return 2;
    glNewList(list,GL_COMPILE);observe(context,"compile");
    glNormal3b(0,0,0);glNormal3bv(b);glNormal3s(0,0,0);glNormal3sv(s);
    observe(context,"compile-after-set");glEndList();observe(context,"after-list");
    glDeleteLists(list,1);GLenum error=glGetError();
    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
    printf("dispatch-trace error=%x\n",(unsigned)error);return error?1:0;
}
