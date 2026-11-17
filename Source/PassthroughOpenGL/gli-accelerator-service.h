/* CGL supplies the selected accelerator at library initialization. Keep our
 * own send right, not the caller's array or a cached CGS surface/connection.
 * Library init/termination is serialized by CGL; each drawable still opens,
 * binds, queries and closes its own endpoint with the original lifetime.
 * When CGL supplies no usable service, retain the same provider the existing
 * discovery path finds, rather than enumerating the registry per attachment.
 */
#ifndef VIM_GLI_ACCELERATOR_SERVICE_H
#define VIM_GLI_ACCELERATOR_SERVICE_H
#include <IOKit/IOKitLib.h>
#include <pthread.h>
#include "iokit-bridge-compat.h"
static io_service_t vim_renderer_accelerator;
static pthread_mutex_t vim_renderer_accelerator_lock=PTHREAD_MUTEX_INITIALIZER;
static void VIMSetRendererAccelerator(io_service_t service)
{
    io_service_t next=0;
    if(service && VIMIOCanRetainObject() && VIMIOObjectConformsTo(service,"VMsvga2Accel") &&
       IOObjectRetain(service)==kIOReturnSuccess)next=service;
    pthread_mutex_lock(&vim_renderer_accelerator_lock);
    io_service_t previous=vim_renderer_accelerator;
    vim_renderer_accelerator=next;
    pthread_mutex_unlock(&vim_renderer_accelerator_lock);
    if(previous)IOObjectRelease(previous);
}
static io_service_t VIMRendererAccelerator(void)
{
    // Some period CGL entry paths supply no retainable accelerator. Their
    // existing fallback performed a registry enumeration for EVERY pbuffer
    // attachment/texture import. Retain that same discovered provider for the
    // renderer image instead. Never cache a surface binding, geometry, pixel
    // contents, ownership result or a failed discovery. CGL serializes library
    // init/termination against use; callers borrow this right for IOServiceOpen.
    pthread_mutex_lock(&vim_renderer_accelerator_lock);
    if(!vim_renderer_accelerator)
        vim_renderer_accelerator=VIMIOServiceForClass("VMsvga2Accel");
    io_service_t result=vim_renderer_accelerator;
    pthread_mutex_unlock(&vim_renderer_accelerator_lock);
    return result;
}
__attribute__((destructor)) static void VIMReleaseRendererAccelerator(void)
{VIMSetRendererAccelerator(0);}
#endif
