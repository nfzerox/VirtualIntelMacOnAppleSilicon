/* Historical owned-process control for the retired image-scoped correction.
 * Only this diagnostic installs it. Production leaves the renderer untouched. */
#include <OpenGL/OpenGL.h>
#include <stdio.h>
#include <stdint.h>
#if defined(__arm64__)
static uint64_t corrections,corrected_bytes;
#define DG_METAL_SOFTWARE_STAGE_OBSERVED(bytes) do { \
    __atomic_fetch_add(&corrections,1,__ATOMIC_RELAXED); \
    __atomic_fetch_add(&corrected_bytes,(bytes),__ATOMIC_RELAXED); \
} while(0)
#endif
#include "opengl-native-metal-subroutines-diagnostic.h"
#if defined(__arm64__)
static int slot_protection(void)
{
    for(uint32_t i=0;i<_dyld_image_count();++i){
        const char *name=_dyld_get_image_name(i);struct DGMetalCopyLocation location;
        if(!name||!strstr(name,"/AppleMetalOpenGLRenderer.bundle/")||
           !DGMetalCopyLocation((const void *)_dyld_get_image_header(i),_dyld_get_image_vmaddr_slide(i),&location))continue;
        mach_vm_address_t address=(uintptr_t)location.copy_slot;mach_vm_size_t length=0;
        vm_region_basic_info_data_64_t info;mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object=MACH_PORT_NULL;
        kern_return_t error=mach_vm_region(mach_task_self(),&address,&length,VM_REGION_BASIC_INFO_64,
            (vm_region_info_t)&info,&count,&object);
        if(object!=MACH_PORT_NULL)mach_port_deallocate(mach_task_self(),object);
        return error?-1:info.protection;
    }
    return -1;
}
#endif
static CGLError create_context(CGLPixelFormatObj format,CGLContextObj share,CGLContextObj *context)
{
    CGLError error=CGLCreateContext(format,share,context);
    if(!error&&context&&*context){
#if defined(__arm64__)
        int before=slot_protection();
#endif
        fprintf(stderr,"METAL IMAGE CONTROL install=%d\n",DGPrepareNativeMetalSubroutines());
#if defined(__arm64__)
        fprintf(stderr,"METAL IMAGE CONTROL import-page-protection before=%d after=%d\n",before,slot_protection());
#endif
    }
    return error;
}
__attribute__((used,section("__DATA,__interpose")))
static const struct {const void *replacement,*original;} control={
    (const void *)(uintptr_t)&create_context,(const void *)(uintptr_t)&CGLCreateContext};
__attribute__((destructor)) static void report_control(void)
{
#if defined(__arm64__)
    fprintf(stderr,"METAL IMAGE CONTROL corrected=%llu bytes=%llu\n",
        (unsigned long long)corrections,(unsigned long long)corrected_bytes);
#endif
}
