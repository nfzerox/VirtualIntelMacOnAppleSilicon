/* Genuine Tiger SDK/public CGL, isolated Intel context, no foreign memory.
 * gliGetVersion's three GLint outputs are checked in the actual loose engine
 * disassembly before this probe is run. No renderer/context offsets invoked. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/CGLRenderers.h>
#include <stddef.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
int main(void){
    CGLPixelFormatAttribute attributes[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFARendererID,(CGLPixelFormatAttribute)kCGLRendererIntel900ID,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint formats=0,bytes=0,accelerated=0;
    void *engine=NULL;int result=1;CGLError error=CGLChoosePixelFormat(attributes,&format,&formats);
    if(!error&&format)error=CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&accelerated);
    if(!error&&accelerated)error=CGLCreateContext(format,NULL,&context);
    if(!error&&context)error=CGLSetCurrentContext(context);
    const char *renderer=context?(const char *)glGetString(GL_RENDERER):NULL;
    printf("context=%d accelerated=%d bits=%u renderer=%s\n",(int)error,(int)accelerated,(unsigned)(8*sizeof(void*)),renderer?renderer:"none");
    if(error||!context||!renderer||!strstr(renderer,"Intel"))goto done;
    error=CGLGetParameter(context,kCGLCPDispatchTableSize,&bytes);
    printf("dispatch_error=%d runtime_bytes=%d SDK_bytes=%u SDK_entries=%u SDK_private_offset=%x\n",
        (int)error,(int)bytes,(unsigned)sizeof(GLIFunctionDispatch),
        (unsigned)(sizeof(GLIFunctionDispatch)/sizeof(void*)),(unsigned)offsetof(struct _CGLContextObject,priv));
    if(error||bytes<=0||bytes%sizeof(void*))goto done;
    engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    if(!engine){puts(dlerror());goto done;}
    unsigned char (*version)(GLint*,GLint*,GLint*)=dlsym(engine,"gliGetVersion");
    GLint major=0,minor=0,implementation=0;
    int accepted=version&&version(&major,&minor,&implementation);
    printf("native_engine_accepted=%d ABI=%d.%d implementation=%x\n",accepted,(int)major,(int)minor,(unsigned)implementation);
    result=accepted?0:1;
done:
    CGLSetCurrentContext(NULL);if(context)CGLDestroyContext(context);if(format)CGLDestroyPixelFormat(format);
    if(engine)dlclose(engine);return result;
}
