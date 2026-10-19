// Experimental fixed shared-RAM mapping backend for the Intel PVG helper.
// Guest hotplug is not implemented. Ranges describe the flattened GPA layout.
#include <mach/mach_vm.h>
#include <pthread.h>
typedef struct {
    uint64_t guest, length, backingOffset;
} OOPRAMRange;
typedef struct {
    uint8_t *backing;
    uint64_t backingLength;
    const OOPRAMRange *ranges;
    uint32_t count;
} OOPRAM;
static void *oop_ram_resolve(const OOPRAM *ram, uint64_t address, uint64_t length) {
    for (uint32_t i = 0; i < ram->count; i++) {
        const OOPRAMRange *r = &ram->ranges[i];
        if (address < r->guest) continue;
        uint64_t offset = address - r->guest;
        if (offset >= r->length || length > r->length - offset) continue;
        if (r->backingOffset > ram->backingLength ||
            offset > ram->backingLength - r->backingOffset ||
            length > ram->backingLength - r->backingOffset - offset) return NULL;
        return ram->backing + r->backingOffset + offset;
    }
    return NULL;
}
struct PGTask_s {
    mach_vm_address_t address;
    uint64_t length;
    pthread_mutex_t lock;
};
static PGTask_t *oop_task_create(uint64_t size, void **base) {
    *base = NULL;
    if (vm_page_size != 4096 || !size || (size & 4095)) return NULL;
    PGTask_t *task = calloc(1, sizeof(*task));
    if (!task) return NULL;
    if (mach_vm_allocate(mach_task_self(), &task->address, size, VM_FLAGS_ANYWHERE)) {
        free(task); return NULL;
    }
    task->length = size;
    pthread_mutex_init(&task->lock, NULL);
    *base = (void *)task->address;
    return task;
}
static void oop_task_destroy(PGTask_t *task) {
    if (!task) return;
    mach_vm_deallocate(mach_task_self(), task->address, task->length);
    pthread_mutex_destroy(&task->lock);
    free(task);
}
static bool oop_task_map(PGTask_t *task, uint64_t offset,
                        const PGPhysicalMemoryRange_t *ranges, uint32_t count,
                        bool readOnly, const OOPRAM *ram) {
    if (!task || offset > task->length || (offset & 4095) || (count && !ranges)) return false;
    uint64_t total = 0;
    // Validate the entire request before changing any mappings.
    for (uint32_t i = 0; i < count; i++) {
        uint64_t address = ranges[i].physicalAddress, length = ranges[i].physicalLength;
        if (!length || ((address | length) & 4095) ||
            !oop_ram_resolve(ram, address, length) ||
            length > task->length - offset - total) return false;
        total += length;
    }
    pthread_mutex_lock(&task->lock);
    bool ok = true;
    for (uint32_t i = 0; i < count; i++) {
        mach_vm_address_t target = task->address + offset;
        vm_prot_t current, maximum;
        uint64_t length = ranges[i].physicalLength;
        kern_return_t kr = mach_vm_remap(mach_task_self(), &target, length, 0,
            VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
            (mach_vm_address_t)oop_ram_resolve(ram, ranges[i].physicalAddress, length), FALSE,
            &current, &maximum, VM_INHERIT_NONE);
        if (!kr && readOnly) kr = mach_vm_protect(mach_task_self(), target,
                                                  length, FALSE, VM_PROT_READ);
        if (kr) { ok = false; break; }
        offset += length;
    }
    pthread_mutex_unlock(&task->lock);
    return ok;
}
static bool oop_task_unmap(PGTask_t *task, uint64_t offset, uint64_t length) {
    if (!task || !length || ((offset | length) & 4095) ||
        offset > task->length || length > task->length - offset) return false;
    pthread_mutex_lock(&task->lock);
    mach_vm_address_t target = task->address + offset;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &target, length,
                                        VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
    pthread_mutex_unlock(&task->lock);
    return kr == KERN_SUCCESS;
}
