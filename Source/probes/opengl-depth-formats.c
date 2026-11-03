/* Compare the actual accelerated chooser/depth precision with the physical
 * period Intel reference. Never infer a 32-bit request means 24-bit storage. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    (void)argv;
    for(unsigned i=0;i<4;++i){
        int bits=(int[]){0,16,24,32}[i];
        CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,kCGLPFASupportsAutomaticGraphicsSwitching,kCGLPFAColorSize,32,kCGLPFADepthSize,bits,
            argc>1?kCGLPFAMinimumPolicy:kCGLPFAClosestPolicy,0};
        CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0,depth=-1;
        CGLError e=CGLChoosePixelFormat(attrs,&pf,&count);
        if(!e&&pf)e=CGLDescribePixelFormat(pf,0,kCGLPFADepthSize,&depth);
        if(!e&&pf)e=CGLCreateContext(pf,NULL,&c);
        if(c)CGLSetCurrentContext(c);
        const char *renderer=c?(const char *)glGetString(GL_RENDERER):"none";
        printf("request=%d count=%d selected=%d error=%d renderer=%s\n",bits,count,depth,e,renderer);
        if(c&&!strstr(renderer,"Intel"))return 77;
        CGLSetCurrentContext(NULL);if(c)CGLDestroyContext(c);if(pf)CGLDestroyPixelFormat(pf);
    }
    return 0;
}
