/* Bounded negative-cache policy, independent of a running VM or GL driver. */
#include "../PassthroughOpenGL/cgs-defer-rejections.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    struct VIMDeferRejections cache={0};
    assert(!VIMDeferWasUnsupported(&cache,1,1));
    VIMDeferRememberUnsupported(&cache,1,1);
    assert(VIMDeferWasUnsupported(&cache,1,1));
    assert(!VIMDeferWasUnsupported(&cache,1,2)); // New backing incarnation.
    assert(!VIMDeferWasUnsupported(&cache,2,1)); // New kernel object.
    unsigned next=cache.next;
    VIMDeferRememberUnsupported(&cache,1,1);
    assert(cache.next==next); // A hit cannot evict an unrelated entry.
    VIMDeferRememberUnsupported(&cache,0,1);
    VIMDeferRememberUnsupported(&cache,1,0);
    assert(cache.next==next && !VIMDeferWasUnsupported(&cache,0,1));
    for(uint64_t i=2;i<=VIM_DEFER_REJECTIONS;++i)
        VIMDeferRememberUnsupported(&cache,i,1);
    assert(VIMDeferWasUnsupported(&cache,1,1));
    VIMDeferRememberUnsupported(&cache,100,9);
    assert(!VIMDeferWasUnsupported(&cache,1,1)); // Bounded ring eviction.
    assert(VIMDeferWasUnsupported(&cache,100,9));
    struct VIMDeferRejections independent={0};
    assert(!VIMDeferWasUnsupported(&independent,100,9)); // Not process-global.
    puts("PASS deferred rejection identity/incarnation, bounds, eviction, isolation");
    return 0;
}
