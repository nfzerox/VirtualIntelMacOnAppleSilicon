/* Actual IOKit service lifetime, not a GL/FPS fixture. The implementation
 * under test is shared with GLI, not a reimplementation or a mocked port.
 * All operations are on our already installed research accelerator.
 */
#include "../PassthroughOpenGL/gli-accelerator-service.h"
#include "../vendor/source/VMsvga2/AC/UCMethods.h"
#include "../vendor/source/VMsvga2/vminclude/vim_passthrough_opengl.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,
        IOServiceMatching("VMsvga2Accel"));
    if(!service){fputs("Our accelerator unavailable; no lifetime qualification.\n",stderr);return 77;}
    uint64_t expected=0,actual=0;
    assert(!IORegistryEntryGetRegistryEntryID(service,&expected) && expected);
    VIMSetRendererAccelerator(service);assert(vim_renderer_accelerator==service);
    /* Reinitialization must retain the new right BEFORE releasing the old,
     * including when the task-local port names are identical. */
    VIMSetRendererAccelerator(service);assert(vim_renderer_accelerator==service);
    assert(!IOObjectRelease(service));
    assert(!IORegistryEntryGetRegistryEntryID(vim_renderer_accelerator,&actual) && actual==expected);
    io_connect_t connection=0;
    assert(!IOServiceOpen(vim_renderer_accelerator,mach_task_self(),2,&connection) && connection);
    VIMReleaseRendererAccelerator();assert(!vim_renderer_accelerator);
    VIMReleaseRendererAccelerator();assert(!vim_renderer_accelerator);
    struct VIMGLFramebufferStats stats={0};size_t size=sizeof(stats);uint64_t mode=0;
    assert(!IOConnectCallMethod(connection,kIOVM2DFramebufferBlitControl,&mode,1,
        NULL,0,NULL,NULL,&stats,&size));
    assert(size==sizeof(stats) && stats.version==1);
    assert(!IOServiceClose(connection));
    io_service_t unrelated=IOServiceGetMatchingService(kIOMasterPortDefault,
        IOServiceMatching("IOPCIDevice"));
    assert(unrelated && !IOObjectConformsTo(unrelated,"VMsvga2Accel"));
    VIMSetRendererAccelerator(unrelated);assert(!vim_renderer_accelerator);
    assert(!IOObjectRelease(unrelated));
    VIMSetRendererAccelerator(MACH_PORT_DEAD);assert(!vim_renderer_accelerator);
    VIMSetRendererAccelerator(0);assert(!vim_renderer_accelerator);
    puts("PASS actual selected-provider retain/reinitialize/caller-release, existing connection after cache release, unrelated/dead/null rejection and idempotent termination");
    return 0;
}
