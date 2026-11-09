/* Can public CGL state copying query stipple without rebinding a retained PBO? */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <stdio.h>
#include <string.h>
static unsigned failures;
static void check(int good,const char *name)
{printf("%s %s\n",good?"PASS":"FAIL",name);fflush(stdout);failures+=!good;}
int main(void)
{
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,0};
    CGLPixelFormatObj pf=NULL;CGLContextObj source=NULL,query=NULL,sharing=NULL;GLint count=0;
    if(CGLChoosePixelFormat(attrs,&pf,&count)||!pf)return 77;
    if(CGLCreateContext(pf,NULL,&source)||CGLCreateContext(pf,NULL,&query)||
       CGLCreateContext(pf,source,&sharing))return 2;
    CGLDestroyPixelFormat(pf);CGLSetCurrentContext(source);
    printf("renderer=%s version=%s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    unsigned char pattern[128],actual[128];GLuint buffer=0;
    glGenBuffers(1,&buffer);glBindBuffer(GL_PIXEL_PACK_BUFFER,buffer);
    glBufferData(GL_PIXEL_PACK_BUFFER,256,NULL,GL_STATIC_DRAW);
    CGLSetCurrentContext(sharing);glDeleteBuffers(1,&buffer);CGLSetCurrentContext(source);
    check(!glIsBuffer(buffer)&&!glGetError(),"source PBO is deleted but retained");
    glPixelStorei(GL_PACK_ROW_BYTES_APPLE,9);glPixelStorei(GL_PACK_SKIP_PIXELS,7);
    for(unsigned iteration=0;iteration<3;++iteration){
        for(unsigned i=0;i<128;++i)pattern[i]=(unsigned char)(i*37+iteration*19+11);
        glPolygonStipple(pattern);
        CGLError copy=CGLCopyContext(source,query,GL_POLYGON_STIPPLE_BIT);
        printf("copy iteration=%u result=%d %s\n",iteration,copy,CGLErrorString(copy));
        check(copy==kCGLNoError,"public CGL polygon-stipple state copy");
        CGLSetCurrentContext(query);GLint binding=-1;glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);
        memset(actual,0xa5,sizeof(actual));glPixelStorei(GL_PACK_ALIGNMENT,1);glGetPolygonStipple(actual);
        check(!binding&&!glGetError()&&!memcmp(actual,pattern,sizeof(actual)),"independent query context sees current native stipple");
        CGLSetCurrentContext(source);GLint row=0,skip=0,size=0;
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&binding);glGetIntegerv(GL_PACK_ROW_BYTES_APPLE,&row);
        glGetIntegerv(GL_PACK_SKIP_PIXELS,&skip);glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER,GL_BUFFER_SIZE,&size);
        check((GLuint)binding==buffer&&row==9&&skip==7&&size==256&&!glIsBuffer(buffer)&&!glGetError(),
            "source retained PBO and client state unchanged");
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);CGLSetCurrentContext(NULL);
    CGLDestroyContext(query);CGLDestroyContext(sharing);CGLDestroyContext(source);
    printf("stipple-context-copy failures=%u\n",failures);return failures?1:0;
}
