#include "HostVM.hpp"
#include <mach/mach_vm.h>
#include <cstdio>

int main()
{
    vm_address_t allocation = 0;
    const vm_size_t length = vm_page_size * 8;
    if (vm_allocate(mach_task_self(), &allocation, length, VM_FLAGS_ANYWHERE))
        return 1;
    struct Cleanup {
        vm_address_t address;
        vm_size_t length;
        ~Cleanup() { vm_deallocate(mach_task_self(), address, length); }
    } cleanup{allocation, length};
    auto* bytes = reinterpret_cast<volatile uint8_t*>(allocation);
    const vm_address_t begin = allocation + 4096;
    const vm_address_t end = allocation + vm_page_size * 3 + 4096;
    if (protectUnusedHostPages(reinterpret_cast<void*>(begin), reinterpret_cast<void*>(end)))
        return 2;
    // On a 16 KiB host these writes share the host pages containing the
    // guest's guard boundaries. The old outward-rounded protection faults.
    bytes[begin - allocation - 1] = 0xab;
    bytes[end - allocation] = 0xcd;
    mach_vm_address_t address = (begin + vm_page_size - 1) & ~(vm_address_t(vm_page_size) - 1);
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info{};
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t result = mach_vm_region(mach_task_self(), &address, &size,
        VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &count, &object);
    if (object != MACH_PORT_NULL)
        mach_port_deallocate(mach_task_self(), object);
    if (result || info.protection != VM_PROT_NONE)
        return 3;
    if (protectUnusedHostPages(reinterpret_cast<void*>(allocation + 32),
                               reinterpret_cast<void*>(allocation + 64)))
        return 4;
    bytes[48] = 0xef;
    puts("PASS 4 KiB guest guard boundaries retain writable host edge pages and protect whole unused pages");
    return 0;
}
