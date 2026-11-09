/* Explicit-process CGL selector for owned qualification executables only.
 * A live mux state does not prove which GPU a new GL context will choose.
 * Preserve non-renderer attributes and let CGL choose the actual format.
 * The executable's renderer log is still the device identity oracle. */
#include <OpenGL/OpenGL.h>
#include <stdint.h>
#include <stdio.h>
#include "opengl-reference-renderer.h"

static int has_value(CGLPixelFormatAttribute attribute)
{
    switch(attribute){
    case kCGLPFAAuxBuffers:case kCGLPFAColorSize:case kCGLPFAAlphaSize:
    case kCGLPFADepthSize:case kCGLPFAStencilSize:case kCGLPFAAccumSize:
    case kCGLPFASampleBuffers:case kCGLPFASamples:case kCGLPFARendererID:
    case kCGLPFADisplayMask:case kCGLPFAOpenGLProfile:return 1;
    default:return 0;
    }
}

static CGLError choose_reference(const CGLPixelFormatAttribute *attributes,
                                 CGLPixelFormatObj *format,GLint *count)
{
    GLint renderer=VIMReferenceIntelHD5000();
    if(!renderer){fputs("Intel control: accelerated Haswell renderer unavailable\n",stderr);return kCGLBadMatch;}
    CGLPixelFormatAttribute selected[128]={kCGLPFARendererID,(CGLPixelFormatAttribute)renderer,
        kCGLPFAAllowOfflineRenderers,kCGLPFASupportsAutomaticGraphicsSwitching};
    size_t used=4;
    if(attributes)for(size_t i=0;attributes[i];++i){
        CGLPixelFormatAttribute key=attributes[i];
        if(key==kCGLPFARendererID){++i;continue;}
        if(key==kCGLPFAAllRenderers || key==kCGLPFAAllowOfflineRenderers ||
           key==kCGLPFASupportsAutomaticGraphicsSwitching)continue;
        int value=has_value(key);
        if(used+value+1>=sizeof(selected)/sizeof(selected[0]))return kCGLBadAttribute;
        selected[used++]=key;
        if(value)selected[used++]=attributes[++i];
    }
    selected[used]=0;
    return CGLChoosePixelFormat(selected,format,count);
}

__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} control={
    (const void *)(uintptr_t)&choose_reference,(const void *)(uintptr_t)&CGLChoosePixelFormat};
