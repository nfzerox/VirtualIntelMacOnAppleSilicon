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
    io_string_t expected={0},actual={0};
    assert(!IORegistryEntryGetPath(service,kIOServicePlane,expected) && *expected);
    VIMSetRendererAccelerator(service);assert(vim_renderer_accelerator==service);
    /* Reinitialization must retain the new right BEFORE releasing the old,
     * including when the task-local port names are identical. */
    VIMSetRendererAccelerator(service);assert(vim_renderer_accelerator==service);
    assert(!IOObjectRelease(service));
    assert(!IORegistryEntryGetPath(vim_renderer_accelerator,kIOServicePlane,actual) && !strcmp(actual,expected));
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
    // Same provider discovery as the old per-attachment fallback, with one
    // image-lifetime right. Exercise a fresh lookup after explicit termination.
    for(unsigned pass=0;pass<2;++pass) {
        io_service_t discovered=VIMRendererAccelerator();assert(discovered);
        for(unsigned i=0;i<1000;++i)assert(VIMRendererAccelerator()==discovered);
        assert(!IORegistryEntryGetPath(discovered,kIOServicePlane,actual) && !strcmp(actual,expected));
        connection=0;
        assert(!IOServiceOpen(discovered,mach_task_self(),2,&connection) && connection);
        VIMReleaseRendererAccelerator();assert(!vim_renderer_accelerator);
        assert(!IOServiceClose(connection));
    }
    puts("PASS actual selected-provider retain/reinitialize/caller-release, existing connection after cache release, unrelated/dead/null rejection, cached discovery and rediscovery after termination");
    return 0;
}
