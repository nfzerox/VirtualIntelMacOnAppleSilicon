/* Explicit-process research only, NOT production. The native interpreter
 * control is incomplete because inlined JIT copies bypass its method guard.
 * Guest shader lowering is the preferred implementation direction. */
#include <OpenGL/OpenGL.h>
#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>
#if defined(__arm64__)
static uint64_t corrected_calls[2],forwarded_calls[2];
static void observe_forward(unsigned kind,uintptr_t caller)
{
    if(!__atomic_fetch_add(forwarded_calls+kind,1,__ATOMIC_RELAXED)){
        Dl_info info={0};dladdr((void *)caller,&info);
        fprintf(stderr,"GLVM MATH CONTROL forward=%s caller=%s+%lu\n",kind?"ceil":"floor",
            info.dli_sname?info.dli_sname:"unknown",(unsigned long)(caller-(uintptr_t)info.dli_saddr));
    }
}
#define DG_GLVM_ROUND_OBSERVED(kind) __atomic_fetch_add(corrected_calls+(kind),1,__ATOMIC_RELAXED)
#define DG_GLVM_FORWARD_OBSERVED(kind,caller) observe_forward(kind,caller)
#endif
#include "opengl-glvm-math-candidate.c"
static CGLError create_context(CGLPixelFormatObj format,CGLContextObj share,CGLContextObj *context)
{
    CGLError error=CGLCreateContext(format,share,context);
    if(!error&&context&&*context)fprintf(stderr,"GLVM MATH CONTROL install=%d\n",DGPrepareNativeGLVMRoundEven());
    return error;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} control={
    (const void *)(uintptr_t)&create_context,(const void *)(uintptr_t)&CGLCreateContext};
__attribute__((destructor)) static void report_control(void)
{
#if defined(__arm64__)
    fprintf(stderr,"GLVM MATH CONTROL corrected floor=%llu ceil=%llu forwarded floor=%llu ceil=%llu\n",
        (unsigned long long)corrected_calls[0],(unsigned long long)corrected_calls[1],
        (unsigned long long)forwarded_calls[0],(unsigned long long)forwarded_calls[1]);
#endif
}
