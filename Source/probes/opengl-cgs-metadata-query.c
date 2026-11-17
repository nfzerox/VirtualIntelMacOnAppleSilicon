/* Native unit test of the actual optional-query adapter, no IOKit/VM. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int IOReturn;
typedef unsigned io_connect_t;
enum { kIOReturnUnsupported=-1,kIOReturnBadArgument=-2,kIOReturnNotPermitted=-3,kIOReturnNotOpen=-4 };
enum { kIOVM2DGetSurfaceGeometry=27,kIOVM2DGetSurfaceState=29,
    kIOVM2DGetSurfaceDrawingSize=53,kIOVM2DGetSurfacePixelFormat=54 };
#include "../vendor/source/VMsvga2/vminclude/vim_surface_query.h"
static unsigned probes,queries,methods;static IOReturn probe_error,query_error;
static unsigned kind(unsigned selector) {
    return selector==27?VIM_SURFACE_QUERY_GEOMETRY:selector==29?VIM_SURFACE_QUERY_STATE:
        selector==53?VIM_SURFACE_QUERY_DRAWING_SIZE:VIM_SURFACE_QUERY_PIXEL_FORMAT;
}
static IOReturn IOConnectTrap4(io_connect_t connection,unsigned index,uintptr_t action,
    uintptr_t output,uintptr_t bytes,uintptr_t reserved) {
    assert(connection==7 && index==VIM_SURFACE_QUERY_TRAP && !reserved);
    if(!action){++probes;assert(!output&&!bytes);return probe_error;}
    ++queries;assert(output && bytes==VIMSurfaceQueryBytes((unsigned)action));
    if(!query_error)memset((void *)output,(int)action,bytes);return query_error;
}
static IOReturn IOConnectCallStructMethod(io_connect_t connection,unsigned selector,
    const void *input,size_t input_bytes,void *output,size_t *bytes) {
    assert(connection==7 && !input && !input_bytes);++methods;
    if(output&&bytes)memset(output,(int)kind(selector),*bytes);return 0;
}
#include "../PassthroughOpenGL/cgs-metadata-query.h"
static void reset(void) {vim_surface_query_trap=-1;probes=queries=methods=0;probe_error=query_error=0;}
int main(void) {
    const unsigned selectors[]={27,29,53,54};unsigned char fast[32],old[32];
#if defined(__LP64__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    reset();size_t bytes=32;
    assert(!VIMSurfaceMetadataQuery(7,29,fast,&bytes));
    assert(methods==1 && !probes && !queries);
    puts("PASS 64-bit Darwin8/9 task selects address-safe MIG before a trap");return 0;
#else
    reset();
    for(unsigned i=0;i<4;i++){
        size_t bytes=VIMSurfaceQueryBytes(kind(selectors[i]));memset(fast,0xaa,sizeof(fast));memset(old,0xaa,sizeof(old));
        assert(!VIMSurfaceMetadataQuery(7,selectors[i],fast,&bytes));
        assert(!IOConnectCallStructMethod(7,selectors[i],NULL,0,old,&bytes));
        assert(!memcmp(fast,old,sizeof(fast)));
    }
    assert(probes==1 && queries==4 && methods==4);
    size_t bytes=32;query_error=kIOReturnNotOpen;
    assert(VIMSurfaceMetadataQuery(7,29,fast,&bytes)==kIOReturnNotOpen && methods==4);
    unsigned old_queries=queries;bytes=31;
    assert(VIMSurfaceMetadataQuery(7,29,fast,&bytes)==kIOReturnBadArgument && queries==old_queries);
    assert(VIMSurfaceMetadataQuery(7,29,NULL,&bytes)==kIOReturnBadArgument);
    for(unsigned i=0;i<2;i++) {
        reset();probe_error=i?kIOReturnBadArgument:kIOReturnUnsupported;bytes=32;
        for(unsigned j=0;j<3;j++)assert(!VIMSurfaceMetadataQuery(7,29,old,&bytes));
        assert(probes==1 && !queries && methods==3);
    }
    reset();probe_error=kIOReturnNotPermitted;bytes=32;
    assert(VIMSurfaceMetadataQuery(7,29,old,&bytes)==kIOReturnNotPermitted && !queries && !methods);
    reset();assert(!VIMSurfaceMetadataQueryUsingTrap(7,29,old,&bytes,NULL));
    assert(!probes && !queries && methods==1); // weak Rosetta1 export absent
    reset();assert(!VIMSurfaceMetadataQuery(7,999,old,&bytes));assert(!probes && methods==1);
    assert(!VIMSurfaceQueryBytes(0) && !VIMSurfaceQueryBytes(5));
    puts("PASS actual metadata adapter: exact results, one probe, old/weak fallback, invalid sizes, no retry on failure");
    return 0;
#endif
}
