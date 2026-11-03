/* Compare native Intel and guest CGL's real selection/description contract.
 * --require-vim-rgba32 also creates the three accepted RGBA32 contexts and
 * checks the actual selected renderer; no rendering/preferences/spoofing. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv)
{
    int require=argc==2&&!strcmp(argv[1],"--require-vim-rgba32"),failures=0,checked=0;
    if(argc>2||(argc==2&&!require))return 2;
    const int colors[]={24,32},depths[]={0,16,24,32};
    for(unsigned c=0;c<2;++c)for(unsigned d=0;d<4;++d) {
        CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFAColorSize,
            (CGLPixelFormatAttribute)colors[c],kCGLPFADepthSize,
            (CGLPixelFormatAttribute)depths[d],0};
        CGLPixelFormatObj pf=NULL;GLint count=0;
        CGLError error=CGLChoosePixelFormat(attrs,&pf,&count);
        printf("requested color=%d depth=%d error=%d count=%d",colors[c],depths[d],error,count);
        if(!error&&pf&&count) {
            GLint renderer=0,color=0,alpha=0,depth=0,accelerated=0;
            CGLDescribePixelFormat(pf,0,kCGLPFARendererID,&renderer);
            CGLDescribePixelFormat(pf,0,kCGLPFAColorSize,&color);
            CGLDescribePixelFormat(pf,0,kCGLPFAAlphaSize,&alpha);
            CGLDescribePixelFormat(pf,0,kCGLPFADepthSize,&depth);
            CGLDescribePixelFormat(pf,0,kCGLPFAAccelerated,&accelerated);
            printf(" selected renderer=%x color=%d alpha=%d depth=%d accelerated=%d",renderer,color,alpha,depth,accelerated);
            if(require&&colors[c]==32&&depths[d]<=24) {
                ++checked;
                CGLContextObj context=NULL;
                CGLError created=CGLCreateContext(pf,NULL,&context);
                GLint actual=0;
                if(!created&&context) {
                    CGLSetCurrentContext(context);
                    CGLGetParameter(context,kCGLCPCurrentRendererID,&actual);
                    printf(" actual=%x GL=%s",actual,glGetString(GL_RENDERER));
                    CGLSetCurrentContext(NULL);CGLDestroyContext(context);
                }
                if(created||!context||(actual&0x00fe7f00)!=0x00026000||color!=32||alpha!=8||!accelerated)++failures;
            }
        }
        else if(require&&colors[c]==32&&depths[d]<=24)++failures;
        puts("");if(pf)CGLDestroyPixelFormat(pf);
    }
    if(require)printf("RGBA32 hardware contexts=%d failures=%d\n",checked,failures);
    return !!(failures||(require&&checked!=3));
}
