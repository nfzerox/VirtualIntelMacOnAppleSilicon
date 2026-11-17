// The C interface a QEMU AccelOps accelerator drives. Devices, memory topology and the machine stay in QEMU; the engine owns the x86 CPU,
// its translation, its shadow page tables and the local APIC's hot paths. QEMU's side is vendor/qemu/target/i386/tcg/system/hvdbt.c.
// Threading: everything below runs on the thread that created the engine, unless marked "any thread".
#pragma once
#include "latency.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EngineHandle EngineHandle;

// Device and platform callbacks. Each runs on the vCPU thread with the guest stopped. For mmio and ports, the access itself has not
// completed: returning completes it. opaque is what engine_create received.
typedef struct {
    void *opaque;
    uint64_t (*mmio_read)(void *opaque, uint64_t gpa, unsigned size);
    void (*mmio_write)(void *opaque, uint64_t gpa, unsigned size, uint64_t value);
    uint32_t (*port_read)(void *opaque, uint16_t port, unsigned size);
    void (*port_write)(void *opaque, uint16_t port, unsigned size, uint32_t value);
    bool (*read_msr)(void *opaque, uint32_t msr, uint64_t *value);
    bool (*write_msr)(void *opaque, uint32_t msr, uint64_t value);
    void (*cpuid)(void *opaque, uint32_t leaf, uint32_t subleaf, uint32_t out[4]);
    // Legacy external interrupt (PIC, ExtINT): the vector, or -1. The APIC path uses engine_post_vector instead.
    int (*acknowledge_interrupt)(void *opaque);
    bool (*has_interrupt_waiting)(void *opaque);
    // The guest halted. Sleep until something might interrupt it, no later than deadline_ns if nonzero. False: nothing ever will.
    bool (*wait_for_interrupt)(void *opaque, uint64_t deadline_ns);
    // EOI of a level-triggered vector, for the IO-APIC model.
    void (*apic_eoi)(void *opaque, uint8_t vector);
    // An IPI the local APIC cannot finish alone: other CPUs, INIT, SIPI. Raw ICR value, high word in bits 63:32.
    void (*apic_icr)(void *opaque, uint64_t icr);
} EngineMachineOps;

typedef struct {
    uint64_t ram_bytes;
    void *ram_host; // RAM the host already mapped, or NULL for the engine to allocate
    bool kernel_model; // full x86 kernel model; false is the user-mode syscall shim
    bool precise_flags;
    bool tso;
    bool tzcnt_lzcnt;
    // Nonzero: ram_host holds this many bytes at GPA 0 and the rest (ram_bytes minus this) at GPA 4 GB. Zero: all of it at GPA 0.
    uint64_t ram_below_4g;
    // Another CPU emulator owns the machine, and takes single instructions back on ENGINE_RUN_FALLBACK: hlt, rare x87, unknown opcodes, far control
    // flow, any MSR outside the syscall family, the APIC. In-VM APIC is off, interrupts come through has_interrupt_waiting and acknowledge_interrupt.
    bool hybrid;
} EngineConfig;

typedef enum {
    ENGINE_RUN_EXIT, // a device asked to stop the machine (engine_request_exit)
    ENGINE_RUN_BOUNDARY, // stopped at an instruction boundary after engine_request_boundary
    ENGINE_RUN_HALTED, // hlt with nothing able to wake it
    ENGINE_RUN_ERROR, // a clear stop: engine_error_text says why
    ENGINE_RUN_FALLBACK, // hybrid only: run the instruction at state.rip elsewhere, engine_set_state, engine_run
} EngineRunResult;

// CPU state in QEMU's terms, to fill CPUX86State at a boundary. Segment bases beyond fs and gs are always zero in long mode.
typedef struct {
    uint64_t gpr[16], rip, rflags;
    uint64_t cr0, cr2, cr3, cr4, cr8, efer;
    uint64_t star, lstar, cstar, sfmask, fs_base, gs_base, kernel_gs_base;
    uint64_t gdt_base, idt_base, tss_base;
    uint32_t gdt_limit, idt_limit, tss_limit;
    uint16_t sel[6], tr, ldtr;
    uint32_t cpl, mxcsr;
    uint64_t xmm[16][2];
    uint64_t mm[8]; // the low 64 bits of the x87 registers, by physical number
    uint16_t fp_exp[8]; // their sign and exponent
    uint8_t fp_top, fp_tags; // fp_tags: bit set means in use, QEMU's fptags inverted
    uint16_t fcw, fsw, fop, fcs, fds; // fsw with the TOP bits clear
    uint64_t fip, fdp;
    uint64_t apic_base;
    uint32_t apic_tpr, apic_svr, apic_id, apic_ldr, apic_dfr, apic_lvt_timer, apic_lvt[5], apic_tmict, apic_tdcr;
    uint32_t apic_irr[8], apic_isr[8], apic_tmr[8];
    // Flat IA-32e compatibility mode at ring0 or3. seg_bad keeps independent
    // user/supervisor data-selector policies; see compat_segments.h.
    uint32_t compat, seg_bad;
    uint64_t ldt_base;
    uint32_t ldt_limit;
    uint64_t sysenter_cs, sysenter_esp, sysenter_eip;
    // Hybrid: the APIC is QEMU's, but the monitor answers for ICR, so that an IPI takes one exit. hybrid_apic_page: 0 if the APIC is off.
    uint64_t hybrid_apic_page;
    uint32_t hybrid_icr_lo, hybrid_icr_hi;
} EngineX86State;

EngineHandle *engine_create(const EngineConfig *config, const EngineMachineOps *ops);
// On the first vCPU, before it runs: how many this VM will have, however many are made by then.
void engine_prepare_count(EngineHandle *first, uint32_t count);
// Another vCPU of first's VM, created and run on the calling thread, from then on used as any handle. Index: from 1, less than the prepared count.
EngineHandle *engine_create_secondary(EngineHandle *first, uint32_t index, const EngineMachineOps *ops);
// For the tally: a kick that will stop this vCPU for another's exclusive section.
void engine_count_exclusive_stop(EngineHandle *engine);
void engine_destroy(EngineHandle *engine);
// Host pointer to RAM, for loaders and DMA.
void *engine_ram(EngineHandle *engine);
// Before the first run: this guest-physical range belongs to a device, though RAM lies behind it. Accesses reach mmio_read and mmio_write.
// Maps device memory that is plain host RAM into the VM, so that guest access needs no exit. The caller keeps it coherent with anything that watches
// it, such as a display's dirty log. Page-aligned to the host's page. False if it cannot be done, and the device path goes on serving it.
bool engine_map_direct(EngineHandle *engine, uint64_t gpa, void *host, uint64_t bytes);
// Takes it out again, while no guest code is running on the vCPU.
void engine_unmap_direct(EngineHandle *engine, uint64_t gpa, uint64_t bytes);
void engine_mark_device(EngineHandle *engine, uint64_t gpa, uint64_t bytes);
// Runs until something needs the host's attention. The first call starts at state.rip after engine_set_state.
EngineRunResult engine_run(EngineHandle *engine);
const char *engine_error_text(EngineHandle *engine);
// Only valid while stopped after ENGINE_RUN_BOUNDARY or before the first run.
// With every vCPU off the engine: RAM may have changed behind it, on a machine reset or a snapshot load. Drops all that derives from it.
void engine_forget_all(EngineHandle *engine);
void engine_get_state(EngineHandle *engine, EngineX86State *out);
void engine_set_state(EngineHandle *engine, const EngineX86State *in);

// From a device callback: engine_run returns ENGINE_RUN_EXIT once the access completes.
void engine_request_exit(EngineHandle *engine);

// Any thread.
void engine_request_boundary(EngineHandle *engine); // kick: engine_run returns ENGINE_RUN_BOUNDARY at the next x86 boundary
void engine_kick(EngineHandle *engine); // look at has_interrupt_waiting and pending vectors
void engine_post_vector(EngineHandle *engine, uint8_t vector, bool level);
// Guest RAM was written other than by this engine (DMA, TCG): drop what depends on it. Translations and shadow tables are gone before the next
// monitor exit after a kick; a guest polling the written memory could see the old state for that long.
void engine_note_write(EngineHandle *engine, uint64_t gpa, uint64_t bytes);
// Hybrid: an interrupt may be waiting. The monitor asks acknowledge_interrupt once IF allows.
void engine_set_irq_line(EngineHandle *engine, bool level);

// While stopped. The guest's TSC in QEMU's terms: ns is the host's uptime clock plus ns_offset, TSC = ns + ns * num / den + tsc_offset.
void engine_set_tsc(EngineHandle *engine, uint64_t ns_offset, uint64_t tsc_offset, uint64_t num, uint64_t den);

// Writes the ranked tally of stops and the unknown rates to path now, every 2 seconds, and on engine_tally_write. Samples the rip each second by a
// boundary stop. Any thread.
void engine_tally_start(EngineHandle *engine, const char *path);
void engine_tally_write(EngineHandle *engine);
// The vCPU stays off the engine for this reason, until the next engine_run. Timed and ranked with the fallbacks. Any thread.
void engine_tally_refused(EngineHandle *engine, const char *reason);
// A line for the top of the tally. Any thread.
void engine_tally_note(EngineHandle *engine, const char *text);
// From port_write: records the access in progress with its rip, CR3, the bytes there, a frame-pointer walk and the stack. slide is subtracted for the unslid column.
void engine_tally_port_write(EngineHandle *engine, uint16_t port, unsigned size, uint32_t value, uint64_t slide);
// A rip seen while the vCPU is off the engine. Any thread.
void engine_tally_sample(EngineHandle *engine, uint64_t rip, uint32_t cpl, uint64_t cr3);
// Any thread. Grows when the guest does anything the monitor or translator sees; unchanged means spinning in translated code, or off the engine.
uint64_t engine_progress(EngineHandle *engine);
// Any thread, for a stall report: whether the vCPU is in hv_vcpu_run, the last host call, the monitor's interrupt state and its last-saved registers.
void engine_debug_text(EngineHandle *engine, char *out, unsigned long size);

// Atomic helpers the Swift side needs, implemented in C.
void engine_ctx_post_vector(void *ctx, uint8_t vector, bool level);
void engine_ctx_set_stop(void *ctx);
void engine_ctx_set_irq_line(void *ctx, bool level);
void engine_spin_hint(void);
bool engine_ctx_note_write(void *ctx, const unsigned char *page_info, uint64_t gpa, uint64_t bytes);
void *engine_test_shared_ram(uint64_t bytes);

#ifdef __cplusplus
}
#endif
