#pragma once

#include <mach/mach.h>
#include <cstdint>

// Cache regions follow the Intel guest's page layout. Only whole unused
// host pages may become guard pages; vm_protect rounds both boundaries out.
inline kern_return_t protectUnusedHostPages(void* begin, const void* end)
{
    const vm_address_t mask = vm_page_size - 1;
    const vm_address_t first = (reinterpret_cast<vm_address_t>(begin) + mask) & ~mask;
    const vm_address_t last = reinterpret_cast<vm_address_t>(end) & ~mask;
    if (last <= first)
        return KERN_SUCCESS;
    return vm_protect(mach_task_self(), first, last - first, false, VM_PROT_NONE);
}
