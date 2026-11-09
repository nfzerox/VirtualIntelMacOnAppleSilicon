/* Explicit-process native diagnostic only, never part of the runtime.
 * Observe which public Metal mipmap operation Apple's GL renderer issues.
 * No texture substitution, native-driver patch, GL error consumption or
 * production swizzling. Run with DYLD_INSERT_LIBRARIES on an owned probe.
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <OpenGL/gl3.h>
#include <execinfo.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
extern void glGenerateMipmapEXT(GLenum);
extern void glCallList(GLuint);
static _Thread_local unsigned active;
static struct { Class cls; IMP original; } hooks[16];
static unsigned hook_count;
static void trace_mipmap(id object,SEL selector,id<MTLTexture> texture)
{
    IMP original=NULL;
    for(Class cls=object_getClass(object);cls&&!original;cls=class_getSuperclass(cls))
        for(unsigned i=0;i<hook_count;++i)if(hooks[i].cls==cls){original=hooks[i].original;break;}
    if(!original)abort(); /* Diagnostic invariant; never silently swallow work. */
    if(active){
        fprintf(stderr,"MIPTRACE encoder=%s format=%lu size=%lux%lu levels=%lu usage=%lu storage=%lu\n",
            object_getClassName(object),(unsigned long)texture.pixelFormat,
            (unsigned long)texture.width,(unsigned long)texture.height,
            (unsigned long)texture.mipmapLevelCount,(unsigned long)texture.usage,
            (unsigned long)texture.storageMode);
        void *frames[16];int count=backtrace(frames,16);backtrace_symbols_fd(frames,count,2);
    }
    ((void (*)(id,SEL,id<MTLTexture>))original)(object,selector,texture);
}
static void generate(GLenum target)
{
    fprintf(stderr,"MIPTRACE GL generate target=%x\n",target);
    ++active;glGenerateMipmap(target);--active;
}
static void generate_ext(GLenum target)
{
    fprintf(stderr,"MIPTRACE GL EXT generate target=%x\n",target);
    ++active;glGenerateMipmapEXT(target);--active;
}
static void subimage2d(GLenum target,GLint level,GLint x,GLint y,GLsizei width,
    GLsizei height,GLenum format,GLenum type,const void *pixels)
{
    fprintf(stderr,"MIPTRACE GL subimage2D target=%x level=%d size=%dx%d\n",target,level,width,height);
    ++active;glTexSubImage2D(target,level,x,y,width,height,format,type,pixels);--active;
}
static void call_list(GLuint list)
{
    fprintf(stderr,"MIPTRACE GL callList list=%u\n",list);
    ++active;glCallList(list);--active;
}
#define INTERPOSE(replacement,original) \
    __attribute__((used,section("__DATA,__interpose"))) \
    static const struct {const void *new_function,*old_function;} hook_##replacement= \
        {(const void *)(uintptr_t)&replacement,(const void *)(uintptr_t)&original};
INTERPOSE(generate,glGenerateMipmap)
INTERPOSE(generate_ext,glGenerateMipmapEXT)
INTERPOSE(subimage2d,glTexSubImage2D)
INTERPOSE(call_list,glCallList)
__attribute__((constructor)) static void install_trace(void)
{
    @autoreleasepool {
        /* Materialize the actual device's encoder classes before inventory. */
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLCommandBuffer> buffer=[queue commandBuffer];
        id<MTLBlitCommandEncoder> encoder=[buffer blitCommandEncoder];[encoder endEncoding];
        unsigned count=0;Class *classes=objc_copyClassList(&count);
        SEL selector=@selector(generateMipmapsForTexture:);
        for(unsigned i=0;i<count;++i){
            /* Only own methods: replacing inherited implementations twice
             * would lose the original IMP and recurse. */
            unsigned methods_count=0;Method *methods=class_copyMethodList(classes[i],&methods_count);
            for(unsigned j=0;j<methods_count;++j)if(method_getName(methods[j])==selector){
                if(hook_count==sizeof(hooks)/sizeof(*hooks))abort();
                hooks[hook_count].cls=classes[i];
                hooks[hook_count++].original=method_setImplementation(methods[j],(IMP)trace_mipmap);
                fprintf(stderr,"MIPTRACE observing class=%s\n",class_getName(classes[i]));
            }
            free(methods);
        }
        free(classes);[queue release];[device release];
    }
}
