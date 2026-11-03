/* Reference-only selection: CGL's actual renderer ID includes engine/screen
 * bits not present in CGLRenderers.h's family constant. Never substitute the
 * family constant for the returned ID, or silently render through NVIDIA. */
#include <stddef.h>
#include <OpenGL/CGLRenderers.h>
static GLint VIMReferenceIntelHD5000(void)
{
#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 1080
    /* This reference GPU did not exist in these SDKs. Keep ordinary guest
     * probes buildable without inventing a renderer ID for an older OS. */
    return 0;
#else
    CGLRendererInfoObj info=NULL;GLint count=0,selected=0;
    if(CGLQueryRendererInfo(~0u,&info,&count))return 0;
    for(GLint i=0;i<count;++i){
        GLint id=0,accelerated=0;
        if(!CGLDescribeRenderer(info,i,kCGLRPRendererID,&id)&&
           !CGLDescribeRenderer(info,i,kCGLRPAccelerated,&accelerated)&&accelerated&&
           (id&kCGLRendererIDMatchingMask)==kCGLRendererIntelHD5000ID)selected=id;
    }
    CGLDestroyRendererInfo(info);return selected;
#endif
}
