/* Finite, own-process Tiger Intel record survey; no installation/injection.
 * The actual8R3032 Intel gldChoosePixelFormat allocates52-byte records;
 * native gliQueryRendererInfo allocates56-byte records. Signatures and
 * lengths were checked in those native functions before this probe. */
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <OpenGL/CGLRenderers.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

static unsigned dump(const char *name,void *record,unsigned bytes)
{
    unsigned count=0;
    while(record && count<8){
        printf("%s[%u]",name,count);
        for(unsigned offset=0;offset<bytes;offset+=4){
            uint32_t value;memcpy(&value,(char *)record+offset,4);
            printf(" %02x:%08x",offset,(unsigned)value);
        }
        puts("");memcpy(&record,record,sizeof(record));++count;
    }
    return record?0:count;
}
int main(void)
{
    setvbuf(stdout,NULL,_IOLBF,0);
    CGLPixelFormatAttribute attrs[]={kCGLPFAAccelerated,kCGLPFANoRecovery,
        kCGLPFARendererID,(CGLPixelFormatAttribute)kCGLRendererIntel900ID,0};
    CGLPixelFormatObj format=NULL;CGLContextObj context=NULL;GLint count=0,id=0,mask=0,accelerated=0;
    int result=1;void *engine=NULL,*records=NULL,*renderers=NULL;
    CGLError error=CGLChoosePixelFormat(attrs,&format,&count);
    if(!error && format)error=CGLDescribePixelFormat(format,0,kCGLPFAAccelerated,&accelerated);
    if(!error && accelerated)error=CGLCreateContext(format,NULL,&context);
    if(!error && context)error=CGLSetCurrentContext(context);
    const char *renderer=context?(const char *)glGetString(GL_RENDERER):NULL;
    printf("context=%d accelerated=%d renderer=%s\n",error,(int)accelerated,renderer?renderer:"none");
    if(error || !context || !renderer || !strstr(renderer,"Intel"))goto done;
    error=CGLDescribePixelFormat(format,0,kCGLPFARendererID,&id);
    if(!error)error=CGLDescribePixelFormat(format,0,kCGLPFADisplayMask,&mask);
    if(error)goto done;
    printf("public renderer_id=%08x display_mask=%08x\n",(unsigned)id,(unsigned)mask);
    engine=dlopen("/System/Library/Frameworks/OpenGL.framework/Resources/GLEngine.bundle/GLEngine",RTLD_NOW|RTLD_LOCAL);
    if(!engine){puts(dlerror());goto done;}
    int (*choose)(void **,const GLint *)=dlsym(engine,"gliChoosePixelFormat");
    int (*destroy)(void *)=dlsym(engine,"gliDestroyPixelFormat");
    int (*query)(void **,unsigned)=dlsym(engine,"gliQueryRendererInfo");
    int (*destroy_info)(void *)=dlsym(engine,"gliDestroyRendererInfo");
    if(!choose || !destroy || !query || !destroy_info)goto done;
    /* Native Intel950's actual GLI chooser rejects public RendererID70.
     * CGL handles renderer filtering above this seam. Survey returned native
     * records separately; do not pass a public-only selector into GLI. */
    GLint raw[]={4,kCGLPFAColorSize,24,kCGLPFAAlphaSize,8,
        kCGLPFADepthSize,24,kCGLPFAStencilSize,8,0};
    int chosen=choose(&records,raw);
    printf("choose=%d\n",chosen);
    unsigned formats=records?dump("format",records,52):0;
    if(records){destroy(records);records=NULL;}
    int queried=query(&renderers,(unsigned)mask);
    printf("query=%d\n",queried);
    unsigned infos=renderers?dump("renderer",renderers,56):0;
    if(renderers){destroy_info(renderers);renderers=NULL;}
    result=chosen || queried || !formats || !infos;
done:
    CGLSetCurrentContext(NULL);
    if(context)CGLDestroyContext(context);
    if(format)CGLDestroyPixelFormat(format);
    if(engine)dlclose(engine);
    return result;
}
