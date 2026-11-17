/* Same read-only getters/validation, avoiding MIG on a supporting provider.
 * No data/epoch is cached. Probe before querying; old providers and Rosetta1
 * bridges without IOConnectTrap4 retain the original struct-method path. */
#ifndef VIM_CGS_METADATA_QUERY_H
#define VIM_CGS_METADATA_QUERY_H
#include "../vendor/source/VMsvga2/vminclude/vim_surface_query.h"
#if defined(__i386__)
extern IOReturn IOConnectTrap4(io_connect_t,unsigned int,uintptr_t,uintptr_t,
    uintptr_t,uintptr_t) __attribute__((weak_import));
#endif
static int vim_surface_query_trap=-1;
typedef IOReturn (*VIMSurfaceQueryTrap)(io_connect_t,unsigned int,uintptr_t,uintptr_t,uintptr_t,uintptr_t);
static IOReturn VIMSurfaceMetadataQueryUsingTrap(io_connect_t connection,unsigned selector,
    void *output,size_t *bytes,VIMSurfaceQueryTrap trap)
{
    unsigned kind=selector==kIOVM2DGetSurfaceGeometry?VIM_SURFACE_QUERY_GEOMETRY:
        selector==kIOVM2DGetSurfaceState?VIM_SURFACE_QUERY_STATE:
        selector==kIOVM2DGetSurfaceDrawingSize?VIM_SURFACE_QUERY_DRAWING_SIZE:
        selector==kIOVM2DGetSurfacePixelFormat?VIM_SURFACE_QUERY_PIXEL_FORMAT:0;
    if(!kind || !trap)
        return IOConnectCallStructMethod(connection,selector,NULL,0,output,bytes);
    int known=__atomic_load_n(&vim_surface_query_trap,__ATOMIC_RELAXED);
    if(known<0) {
        IOReturn probe=trap(connection,VIM_SURFACE_QUERY_TRAP,VIM_SURFACE_QUERY_PROBE,0,0,0);
        if(probe && probe!=kIOReturnUnsupported && probe!=kIOReturnBadArgument)return probe;
        known=probe?0:1;__atomic_store_n(&vim_surface_query_trap,known,__ATOMIC_RELAXED);
    }
    if(!known)return IOConnectCallStructMethod(connection,selector,NULL,0,output,bytes);
    if(!output || !bytes || *bytes!=VIMSurfaceQueryBytes(kind))return kIOReturnBadArgument;
    return trap(connection,VIM_SURFACE_QUERY_TRAP,kind,(uintptr_t)output,*bytes,0);
}
static IOReturn VIMSurfaceMetadataQuery(io_connect_t connection,unsigned selector,void *output,size_t *bytes)
{
#if defined(__LP64__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    /* Darwin8/9's IOTrap callback truncates 64-bit task arguments to a
     * kernel-sized void*. Select MIG before passing any user address, just
     * as the existing direct GL exchange does. No failed-query replay. */
    return IOConnectCallStructMethod(connection,selector,NULL,0,output,bytes);
#else
    return VIMSurfaceMetadataQueryUsingTrap(connection,selector,output,bytes,IOConnectTrap4);
#endif
}
#endif
