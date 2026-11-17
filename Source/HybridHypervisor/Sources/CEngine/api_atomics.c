// Host-side atomics on state shared with the monitor. Swift has no atomics on imported C fields.
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "engine_abi.h"
#include "engine_api.h"

void engine_spin_hint(void) { __asm__ volatile("yield"); }

void engine_ctx_post_vector(void *ctx, uint8_t vector, bool level) {
    EngineCtx *c = ctx;
    uint32_t bit = 1u << (vector & 31);
    if (level)
        __atomic_fetch_or(&c->apic_host_level[vector / 32], bit, __ATOMIC_RELEASE);
    else
        __atomic_fetch_and(&c->apic_host_level[vector / 32], ~bit, __ATOMIC_RELEASE);
    __atomic_fetch_or(&c->apic_host_irr[vector / 32], bit, __ATOMIC_RELEASE);
}

void engine_ctx_set_stop(void *ctx) { __atomic_store_n(&((EngineCtx *)ctx)->stop_requested, 1, __ATOMIC_RELEASE); }

void engine_ctx_set_irq_line(void *ctx, bool level) { __atomic_store_n(&((EngineCtx *)ctx)->irq_line, level, __ATOMIC_RELEASE); }

// Puts each frame of a written range that holds code or page tables on the monitor's list. True if there were any: the vCPU then needs a kick.
bool engine_ctx_note_write(void *ctx, const unsigned char *page_info, uint64_t gpa, uint64_t bytes) {
    EngineCtx *c = ctx;
    bool any = false;
    uint64_t top = gpa + bytes < c->ram_size ? gpa + bytes : c->ram_size;
    for (uint64_t at = gpa & ~0xFFFULL; at < top; at += 4096) {
        if (!(__atomic_load_n(&page_info[at >> 12], __ATOMIC_ACQUIRE) & (PI_CODE | PI_PT)))
            continue;
        any = true;
        uint32_t slot = __atomic_fetch_add(&c->note_head, 1, __ATOMIC_ACQ_REL);
        if (slot - __atomic_load_n(&c->note_tail, __ATOMIC_ACQUIRE) >= 256)
            __atomic_store_n(&c->note_overflow, 1, __ATOMIC_RELEASE);
        else
            __atomic_store_n(&c->note_ring[slot % 256], (uint32_t)(at >> 12) + 1, __ATOMIC_RELEASE);
    }
    return any;
}

// RAM mapped as QEMU's memory-backend-shm does it, for the hybrid test. shm_open is variadic, so Swift can't call it.
void *engine_test_shared_ram(uint64_t bytes) {
    char name[] = "/lc-engine-test-ram";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    shm_unlink(name);
    if (fd < 0 || ftruncate(fd, (off_t)bytes) != 0)
        return NULL;
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return p == MAP_FAILED ? NULL : p;
}
