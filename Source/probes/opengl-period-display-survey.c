/* Read-only native period-OS ABI/renderer control, built with its real SDK.
 * Select only the main display's unique online accelerated renderer BEFORE
 * context creation. This separate probe permits the explicitly supplied
 * NVIDIA Leopard reference; it does not relax the Intel reference guards.
 * Own 16x16 pbuffer only: no screen capture, system replacement or injection.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/CGLRenderers.h>
#include <OpenGL/gl.h>
#include <sys/sysctl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    GLuint mask=CGDisplayIDToOpenGLDisplayMask(CGMainDisplayID());
    CGLRendererInfoObj info=NULL;GLint count=0,chosen=0;
    CGLError error=CGLQueryRendererInfo(mask,&info,&count);
    printf("main_display_mask=%x query=%d renderers=%d pointer_bytes=%zu\n",mask,error,count,sizeof(void *));
    if(error||!mask||!info)return 1;
    for(GLint i=0;i<count;++i){
        GLint id=0,online=0,accelerated=0,display=0;
        CGLDescribeRenderer(info,i,kCGLRPRendererID,&id);
        CGLDescribeRenderer(info,i,kCGLRPOnline,&online);
        CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated);
        CGLDescribeRenderer(info,i,kCGLRPDisplayMask,&display);
        printf("renderer_id=%x online=%d accelerated=%d display_mask=%x\n",id,online,accelerated,display);
        if(online&&accelerated&&(display&mask)){
            if(chosen&&chosen!=id){CGLDestroyRendererInfo(info);return 77;}
            chosen=id;
        }
    }
    CGLDestroyRendererInfo(info);if(!chosen)return 77;
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFAPBuffer,kCGLPFARendererID,(CGLPixelFormatAttribute)chosen,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;CGLPBufferObj pbuffer=NULL;
    GLint formats=0,actual=0,bytes=0;int status=1;
    error=CGLChoosePixelFormat(attributes,&format,&formats);
    if(!error&&format){
        GLint selected=0,accelerated=0;
        error=CGLDescribePixelFormat(format,0,kCGLPFARendererID,&selected);
        if(!error)error=CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&accelerated);
        printf("selected_format_renderer=%x accelerated=%d error=%d\n",selected,accelerated,error);
        if(!error&&selected==chosen&&accelerated)error=CGLCreateContext(format,NULL,&context);
    }
    if(format)CGLDestroyPixelFormat(format);
    if(!error&&context)error=CGLSetCurrentContext(context);
    if(!context||error)goto out;
    CGLGetParameter(context,kCGLCPCurrentRendererID,&actual);
    error=CGLGetParameter(context,kCGLCPDispatchTableSize,&bytes);
    printf("vendor=%s renderer=%s version=%s actual_id=%x\n",
        glGetString(GL_VENDOR),glGetString(GL_RENDERER),glGetString(GL_VERSION),actual);
    printf("dispatch_query=%d native_bytes=%d sdk_bytes=%zu cgl_context_bytes=%zu offsets=%zu,%zu,%zu,%zu\n",
        error,bytes,sizeof(GLIFunctionDispatch),sizeof(struct _CGLContextObject),
        offsetof(struct _CGLContextObject,rend),offsetof(struct _CGLContextObject,disp),
        offsetof(struct _CGLContextObject,priv),offsetof(struct _CGLContextObject,stak));
    /* Leopard reports a different raw context ID from the public renderer
     * record. Do not invent a later SDK mask: verify the actual selected
     * accelerated format above and report the raw context value separately. */
    if(error||bytes<=0||bytes%(int)sizeof(void *))goto out;
    error=CGLCreatePBuffer(16,16,GL_TEXTURE_2D,GL_RGBA,0,&pbuffer);
    if(!error)error=CGLSetPBuffer(context,pbuffer,0,0,0);
    if(error)goto out;
    glViewport(0,0,16,16);glClearColor(.25f,.5f,.75f,1);glClear(GL_COLOR_BUFFER_BIT);
    unsigned char rgba[4]={0};glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    GLenum gl_error=glGetError();
    status=gl_error||abs((int)rgba[0]-64)>1||abs((int)rgba[1]-128)>1||
        abs((int)rgba[2]-191)>1||rgba[3]!=255;
    printf("pbuffer_rgba=%u,%u,%u,%u gl_error=%x passed=%d\n",rgba[0],rgba[1],rgba[2],rgba[3],gl_error,!status);
out:
    printf("cgl_error=%d\n",error);
    CGLSetCurrentContext(NULL);if(context)CGLDestroyContext(context);
    if(pbuffer)CGLDestroyPBuffer(pbuffer);
    return status;
}
