/* Native IOAccelGLDrawable wire boundary, measured from the working guest's
 * IOAccelerator and GLDRendererMetal. Unlike its CF wrapper, these IOKit calls
 * are usable by both Mojave client architectures without Metal or a broker.
 * This is guest-side ABI glue, not a host driver hook or new GPU protocol. */
#ifndef VIM_PVG_DRAWABLE_H
#define VIM_PVG_DRAWABLE_H
#include <IOKit/IOKitLib.h>
#include <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <stdint.h>
#include <string.h>
#define VIM_PVG_SURFACE2 0x4000u
struct VIMPVGDrawableInput {
    uint64_t mode;
    uint32_t surface,depth,face,level,volatile_state,fixed_source,scale,width,height,padding;
};
struct VIMPVGDrawableOutput {
    uint64_t mode;
    uint32_t width,height,surface_width,surface_height;
    uint8_t private_result[16];
};
_Static_assert(sizeof(struct VIMPVGDrawableInput)==48,"Native drawable request in both ABIs");
_Static_assert(sizeof(struct VIMPVGDrawableOutput)==40,"Native drawable reply in both ABIs");
static IOReturn VIMPVGDrawableOpen(io_service_t service,io_connect_t *connection)
{
    *connection=0;
    // Actual IOAccelGLDrawableCreate opens this provider with type9. Its
    // CF wrapper only stores a connection and device reference afterwards.
    return IOServiceOpen(service,mach_task_self(),9,connection);
}
static IOReturn VIMPVGDrawableSet(io_connect_t connection,uint32_t surface,int double_buffer,
    struct VIMPVGDrawableOutput *output)
{
    struct VIMPVGDrawableInput input={.mode=double_buffer?0xc00:0x800,.surface=surface,.depth=4};
    size_t bytes=sizeof(*output);memset(output,0,sizeof(*output));
    IOReturn result=IOConnectCallStructMethod(connection,1,&input,sizeof(input),output,&bytes);
    return !result&&bytes!=sizeof(*output)?kIOReturnIOError:result;
}
static IOReturn VIMPVGDrawableLookup(io_connect_t connection,uint32_t index,IOSurfaceRef *surface)
{
    *surface=NULL;uint64_t input=index,port=0;uint32_t count=1;
    IOReturn result=IOConnectCallScalarMethod(connection,5,&input,1,&port,&count);
    if(result)return result;
    if(count!=1||!port||port>UINT32_MAX)return kIOReturnIOError;
    *surface=IOSurfaceLookupFromMachPort((mach_port_t)port);
    mach_port_deallocate(mach_task_self(),(mach_port_t)port);
    return *surface?kIOReturnSuccess:kIOReturnNotFound;
}
static void VIMPVGDrawableClose(io_connect_t connection)
{
    if(!connection)return;
    struct VIMPVGDrawableInput clear={0};
    IOConnectCallStructMethod(connection,0,&clear,sizeof(clear),NULL,NULL);
    IOServiceClose(connection);
}
#endif
