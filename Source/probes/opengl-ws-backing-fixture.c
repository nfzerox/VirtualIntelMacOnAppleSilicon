/* Explicit LLDB-only lab fixture, NEVER installed/injected at normal startup.
 * VMsvga2 permits only the actual WindowServer task to create surfaces. Use
 * that legitimate owner to register a separate, unpresented client-RAM surface
 * through its real IOAccel ABI; never borrow a user's desktop/window pixels.
 * No worker, polling, launch environment change or renderer replacement. */
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/graphics/IOAccelSurfaceConnect.h>
#include <mach/mach_vm.h>
#include <mach/mach.h>
#include <stdlib.h>
#include <string.h>
static io_service_t fixture_accel;
static io_connect_t fixture_source;
static mach_vm_address_t fixture_memory;
static mach_port_t fixture_entry;
static uint32_t fixture_id;
enum {Width=61,Height=37,Pitch=256,Bytes=12288};
static IOReturn shape(void)
{
    IOAccelDeviceRegion region={0};region.bounds=(IOAccelBounds){0,0,Width,Height};
    uint64_t args[]={kIOAccelSurfaceShapeIdentityScaleBit,0,fixture_memory,Pitch,Bytes};
    return IOConnectCallMethod(fixture_source,kIOAccelSurfaceSetShapeBackingAndLength,args,5,
        &region,sizeof(region),NULL,NULL,NULL,NULL);
}
static int in_use(uint32_t candidate)
{
    io_iterator_t iterator=0;
    if(IORegistryEntryCreateIterator(fixture_accel,kIOServicePlane,kIORegistryIterateRecursively,&iterator))return 1;
    io_registry_entry_t entry;int found=0;
    while((entry=IOIteratorNext(iterator))){
        CFTypeRef value=IORegistryEntryCreateCFProperty(entry,CFSTR("CGSSurfaceID"),kCFAllocatorDefault,0);
        int32_t number=0;
        if(value && CFGetTypeID(value)==CFNumberGetTypeID() && CFNumberGetValue(value,kCFNumberSInt32Type,&number) &&
            (uint32_t)number==candidate)found=1;
        if(value)CFRelease(value);IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);return found;
}
__attribute__((visibility("default"))) int VIMWSBackingFixtureDestroy(void)
{
    if(strcmp(getprogname(),"WindowServer"))return (int)kIOReturnNotPrivileged;
    IOReturn error=fixture_source?IOServiceClose(fixture_source):kIOReturnSuccess;
    if(error)return (int)error; // do not permit CPU reuse after failed retirement
    fixture_source=0;fixture_id=0;
    if(fixture_memory){mach_vm_deallocate(mach_task_self(),fixture_memory,Bytes);fixture_memory=0;}
    if(fixture_entry){mach_port_deallocate(mach_task_self(),fixture_entry);fixture_entry=0;}
    if(fixture_accel){IOObjectRelease(fixture_accel);fixture_accel=0;}
    return 0;
}
__attribute__((visibility("default"))) uint32_t VIMWSBackingFixtureCreate(void)
{
    if(strcmp(getprogname(),"WindowServer"))return 0;
    if(fixture_source)return fixture_id;
    fixture_accel=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2Accel"));
    if(!fixture_accel || IOServiceOpen(fixture_accel,mach_task_self(),kIOAccelSurfaceClientType,&fixture_source))goto fail;
    for(unsigned attempt=0;attempt<32;++attempt){
        fixture_id=0x70000000u|arc4random_uniform(0x1000000u);
        if(!in_use(fixture_id))break;fixture_id=0;
    }
    if(!fixture_id)goto fail;
    uint64_t mode[]={fixture_id,kIOAccelSurfaceModeWindowedBit|kIOAccelSurfaceModeColorDepthBGRA32};
    if(IOConnectCallScalarMethod(fixture_source,kIOAccelSurfaceSetIDMode,mode,2,NULL,NULL))goto fail;
    memory_object_size_t entry_size=Bytes;
    if(mach_make_memory_entry_64(mach_task_self(),&entry_size,0,
        VM_PROT_READ|VM_PROT_WRITE|MAP_MEM_NAMED_CREATE,&fixture_entry,0))goto fail;
    if(mach_vm_map(mach_task_self(),&fixture_memory,Bytes,0,VM_FLAGS_ANYWHERE,fixture_entry,0,FALSE,
        VM_PROT_READ|VM_PROT_WRITE,VM_PROT_READ|VM_PROT_WRITE,VM_INHERIT_NONE))goto fail;
    for(unsigned i=0;i<Bytes/4;++i)((uint32_t*)(uintptr_t)fixture_memory)[i]=0xff123456u;
    if(shape())goto fail;
    return fixture_id;
fail:
    VIMWSBackingFixtureDestroy();return 0;
}
__attribute__((visibility("default"))) int VIMWSBackingFixtureTryCPU(unsigned operation)
{
    if(strcmp(getprogname(),"WindowServer") || !fixture_source)return (int)kIOReturnNotOpen;
    if(operation==2)return (int)shape();
    if(operation>2)return (int)kIOReturnBadArgument;
    IOAccelSurfaceInformation info={0};size_t size=sizeof(info);uint64_t options=0;
    IOReturn result=IOConnectCallMethod(fixture_source,
        operation?kIOAccelSurfaceWriteLockOptions:kIOAccelSurfaceReadLockOptions,
        &options,1,NULL,0,NULL,NULL,&info,&size);
    if(!result)IOConnectCallScalarMethod(fixture_source,
        operation?kIOAccelSurfaceWriteUnlockOptions:kIOAccelSurfaceReadUnlockOptions,&options,1,NULL,NULL);
    return (int)result;
}
