/* Exercise the ACTUAL bounded admission core, no VM/kernel/display mutation. */
#include "opengl-object-admission.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
int main(void)
{
    struct VIMObjectAdmission r={0};
    assert(!VIMObjectRelease(&r,0));
    assert(VIMObjectReserve(&r,0));assert(!VIMObjectReserve(&r,(void *)(uintptr_t)1));
    assert(!VIMObjectReserve(&r,0));assert(VIMObjectRelease(&r,0));
    for(unsigned i=1;i<=VIM_OBJECT_ADMISSION_LIMIT;++i) {
        assert(VIMObjectReserve(&r,(void *)(uintptr_t)i));
        assert(!VIMObjectReserve(&r,(void *)(uintptr_t)i));
        assert(!VIMObjectReserve(&r,0));
    }
    assert(!VIMObjectReserve(&r,(void *)(uintptr_t)(VIM_OBJECT_ADMISSION_LIMIT+1)));
    assert(!VIMObjectRelease(&r,(void *)(uintptr_t)(VIM_OBJECT_ADMISSION_LIMIT+1)));
    assert(VIMObjectRelease(&r,(void *)(uintptr_t)1));
    assert(VIMObjectReserve(&r,(void *)(uintptr_t)(VIM_OBJECT_ADMISSION_LIMIT+1)));
    for(unsigned i=2;i<=VIM_OBJECT_ADMISSION_LIMIT+1;++i)assert(VIMObjectRelease(&r,(void *)(uintptr_t)i));
    assert(VIMObjectReserve(&r,0));assert(VIMObjectRelease(&r,0));
    puts("PASS bounded object admission: aliases, independent objects, exclusive partial timer, capacity, retirement");
    return 0;
}
