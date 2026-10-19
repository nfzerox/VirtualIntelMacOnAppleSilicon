// Mojave-only Surface2 compatibility export. Reexport native IOKit for all
// other symbols. No interposition, global injection, or kernel patching.
#include <IOKit/IOKitLib.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <stdint.h>

typedef IOReturn (*CreateSurface)(io_service_t,uint32_t,uint32_t,void **);
IOReturn IOAccelCreateSurface(io_service_t service,uint32_t surfaceID,
                             uint32_t mode,void **out) {
    if(!out)return kIOReturnBadArgument;
    *out=NULL;
    if(!(mode&0x4000)||!IOObjectConformsTo(service,"AppleParavirtAccelerator")) {
        static CreateSurface native;
        static dispatch_once_t once;
        dispatch_once(&once, ^{
            void *h=dlopen("/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit",RTLD_NOW|RTLD_LOCAL);
            if(h)native=(CreateSurface)dlsym(h,"IOAccelCreateSurface");
        });
        return native?native(service,surfaceID,mode,out):kIOReturnUnsupported;
    }
    // Verified Catalina/Big Sur helper: type32 selects IOAccelSurfaceMTL;
    // method7 takes the same surface ID / mode scalars as Mojave's helper.
    io_connect_t connection=IO_OBJECT_NULL;
    IOReturn status=IOServiceOpen(service,mach_task_self(),32,&connection);
    if(status!=kIOReturnSuccess)return status;
    uint64_t input[2]={surfaceID,mode};
    status=IOConnectCallScalarMethod(connection,7,input,2,NULL,NULL);
    if(status!=kIOReturnSuccess){IOServiceClose(connection);return status;}
    *out=(void *)(uintptr_t)connection;
    return kIOReturnSuccess;
}
