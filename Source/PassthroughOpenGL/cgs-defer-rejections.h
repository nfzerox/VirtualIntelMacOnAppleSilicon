/* A negative optimization only: an unsupported deferred handoff still uses
 * the ordinary synchronous CPU publication. Never cache busy/transient errors
 * or infer eligibility from a CGS number or a recycled virtual address.
 * Fresh state under the surface lock supplies both lifetime-unique keys.
 * A formerly unsupported backing becoming eligible without changing keys
 * merely retains the correct CPU fallback until eviction/context teardown.
 */
#ifndef VIM_CGS_DEFER_REJECTIONS_H
#define VIM_CGS_DEFER_REJECTIONS_H
#include <stdint.h>
#define VIM_DEFER_REJECTIONS 16
struct VIMDeferRejection { uint64_t object,incarnation; };
struct VIMDeferRejections {
    struct VIMDeferRejection entries[VIM_DEFER_REJECTIONS];
    unsigned next;
};
static int VIMDeferWasUnsupported(const struct VIMDeferRejections *cache,
    uint64_t object,uint64_t incarnation)
{
    if(!object || !incarnation)return 0;
    for(unsigned i=0;i<VIM_DEFER_REJECTIONS;++i)
        if(cache->entries[i].object==object && cache->entries[i].incarnation==incarnation)return 1;
    return 0;
}
static void VIMDeferRememberUnsupported(struct VIMDeferRejections *cache,
    uint64_t object,uint64_t incarnation)
{
    if(!object || !incarnation || VIMDeferWasUnsupported(cache,object,incarnation))return;
    cache->entries[cache->next]=(struct VIMDeferRejection){object,incarnation};
    cache->next=(cache->next+1)%VIM_DEFER_REJECTIONS;
}
#endif
