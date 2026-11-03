/* Public CGL selection diagnostic on the unmodified physical oracle. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdio.h>
int main(void)
{
    CGLRendererInfoObj info=NULL;GLint n=0;
    if(CGLQueryRendererInfo(~0U,&info,&n))return 2;
    for(GLint i=0;i<n;++i){
        GLint id=0,accelerated=0;CGLDescribeRenderer(info,i,kCGLRPRendererID,&id);
        CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated);if(!accelerated)continue;
        for(unsigned mode=0;mode<4;++mode){
            CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFAAllowOfflineRenderers,
                kCGLPFAOpenGLProfile,kCGLOGLPVersion_Legacy,kCGLPFARendererID,
                (CGLPixelFormatAttribute)(mode&1?id&0x00fe7f00:id),
                mode&2?kCGLPFASupportsAutomaticGraphicsSwitching:0,0};
            CGLPixelFormatObj pf=NULL;CGLContextObj c=NULL;GLint count=0;
            CGLError choose=CGLChoosePixelFormat(attrs,&pf,&count),create=choose;
            if(!choose && pf)create=CGLCreateContext(pf,NULL,&c);
            if(pf)CGLDestroyPixelFormat(pf);
            if(c)CGLSetCurrentContext(c);
            printf("id=0x%x mode=%u choose=%d count=%d create=%d renderer=%s\n",id,mode,choose,count,create,c?(const char *)glGetString(GL_RENDERER):"none");
            if(c){CGLSetCurrentContext(NULL);CGLDestroyContext(c);}
        }
    }
    CGLDestroyRendererInfo(info);return 0;
}
