/* Private, same-process contract between the VMsvga2 2D companion and our
 * GLI plugin. This is not an Apple SPI or a guest/host wire ABI. The region
 * and destination follow the SDK IOBlitCopyRegion contract traced on 10.9.
 * Call only after the kernel has completed the destination surface write. */
#ifndef VIM_GLI_CPU_COPY_H
#define VIM_GLI_CPU_COPY_H
#include <stdint.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsInterfaceTypes.h>
#define VIM_GLI_CPU_COPY_SYMBOL "VIMGLINotifyCPUSurfaceCopy"
typedef IOReturn (*VIMGLICPUCopyProc)(uint32_t surface,const IOAccelDeviceRegion *region,
    int32_t destination_x,int32_t destination_y);
/* Optional pre-copy route. Zero means untouched/not eligible; a nonzero
 * result owns completion (including errors), so never repeat it on the CPU. */
#define VIM_GLI_GPU_COPY_SYMBOL "VIMGLITryGPUFramebufferCopy"
typedef uint32_t (*VIMGLIGPUCopyProc)(io_connect_t connection,uint32_t surface,
    const IOAccelDeviceRegion *region,int32_t destination_x,int32_t destination_y,IOReturn *status);
#endif
