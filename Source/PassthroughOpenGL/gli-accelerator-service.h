/* CGL supplies the selected accelerator at library initialization. Keep our
 * own send right, not the caller's array or a cached CGS surface/connection.
 * Library init/termination is serialized by CGL; each drawable still opens,
 * binds, queries and closes its own endpoint with the original lifetime.
 * A direct test caller with no matching device keeps the old discovery path.
 */
#ifndef VIM_GLI_ACCELERATOR_SERVICE_H
#define VIM_GLI_ACCELERATOR_SERVICE_H
#include <IOKit/IOKitLib.h>
#include "iokit-bridge-compat.h"
static io_service_t vim_renderer_accelerator;
static void VIMSetRendererAccelerator(io_service_t service)
{
    io_service_t next=0;
    if(service && VIMIOCanRetainObject() && VIMIOObjectConformsTo(service,"VMsvga2Accel") &&
       IOObjectRetain(service)==kIOReturnSuccess)next=service;
    io_service_t previous=vim_renderer_accelerator;
    vim_renderer_accelerator=next;
    if(previous)IOObjectRelease(previous);
}
__attribute__((destructor)) static void VIMReleaseRendererAccelerator(void)
{VIMSetRendererAccelerator(0);}
#endif
