/* Read-only 32-bit Leopard kmod inventory. XNU 1228 serializes all module
 * records first, preserving a nonzero next sentinel until the final record;
 * reference records follow. Never follow the copied kernel pointers. */
#include <mach/mach.h>
#include <mach/kmod.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    kmod_args_t serialized = NULL;
    mach_msg_type_number_t bytes = 0;
    kern_return_t result = kmod_get_info(mach_host_self(), &serialized, &bytes);
    kmod_info_array_t modules = (kmod_info_array_t)serialized;
    printf("kmod-result=%d bytes=%u record-bytes=%lu\n", result, bytes,
           (unsigned long)sizeof(kmod_info_t));
    if (result != KERN_SUCCESS) return 1;
    size_t count = bytes / sizeof(kmod_info_t);
    for (size_t i = 0; i < count; ++i) {
        const kmod_info_t *m = modules + i;
        if (m->info_version != KMOD_INFO_VERSION ||
            !memchr(m->name, 0, sizeof(m->name)) ||
            !memchr(m->version, 0, sizeof(m->version))) {
            puts("invalid-module-record");
            vm_deallocate(mach_task_self(), (vm_address_t)modules, bytes);
            return 2;
        }
        printf("module=%d name=%s version=%s address=%08lx size=%lu\n",
               m->id, m->name, m->version, (unsigned long)m->address,
               (unsigned long)m->size);
        if (!m->next) break;
    }
    if (modules) vm_deallocate(mach_task_self(), (vm_address_t)modules, bytes);
    return 0;
}
