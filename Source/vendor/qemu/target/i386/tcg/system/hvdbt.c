/*
 * hvdbt: run a guest's 64-bit code, and its flat 32-bit user code, in Source/Engine, the x86 translator inside a Hypervisor.framework VM, with TCG as its other half.
 *
 * Off unless LC_HVDBT=1, on a build configured with -Dhvdbt_engine. TCG runs the firmware and the boot loaders. On the first TB miss in the high half in
 * 64-bit mode, which is XNU's vstart, this file loads the engine's state from CPUX86State and runs it on the vCPU thread. Whatever the engine can't do
 * comes back as ENGINE_RUN_FALLBACK: TCG runs that one instruction and, through the same TB miss, control returns. An instruction that leaves what the
 * engine takes (to 16-bit code, say, or a segment with a base) just means TCG goes on running until a TB miss finds the state acceptable again. Since a
 * normal TB is never generated for an acceptable state, there is always a miss to catch.
 *
 * The APIC stays QEMU's, for both halves: the engine's in-VM one is off. So APIC accesses go through the host here, which DESIGN.md's rates don't assume.
 * Device models, timers and vCPU stop and start remain QEMU's. SMP uses the engine's bounded vCPU set.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/cutils.h"
#include "qemu/rcu.h"
#include "qemu/thread.h"
#include "qemu/timer.h"
#include "cpu.h"
#include "accel/tcg/handoff.h"
#include "exec/cputlb.h"
#include "exec/target_page.h"
#include "exec/tb-flush.h"
#include "exec/translation-block.h"
#include "hw/core/boards.h"
#include "hw/core/cpu.h"
#include "system/reset.h"
#include "hw/i386/apic.h"
#include "hw/i386/apic_internal.h"
#include "hw/i386/x86.h"
#include "system/address-spaces.h"
#include "system/cpu-timers.h"
#include "system/cpus.h"
#include "system/memory.h"
#include "system/physmem.h"
#include "system/runstate.h"
#include "system/tcg.h"
#include "ui/console.h"
#include "ui/qemu-pixman.h"
#include "engine_api.h"
#include "compat_segments.h"
#include "hvdbt-write-span.h"

#define HIGH_RAM (1ULL << 32)
/* What the monitor's CR4_OK allows. */
#define ENGINE_CR4 (CR4_TSD_MASK | CR4_DE_MASK | CR4_PSE_MASK | CR4_PAE_MASK | CR4_MCE_MASK | CR4_PGE_MASK | CR4_PCE_MASK | CR4_OSFXSR_MASK | \
                    CR4_OSXMMEXCPT_MASK | CR4_PCIDE_MASK | CR4_OSXSAVE_MASK | CR4_SMEP_MASK)
#define OTHER_INTERRUPTS (CPU_INTERRUPT_NMI | CPU_INTERRUPT_SMI | CPU_INTERRUPT_INIT | CPU_INTERRUPT_SIPI | CPU_INTERRUPT_MCE)

static struct {
    EngineHandle *engine; /* the first vCPU's, which makes the VM and owns what is one for all: RAM notes, device memory */
    bool dead;
    ram_addr_t ram_base;
    uint64_t ram_bytes, below_4g;
    uint64_t kernel_base, kernel_slide, kernel_data, kernel_data_bytes;
    bool shutdown_dumped;
} hv;

/* As the engine's MAX_VCPUS. */
#define HVDBT_MAX_CPUS 8

typedef struct {
    EngineHandle *engine;
    bool in_engine;
    bool until_kernel; /* after a reset: firmware and boot loaders stay on TCG, as on a first boot */
    bool tcg_ran; /* normal TBs have been generated: engine writes may have made them stale, and nothing tells TCG */
    bool fallback_quantum;
    unsigned fallback_left;
    bool transition_guard; /* temporary selector/interrupt state, checked at every TB */
    /* Pages TCG wrote since DIRTY_MEMORY_CODE was last cleared for them. Only the vCPU thread touches this. */
    HvdbtWriteSpan written;
    bool protect_all;
    char refused[64]; /* why the vCPU is on TCG, while it is */
    bool dump_done;
} HvCpu;

static HvCpu hv_cpus[HVDBT_MAX_CPUS];
#define HV(cs) (&hv_cpus[(cs)->cpu_index])

static void hvdbt_dump_kernel(CPUState *cs, const char *path);

/* ---- Devices: the vCPU thread, BQL taken here, as TCG's slow path would. ---- */

/* A device model running here can raise an interrupt or stop this vCPU, with no kick since it is on the vCPU's own thread. */
static void hvdbt_after_device(CPUState *cs)
{
    if (cpu_test_interrupt(cs, CPU_INTERRUPT_HARD | CPU_INTERRUPT_POLL)) {
        engine_set_irq_line(HV(cs)->engine, true);
    }
    if (cpu_test_interrupt(cs, OTHER_INTERRUPTS) || !cpu_can_run(cs) || !cpu_work_list_empty(cs)) {
        engine_request_boundary(HV(cs)->engine);
    }
}

/*
 * Device memory that is plain RAM to QEMU goes straight into the VM. By name, as each must be known to stay coherent this way. vmsvga reads its FIFO and scanout
 * from these pointers, and only legacy VGA mode watches a dirty log, which log_sync feeds. A PVG guest has neither region, and nothing here touches it.
 */
static bool hvdbt_direct_section(MemoryRegionSection *section)
{
    const char *name = memory_region_name(section->mr);

    if (!name || (strcmp(name, "vga.vram") && strcmp(name, "vmsvga.fifo") &&
                  strcmp(name, "vim-gl-headless-mailbox")) || !memory_region_is_ram(section->mr)) {
        return false;
    }
    /* Whole, aligned and at its BAR: the low VGA window is an alias of the same RAM, and it keeps its device path. */
    return section->offset_within_region == 0 && section->offset_within_address_space >= (1ULL << 20) &&
           int128_get64(section->size) == memory_region_size(section->mr) &&
           QEMU_IS_ALIGNED(section->offset_within_address_space | int128_get64(section->size), qemu_real_host_page_size());
}

static void hvdbt_direct_add(MemoryListener *listener, MemoryRegionSection *section)
{
    if (hvdbt_direct_section(section)) {
        bool done = engine_map_direct(hv.engine, section->offset_within_address_space,
                                      memory_region_get_ram_ptr(section->mr), int128_get64(section->size));
        char line[120];
        snprintf(line, sizeof(line), "direct map of '%s' at 0x%llx: %s", memory_region_name(section->mr),
                 (unsigned long long)section->offset_within_address_space, done ? "done" : "refused");
        engine_tally_note(hv.engine, line);
    }
}

static void hvdbt_direct_del(MemoryListener *listener, MemoryRegionSection *section)
{
    if (hvdbt_direct_section(section)) {
        engine_unmap_direct(hv.engine, section->offset_within_address_space, int128_get64(section->size));
    }
}

/* shortcut: everything counts as written at each sync, so legacy VGA redraws whole frames. It is only the boot screens; narrow it with write protection if they ever matter. */
static void hvdbt_direct_sync(MemoryListener *listener, MemoryRegionSection *section)
{
    if (hvdbt_direct_section(section)) {
        physical_memory_set_dirty_range(memory_region_get_ram_addr(section->mr), int128_get64(section->size),
                                        1 << DIRTY_MEMORY_VGA);
    }
}

static MemoryListener hvdbt_direct_listener = {
    .name = "hvdbt-direct",
    .region_add = hvdbt_direct_add,
    .region_del = hvdbt_direct_del,
    .log_sync = hvdbt_direct_sync,
    .priority = MEMORY_LISTENER_PRIORITY_ACCEL,
};

/* For the tally: what each device megabyte is, the first time it is touched. Up to 16 GB; the rest are not looked at. */
static void hvdbt_name_device(uint64_t gpa)
{
    static uint64_t seen[256];
    uint64_t mb = gpa >> 20;

    if (mb >= 16384 || (seen[mb / 64] & (1ULL << (mb % 64)))) {
        return;
    }
    seen[mb / 64] |= 1ULL << (mb % 64);
    RCU_READ_LOCK_GUARD();
    hwaddr offset, length = 1;
    MemoryRegion *mr = address_space_translate(&address_space_memory, gpa, &offset, &length, true,
                                               MEMTXATTRS_UNSPECIFIED);
    char line[160];
    snprintf(line, sizeof(line), "device memory at 0x%llx: region '%s', %llu MB, ram %d, ram_device %d, offset 0x%llx",
             (unsigned long long)gpa, memory_region_name(mr) ? memory_region_name(mr) : "?",
             (unsigned long long)(memory_region_size(mr) >> 20), memory_region_is_ram(mr),
             memory_region_is_ram_device(mr), (unsigned long long)offset);
    engine_tally_note(hv.engine, line);
}

static uint64_t hvdbt_mmio_read(void *opaque, uint64_t gpa, unsigned size)
{
    uint64_t v = 0;
    bql_lock();
    hvdbt_name_device(gpa);
    address_space_read(&address_space_memory, gpa, MEMTXATTRS_UNSPECIFIED, &v, size);
    hvdbt_after_device(opaque);
    bql_unlock();
    return v;
}

static void hvdbt_mmio_write(void *opaque, uint64_t gpa, unsigned size, uint64_t value)
{
    bql_lock();
    hvdbt_name_device(gpa);
    address_space_write(&address_space_memory, gpa, MEMTXATTRS_UNSPECIFIED, &value, size);
    hvdbt_after_device(opaque);
    bql_unlock();
}

static uint32_t hvdbt_port_read(void *opaque, uint16_t port, unsigned size)
{
    uint32_t v = 0;
    bql_lock();
    address_space_read(&address_space_io, port, MEMTXATTRS_UNSPECIFIED, &v, size);
    hvdbt_after_device(opaque);
    bql_unlock();
    return v;
}

/*
 * Who powers off or resets. shortcut: PMBASE as OVMF sets it on q35, 0x600, seen in every run so far. Writes run by TCG don't pass here, so a shutdown
 * with no row means TCG made it.
 */
static void hvdbt_watch_power_ports(CPUState *cs, uint16_t port, unsigned size, uint32_t value)
{
    const char *kernel = getenv("LC_HVDBT_KERNEL");
    bool off = (port == 0x604 && size >= 2 && (value & 0x2000)) || (port == 0x605 && (value & 0x20));
    bool reset = port == 0xcf9 && (value & 4);

    if (port != 0x604 && port != 0x605 && port != 0xcf9) {
        return;
    }
    engine_tally_port_write(HV(cs)->engine, port, size, value, hv.kernel_slide);
    if ((off || reset) && getenv("LC_HVDBT_RAM") && !hv.shutdown_dumped && !fork()) { /* in an orphan, as whoever is watching may end this process and its children as soon as the guest goes quiet */
        setsid();
        if (fork()) {
            _exit(0);
        }
        /* Sparse: zero pages are holes. The offset is into pc.ram, which is the GPA below 4 GB and GPA - 4 GB + below_4g above it. */
        FILE *ram = fopen(getenv("LC_HVDBT_RAM"), "wb");
        uint8_t *host = memory_region_get_ram_ptr(current_machine->ram);
        for (uint64_t at = 0; ram && at < hv.ram_bytes; at += TARGET_PAGE_SIZE) {
            if (!buffer_is_zero(host + at, TARGET_PAGE_SIZE)) {
                fseeko(ram, at, SEEK_SET);
                fwrite(host + at, 1, TARGET_PAGE_SIZE, ram);
            }
        }
        if (ram) {
            ftruncate(fileno(ram), hv.ram_bytes);
            fclose(ram);
        }
        _exit(0);
    }
    if ((off || reset) && kernel && !hv.shutdown_dumped) { /* the kernel's data as it stands now: its message buffer, and any panic string */
        g_autofree char *path = g_strdup_printf("%s.shutdown", kernel);
        g_autofree char *data_path = g_strdup_printf("%s.shutdown.data", kernel);
        FILE *data = fopen(data_path, "wb");
        uint8_t page[4096];

        hv.shutdown_dumped = true;
        hvdbt_dump_kernel(cs, path);
        /* All of __DATA from its load address, its zero-fill too, which a Mach-O doesn't hold: that is where the message buffer and panic string live. */
        for (uint64_t done = 0; data && done < hv.kernel_data_bytes; done += sizeof page) {
            if (cpu_memory_rw_debug(cs, hv.kernel_data + done, page, sizeof page, false)) {
                memset(page, 0, sizeof page);
            }
            fwrite(page, 1, sizeof page, data);
        }
        if (data) {
            fclose(data);
        }
    }
}

static void hvdbt_port_write(void *opaque, uint16_t port, unsigned size, uint32_t value)
{
    hvdbt_watch_power_ports(opaque, port, size, value);
    bql_lock();
    address_space_write(&address_space_io, port, MEMTXATTRS_UNSPECIFIED, &value, size);
    hvdbt_after_device(opaque);
    bql_unlock();
}

/* The monitor keeps the syscall MSRs and sends the rest back as fallbacks, so these never run in hybrid mode. */
static bool hvdbt_read_msr(void *opaque, uint32_t msr, uint64_t *value)
{
    return false;
}

/* What helper_wrmsr does for the only MSR the monitor brings here. False: TCG takes the instruction, and its #GP if the CPU has no such timer. */
static bool hvdbt_write_msr(void *opaque, uint32_t msr, uint64_t value)
{
    /* Keep the product's current APIC/timer model. The fork's deadline-timer
     * extension is separate from the CPU handoff and is not advertised here.
     * TCG handles both the MSR and any architectural fault. */
    return false;
}

static void hvdbt_cpuid(void *opaque, uint32_t leaf, uint32_t subleaf, uint32_t out[4])
{
    CPUState *cs = opaque;
    cpu_x86_cpuid(cpu_env(cs), leaf, subleaf, &out[0], &out[1], &out[2], &out[3]);
}

static bool hvdbt_has_interrupt(void *opaque)
{
    CPUState *cs = opaque;
    bool hard;

    if (!cpu_test_interrupt(cs, CPU_INTERRUPT_POLL | CPU_INTERRUPT_HARD | OTHER_INTERRUPTS)) { /* most kicks: not worth waiting for the BQL to learn it */
        return false;
    }
    bql_lock();
    if (cpu_test_interrupt(cs, CPU_INTERRUPT_POLL)) { /* what the IO-APIC raises from another thread, as x86_cpu_exec_interrupt handles it */
        cpu_reset_interrupt(cs, CPU_INTERRUPT_POLL);
        apic_poll_irq(X86_CPU(cs)->apic_state);
    }
    if (cpu_test_interrupt(cs, OTHER_INTERRUPTS)) { /* TCG delivers these, when the boundary stop gets back to its loop */
        engine_request_boundary(HV(cs)->engine);
    }
    hard = cpu_test_interrupt(cs, CPU_INTERRUPT_HARD);
    bql_unlock();
    return hard;
}

static int hvdbt_acknowledge_interrupt(void *opaque)
{
    CPUState *cs = opaque;
    int vector;

    bql_lock();
    cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    vector = cpu_get_pic_interrupt(cpu_env(cs));
    bql_unlock();
    return vector;
}

/* hlt comes back as a fallback, and the APIC's EOI and ICR are MMIO to QEMU's model. */
static bool hvdbt_wait(void *opaque, uint64_t deadline_ns)
{
    return false;
}

static void hvdbt_apic_eoi(void *opaque, uint8_t vector)
{
}

/* The write to the low word, with the high word from the monitor's copy: see hybrid_icr_hi. */
static void hvdbt_apic_icr(void *opaque, uint64_t icr)
{
    CPUState *cs = opaque;
    APICCommonState *apic = X86_CPU(cs)->apic_state;

    bql_lock();
    apic->icr[1] = icr >> 32;
    hvdbt_name_device(apic->apicbase & MSR_IA32_APICBASE_BASE);
    uint32_t low = icr;
    address_space_write(&address_space_memory, (apic->apicbase & MSR_IA32_APICBASE_BASE) + 0x300, MEMTXATTRS_UNSPECIFIED, &low, 4);
    hvdbt_after_device(opaque);
    bql_unlock();
}

/* ---- State ---- */

static bool hvdbt_read_descriptor(CPUState *cs, uint16_t sel, uint32_t *e, unsigned words)
{
    CPUX86State *env = cpu_env(cs);
    uint64_t table = sel & 4 ? env->ldt.base : env->gdt.base;
    uint8_t bytes[16];

    if (cpu_memory_rw_debug(cs, table + (sel & ~7), bytes, words * 4, false)) {
        return false;
    }
    for (unsigned i = 0; i < words; i++) {
        e[i] = ldl_le_p(bytes + i * 4);
    }
    return true;
}

static uint64_t hvdbt_desc_base(const uint32_t *e)
{
    return (e[0] >> 16) | ((e[1] & 0xff) << 16) | (e[1] & 0xff000000);
}

static uint32_t hvdbt_desc_limit(const uint32_t *e)
{
    uint32_t limit = (e[0] & 0xffff) | (e[1] & 0x000f0000);
    return e[1] & DESC_G_MASK ? (limit << 12) | 0xfff : limit;
}

/* Only descriptors for a changed selector are read. A change of GDT contents under the same selector is not noticed. */
static void hvdbt_load_segment(CPUState *cs, X86Seg seg, uint16_t sel, uint32_t cpl)
{
    CPUX86State *env = cpu_env(cs);
    uint32_t e[2] = { 0, 0 };

    if (env->segs[seg].selector == sel && (seg != R_SS || ((env->segs[R_SS].flags >> DESC_DPL_SHIFT) & 3) == cpl)) {
        return;
    }
    if (sel & ~3) {
        hvdbt_read_descriptor(cs, sel, e, 2);
    }
    if (seg == R_SS) { /* CPL is what TCG reads from here, and the engine keeps SS null in ring 0 */
        e[1] = (e[1] & ~DESC_DPL_MASK) | (cpl << DESC_DPL_SHIFT);
    }
    cpu_x86_load_seg_cache(env, seg, sel, hvdbt_desc_base(e), hvdbt_desc_limit(e), e[1]);
}

/* Both target-CPL policies must match seg_note in the monitor, including
 * when a 64-bit XNU trampoline preloads the next mode's selectors. */
static uint32_t hvdbt_seg_bad(CPUX86State *env)
{
    static const int regs[] = { R_ES, R_DS, R_FS, R_GS };
    uint32_t bad = 0;

    for (int i = 0; i < ARRAY_SIZE(regs); i++) {
        const SegmentCache *s = &env->segs[regs[i]];
        bad |= engine_compat_segment_bad(regs[i],s->selector,s->flags,s->base,s->limit);
    }
    return bad;
}

static void hvdbt_put_state(CPUState *cs)
{
    CPUX86State *env = cpu_env(cs);
    /* TCG owns the architectural state here. Every input to engine_set_state
     * is populated below; the engine's internal APIC state stays in place.
     * Don't read the old FPSR and 16 SIMD registers just to overwrite them. */
    EngineX86State s = { 0 };

    memcpy(s.gpr, env->regs, sizeof s.gpr);
    {
        APICCommonState *apic = X86_CPU(cs)->apic_state;
        bool mapped = apic && (apic->apicbase & MSR_IA32_APICBASE_ENABLE) && !(apic->apicbase & MSR_IA32_APICBASE_EXTD);
        s.hybrid_apic_page = mapped ? apic->apicbase & MSR_IA32_APICBASE_BASE : 0;
        s.hybrid_icr_lo = apic ? apic->icr[0] : 0;
        s.hybrid_icr_hi = apic ? apic->icr[1] : 0;
    }
    s.rip = env->eip;
    s.rflags = env->eflags; /* cpu_exec_exit made it whole */
    s.cr0 = env->cr[0];
    s.cr2 = env->cr[2];
    s.cr3 = env->cr[3];
    s.cr4 = env->cr[4];
    s.efer = env->efer;
    s.star = env->star;
    s.lstar = env->lstar;
    s.cstar = env->cstar;
    s.sfmask = env->fmask;
    s.fs_base = env->segs[R_FS].base;
    s.gs_base = env->segs[R_GS].base;
    s.kernel_gs_base = env->kernelgsbase;
    s.gdt_base = env->gdt.base;
    s.gdt_limit = env->gdt.limit;
    s.idt_base = env->idt.base;
    s.idt_limit = env->idt.limit;
    s.tss_base = env->tr.base;
    s.tss_limit = env->tr.limit;
    for (int i = 0; i < 6; i++) {
        s.sel[i] = env->segs[i].selector; /* the same order: es cs ss ds fs gs */
    }
    s.tr = env->tr.selector;
    s.ldtr = env->ldt.selector;
    s.ldt_base = env->ldt.base;
    s.ldt_limit = env->ldt.limit;
    s.compat = !(env->hflags & HF_CS64_MASK);
    s.seg_bad = hvdbt_seg_bad(env);
    s.sysenter_cs = env->sysenter_cs;
    s.sysenter_esp = env->sysenter_esp;
    s.sysenter_eip = env->sysenter_eip;
    s.cpl = env->hflags & HF_CPL_MASK;
    update_mxcsr_from_sse_status(env);
    s.mxcsr = env->mxcsr;
    for (int i = 0; i < 16; i++) {
        s.xmm[i][0] = env->xmm_regs[i].ZMM_Q(0);
        s.xmm[i][1] = env->xmm_regs[i].ZMM_Q(1);
    }
    s.fp_tags = 0;
    for (int i = 0; i < 8; i++) {
        s.mm[i] = env->fpregs[i].mmx.MMX_Q(0);
        s.fp_exp[i] = env->fpregs[i].d.high;
        s.fp_tags |= !env->fptags[i] << i;
    }
    s.fp_top = env->fpstt;
    s.fcw = env->fpuc;
    s.fsw = env->fpus & ~0x3800;
    s.fop = env->fpop;
    s.fcs = env->fpcs;
    s.fds = env->fpds;
    s.fip = env->fpip;
    s.fdp = env->fpdp;
    engine_set_state(HV(cs)->engine, &s);

    /* The TSC is the same clock on both sides, cpu_get_tsc's steps with cpu_get_tsc's offsets. */
    uint32_t khz = env->user_tsc_khz;
    uint64_t num = khz > 1000000 ? khz - 1000000 : 0, den = 1000000, g = den;
    for (uint64_t r = num; r; ) { /* gcd: keeps ns * num in range, and 1.2 GHz is exactly floor(ns / 5), as cpu_get_tsc has it */
        uint64_t next = g % r;
        g = r;
        r = next;
    }
    /* Do not estimate this offset by subtracting separate clock reads.
     * Preemption/lock contention between those reads creates a different
     * negative bias on every CPU and hand-off, allowing time to run backwards
     * in XNU's scheduler. Use the VM clock's actual shared offset. */
    engine_set_tsc(HV(cs)->engine, cpu_get_ticks_offset(), env->tsc_offset, num / g, den / g);
}

static void hvdbt_get_state(CPUState *cs)
{
    CPUX86State *env = cpu_env(cs);
    EngineX86State s;

    engine_get_state(HV(cs)->engine, &s);
    memcpy(env->regs, s.gpr, sizeof s.gpr);
    if (X86_CPU(cs)->apic_state) { /* TCG may be the one to read them next, or to finish an IPI begun here */
        X86_CPU(cs)->apic_state->icr[0] = s.hybrid_icr_lo;
        X86_CPU(cs)->apic_state->icr[1] = s.hybrid_icr_hi;
    }
    env->eip = s.rip;
    env->eflags = s.rflags; /* cpu_exec_enter splits it again */
    env->cr[0] = s.cr0;
    env->cr[2] = s.cr2;
    env->cr[3] = s.cr3;
    env->cr[4] = s.cr4;
    env->efer = s.efer | MSR_EFER_LMA;
    env->star = s.star;
    env->lstar = s.lstar;
    env->cstar = s.cstar;
    env->fmask = s.sfmask;
    env->sysenter_cs = s.sysenter_cs;
    env->sysenter_esp = s.sysenter_esp;
    env->sysenter_eip = s.sysenter_eip;
    env->kernelgsbase = s.kernel_gs_base;
    env->gdt.base = s.gdt_base;
    env->gdt.limit = s.gdt_limit;
    env->idt.base = s.idt_base;
    env->idt.limit = s.idt_limit;
    env->hflags |= HF_LMA_MASK; /* for the page walks below */

    if (env->tr.selector != s.tr) {
        env->tr.selector = s.tr;
        env->tr.flags = DESC_P_MASK | (11 << DESC_TYPE_SHIFT);
    }
    env->tr.base = s.tss_base;
    env->tr.limit = s.tss_limit;
    if (env->ldt.selector != s.ldtr) { /* the monitor's lldt keeps only the selector */
        uint32_t e[4] = { 0, 0, 0, 0 };
        if (s.ldtr & ~3) {
            hvdbt_read_descriptor(cs, s.ldtr & ~4, e, 4);
        }
        cpu_x86_load_seg_cache(env, R_LDTR, s.ldtr, hvdbt_desc_base(e) | ((uint64_t)e[2] << 32), hvdbt_desc_limit(e), e[1]);
    }
    env->ldt.base = s.ldt_base;
    env->ldt.limit = s.ldt_limit;
    for (int i = 0; i < 6; i++) {
        hvdbt_load_segment(cs, i, s.sel[i], s.cpl);
    }
    /* sysenter and sysexit take these from the instruction, whatever the table says of the selector. */
    env->segs[R_CS].flags = (env->segs[R_CS].flags & ~(DESC_L_MASK | DESC_B_MASK)) | (s.compat ? DESC_B_MASK : DESC_L_MASK);
    if (s.compat) {
        env->segs[R_SS].flags |= DESC_B_MASK;
    }
    env->segs[R_FS].base = s.fs_base;
    env->segs[R_GS].base = s.gs_base;
    x86_update_hflags(env);
    env->hflags = (env->hflags & ~HF_INHIBIT_IRQ_MASK) |
        (s.irq_shadow ? HF_INHIBIT_IRQ_MASK : 0);

    cpu_set_mxcsr(env, s.mxcsr);
    for (int i = 0; i < 16; i++) {
        env->xmm_regs[i].ZMM_Q(0) = s.xmm[i][0];
        env->xmm_regs[i].ZMM_Q(1) = s.xmm[i][1];
    }
    for (int i = 0; i < 8; i++) {
        env->fpregs[i].mmx.MMX_Q(0) = s.mm[i];
        env->fpregs[i].d.high = s.fp_exp[i];
        env->fptags[i] = !(s.fp_tags >> i & 1);
    }
    env->fpstt = s.fp_top;
    cpu_set_fpuc(env, s.fcw);
    env->fpus = s.fsw;
    env->fpop = s.fop;
    env->fpcs = s.fcs;
    env->fpds = s.fds;
    env->fpip = s.fip;
    env->fpdp = s.fdp;
    /* shortcut: a whole TLB flush at every return to TCG, since guest tables changed unseen. Measure before narrowing it. */
    tlb_flush(cs);
}

/* ---- The other direction: TCG and DMA writing what the engine translated or shadows ---- */

static void hvdbt_note_write(uint64_t addr, uint64_t len, bool by_tcg)
{
    EngineHandle *engine = __atomic_load_n(&hv.engine, __ATOMIC_ACQUIRE);

    if (!engine || !len || addr < hv.ram_base || addr >= hv.ram_base + hv.ram_bytes) {
        return;
    }
    if (by_tcg) {
        HvCpu *cpu = HV(current_cpu);
        if (!cpu->protect_all) {
            hvdbt_write_span_note(&cpu->written, addr,
                MIN(len, hv.ram_bytes - (addr - hv.ram_base)), TARGET_PAGE_SIZE);
        }
    }
    uint64_t off = addr - hv.ram_base;
    if (off < hv.below_4g) {
        uint64_t n = MIN(len, hv.below_4g - off);
        engine_note_write(engine, off, n);
        off += n;
        len -= n;
    }
    if (len) {
        engine_note_write(engine, HIGH_RAM + off - hv.below_4g, len);
    }
}

/* Every TCG write to RAM must hit notdirty_write, or the engine won't hear of it. That is what a clean page does; it stays dirty once written. */
static void hvdbt_protect_written(HvCpu *cpu)
{
    if (cpu->protect_all) {
        physical_memory_test_and_clear_dirty(hv.ram_base, hv.ram_bytes, DIRTY_MEMORY_CODE, NULL);
    } else if (cpu->written.pending) {
        RAMDirtyRange ranges[4];
        for (unsigned i = 0; i < cpu->written.count; i++) {
            ranges[i].start = cpu->written.ranges[i].first;
            ranges[i].length = cpu->written.ranges[i].last - ranges[i].start + TARGET_PAGE_SIZE;
        }
        physical_memory_test_and_clear_dirty_ranges(ranges, cpu->written.count,
                                                    DIRTY_MEMORY_CODE);
    }
    cpu->protect_all = false;
    cpu->written.pending = false;
}

/* ---- Hand-off ---- */

static bool hvdbt_mark_device(Int128 start, Int128 len, const MemoryRegion *mr, hwaddr offset, void *opaque)
{
    MachineState *ms = opaque;
    uint64_t gpa = int128_get64(start), bytes = int128_get64(len);
    uint64_t expect = gpa >= HIGH_RAM ? gpa - HIGH_RAM + hv.below_4g : gpa;

    if (mr != ms->ram || offset != expect) {
        engine_mark_device(hv.engine, gpa, bytes);
    }
    return false;
}

static void hvdbt_write_tally_at_exit(void)
{
    for (int i = 0; i < HVDBT_MAX_CPUS; i++) {
        if (hv_cpus[i].engine) {
            engine_tally_write(hv_cpus[i].engine);
        }
    }
}

static void hvdbt_set_ops(EngineMachineOps *ops, CPUState *cs)
{
    *ops = (EngineMachineOps) {
        .opaque = cs,
        .mmio_read = hvdbt_mmio_read,
        .mmio_write = hvdbt_mmio_write,
        .port_read = hvdbt_port_read,
        .port_write = hvdbt_port_write,
        .read_msr = hvdbt_read_msr,
        .write_msr = hvdbt_write_msr,
        .cpuid = hvdbt_cpuid,
        .acknowledge_interrupt = hvdbt_acknowledge_interrupt,
        .has_interrupt_waiting = hvdbt_has_interrupt,
        .wait_for_interrupt = hvdbt_wait,
        .apic_eoi = hvdbt_apic_eoi,
        .apic_icr = hvdbt_apic_icr,
    };
}

static void *hvdbt_watchdog(void *arg);
static void hvdbt_find_kernel(CPUState *cs);

/* A reset, which a snapshot load also starts with: RAM is about to change with nothing telling the engine. vCPUs are paused. */
static void hvdbt_reset(void *opaque)
{
    engine_forget_all(hv.engine);
    for (int i = 0; i < HVDBT_MAX_CPUS; i++) {
        hv_cpus[i].until_kernel = true;
        hv_cpus[i].protect_all = true;
        hv_cpus[i].fallback_quantum = false;
        hv_cpus[i].fallback_left = 0;
        hv_cpus[i].transition_guard = false;
    }
}

static bool hvdbt_create(CPUState *cs)
{
    MachineState *ms = current_machine;
    X86MachineState *x86ms = X86_MACHINE(ms);
    CPUX86State *env = cpu_env(cs);
    const char *tally = getenv("LC_HVDBT_TALLY");

    if (x86ms->above_4g_mem_size && x86ms->above_4g_mem_start != HIGH_RAM) {
        error_report("hvdbt: RAM above 4 GB starts at 0x%" PRIx64 ": staying on TCG", x86ms->above_4g_mem_start);
        return false;
    }
    EngineConfig config = {
        .ram_bytes = ms->ram_size,
        .ram_host = memory_region_get_ram_ptr(ms->ram),
        .ram_below_4g = x86ms->above_4g_mem_size ? x86ms->below_4g_mem_size : 0,
        .kernel_model = true,
        .precise_flags = true,
        .tso = true,
        .tzcnt_lzcnt = (env->features[FEAT_7_0_EBX] & CPUID_7_0_EBX_BMI1) && (env->features[FEAT_8000_0001_ECX] & CPUID_EXT3_ABM),
        .hybrid = true,
    };
    EngineMachineOps ops;
    hvdbt_set_ops(&ops, cs);
    EngineHandle *engine = engine_create(&config, &ops);
    if (!engine) {
        error_report("hvdbt: the engine would not start: staying on TCG");
        return false;
    }
    engine_prepare_count(engine, ms->smp.max_cpus); /* before it runs: the monitor's lock turns on from this */
    hv.ram_base = memory_region_get_ram_addr(ms->ram);
    hv.ram_bytes = ms->ram_size;
    hv.below_4g = x86ms->above_4g_mem_size ? x86ms->below_4g_mem_size : ms->ram_size;
    HV(cs)->protect_all = true;
    HV(cs)->engine = engine;
    hv.engine = engine; /* hvdbt_mark_device reads it, and nothing else can until the release below */

    /* shortcut: the memory map as it stands at hand-off. A BAR or PAM change after this isn't seen; a low-RAM remap would need a MemoryListener. */
    WITH_RCU_READ_LOCK_GUARD() {
        flatview_for_each_range(address_space_to_flatview(&address_space_memory), hvdbt_mark_device, ms);
    }
    qemu_register_reset(hvdbt_reset, NULL);
    __atomic_store_n(&hv.engine, engine, __ATOMIC_RELEASE);
    if (!getenv("LC_HVDBT_NO_DIRECT")) {
        BQL_LOCK_GUARD();
        memory_listener_register(&hvdbt_direct_listener, &address_space_memory);
    }
    if (tally) {
        engine_tally_start(engine, tally);
        atexit(hvdbt_write_tally_at_exit);
    }
    hvdbt_find_kernel(cs);
    if (getenv("LC_HVDBT_KERNEL")) {
        hvdbt_dump_kernel(cs, getenv("LC_HVDBT_KERNEL"));
    }
    if (getenv("VIM_HYBRID_WATCHDOG")) {
        QemuThread watchdog;
        qemu_thread_create(&watchdog, "hvdbt-watchdog", hvdbt_watchdog, cs, QEMU_THREAD_DETACHED);
    }
    info_report("hvdbt: the engine takes over at rip 0x%" PRIx64 ", cr3 0x%" PRIx64, (uint64_t)env->eip, (uint64_t)env->cr[3]);
    return true;
}

/* Another vCPU joins the VM on its own thread, as HVF requires. The first made it and said how many there would be. */
static bool hvdbt_create_secondary(CPUState *cs)
{
    const char *tally = getenv("LC_HVDBT_TALLY");
    EngineMachineOps ops;

    hvdbt_set_ops(&ops, cs);
    HvCpu *cpu = HV(cs);
    cpu->engine = engine_create_secondary(hv.engine, cs->cpu_index, &ops);
    if (!cpu->engine) {
        error_report("hvdbt: vCPU %d would not join the engine", cs->cpu_index);
        return false;
    }
    cpu->protect_all = true;
    if (tally) {
        g_autofree char *path = g_strdup_printf("%s.cpu%d", tally, cs->cpu_index);
        engine_tally_start(cpu->engine, path);
    }
    if (getenv("VIM_HYBRID_WATCHDOG")) {
        QemuThread watchdog;
        qemu_thread_create(&watchdog, "hvdbt-watchdog", hvdbt_watchdog, cs, QEMU_THREAD_DETACHED);
    }
    info_report("hvdbt: vCPU %d joins at rip 0x%" PRIx64, cs->cpu_index, (uint64_t)cpu_env(cs)->eip);
    return true;
}

static bool hvdbt_want_engine(CPUState *cs, vaddr pc, uint32_t flags)
{
    /* Do not mix extra untracked TCG CPUs with the bounded engine arrays. */
    if (cs->cpu_index >= HVDBT_MAX_CPUS || current_machine->smp.max_cpus > HVDBT_MAX_CPUS) {
        return false;
    }
    CPUX86State *env = cpu_env(cs);
    HvCpu *cpu = HV(cs);
    bool ok;

    if (hv.dead) {
        return false;
    }
    /*
     * Firmware and boot loaders run identity-mapped, and XNU's vstart is the first thing in the high half. An application processor gets there through TCG's real mode, and
     * waits on TCG until the first vCPU has made the VM.
     */
    if (!cpu->engine && ((int64_t)pc >= 0 || (cs->cpu_index && !__atomic_load_n(&hv.engine, __ATOMIC_ACQUIRE)))) {
        cpu->tcg_ran = true;
        return false;
    }
    if (cpu->until_kernel) {
        if ((int64_t)pc >= 0) {
            cpu->tcg_ran = true;
            return false;
        }
        cpu->until_kernel = false;
    }
    if (current_machine->smp.max_cpus > HVDBT_MAX_CPUS || !qemu_tcg_mttcg_enabled()) {
        error_report("hvdbt: needs at most %d vCPUs and -accel tcg,thread=multi", HVDBT_MAX_CPUS);
        exit(EXIT_FAILURE);
    }
    char why[sizeof cpu->refused];
    bool guarded_transition = false;
    why[0] = 0;
    if (flags & HF_INHIBIT_IRQ_MASK) {
        /* The input ABI contains RFLAGS but not TCG's one-instruction
         * interrupt shadow. Finish the protected instruction in TCG
         * rather than admitting IRQs early on return to the engine. */
        snprintf(why, sizeof why, "refused: interrupt shadow");
        guarded_transition = true;
    } else if (!(flags & HF_CS64_MASK)) {
        const SegmentCache *cseg = &env->segs[R_CS], *sseg = &env->segs[R_SS];
        if (!(flags & HF_LMA_MASK)) {
            snprintf(why, sizeof why, "refused: not in long mode");
        } else if (!(flags & HF_CS32_MASK) || !(flags & HF_SS32_MASK)) {
            snprintf(why, sizeof why, "refused: 16-bit code or stack");
            guarded_transition = (flags & HF_CS32_MASK) && !(flags & HF_CPL_MASK);
        } else if ((flags & HF_CPL_MASK) != 0 && (flags & HF_CPL_MASK) != 3) {
            snprintf(why, sizeof why, "refused: compatibility mode in ring 1 or 2");
        } else if (cseg->base || cseg->limit != 0xffffffff || sseg->base || sseg->limit != 0xffffffff || (sseg->flags & DESC_E_MASK) ||
                   engine_compat_segments_bad(hvdbt_seg_bad(env),flags & HF_CPL_MASK)) {
            snprintf(why, sizeof why, "refused: compatibility mode, non-flat segments");
            guarded_transition = true;
        }
    }
    if (why[0]) {
        /* said above */
    } else if (flags & HF_SMM_MASK) {
        snprintf(why, sizeof why, "refused: SMM");
    } else if (flags & TF_MASK) {
        snprintf(why, sizeof why, "refused: TF set");
    } else if (!(env->cr[0] & CR0_WP_MASK)) {
        snprintf(why, sizeof why, "refused: CR0.WP clear");
    } else if (env->cr[4] & ~ENGINE_CR4) {
        snprintf(why, sizeof why, "refused: CR4 bits 0x%" PRIx64, (uint64_t)env->cr[4] & ~ENGINE_CR4);
    } else if ((env->mxcsr & 0x1F80) != 0x1F80) {
        snprintf(why, sizeof why, "refused: unmasked SSE exceptions");
    } else if (!QTAILQ_EMPTY(&cs->breakpoints) || cpu_single_stepping(cs)) {
        snprintf(why, sizeof why, "refused: debugger attached");
    }
    ok = !why[0];
    if (!ok) {
        /* i386 XNU's copyin/out and user-space selector changes can both
         * leave the engine's flat segment model. TCG must implement those
         * semantics, but it need not leave ordinary unguarded blocks behind.
         * The existing hand-off guard
         * validates actual decoded bytes before every unchained execution.
         * Keep this ownership through the temporary state, then return to
         * the engine without a global TB flush/exclusive stop on every copy. */
        if (cpu->engine && guarded_transition) {
            cpu->transition_guard = true;
        }
        if (!cpu->transition_guard) {
            cpu->tcg_ran = true;
        }
    }
    if (strcmp(why, cpu->refused)) { /* on a change only: this runs at each TB miss while on TCG */
        strcpy(cpu->refused, why);
        if (!ok && cpu->engine) {
            engine_tally_refused(cpu->engine, why);
        }
    }
    return ok;
}

static void hvdbt_finish(CPUState *cs)
{
    hv.dead = true;
    cpu_dump_state(cs, stderr, CPU_DUMP_CODE | CPU_DUMP_CCOP);
    hvdbt_write_tally_at_exit();
    vm_stop(RUN_STATE_INTERNAL_ERROR);
}

/* Keep the soft TLB and compiled blocks while TCG owns dense VEX work.
 * Every block remains guarded and unchained, even across exceptions or a
 * temporary state the engine cannot accept. Interrupts are normal after the
 * first forced fallback. Exact debugger/icount/SMC requests keep their count. */
static uint32_t hvdbt_select_cflags(CPUState *cs, vaddr pc, uint32_t flags,
                                   uint32_t cflags, bool exact, bool *handoff)
{
    if (cs->cpu_index >= HVDBT_MAX_CPUS) {
        return cflags;
    }
    HvCpu *cpu = HV(cs);
    if (!cpu->fallback_quantum && !cpu->transition_guard) {
        return cflags;
    }
    if ((cpu->transition_guard || !cpu->fallback_left) && !exact) {
        if (hvdbt_want_engine(cs, pc, flags)) {
            /* Keep the request armed until hvdbt_run actually enters the
             * engine; pending work can make it return without running. */
            *handoff = true;
            return cflags;
        }
        if (!cpu->transition_guard) {
            cpu->fallback_left = 65536;
        }
    }
    unsigned count = cflags & CF_COUNT_MASK;
    return (cflags & ~CF_COUNT_MASK) | CF_HANDOFF_GUARD |
        CF_NO_GOTO_TB | CF_NO_GOTO_PTR | (count ? count : 256);
}

static void hvdbt_completed_tb(CPUState *cs, unsigned instructions)
{
    if (cs->cpu_index < HVDBT_MAX_CPUS) {
        HvCpu *cpu = HV(cs);
        if (cpu->fallback_quantum) {
            cpu->fallback_left -= MIN(cpu->fallback_left, instructions);
        }
    }
}

/* Run the unsupported instruction without an interrupt ahead of it. Dense
 * VEX workloads otherwise retranslate/invalidate a whole page for every AVX
 * instruction (for example XNU's AVX2 SHA routines). Let TCG execute a bounded
 * block there instead; normal control-flow/fault exits still end the block.
 * Other fallbacks retain their single-instruction hand-off. */
static void hvdbt_prepare_step(CPUState *cs)
{
    CPUX86State *env = cpu_env(cs);
    unsigned instructions = 1;

    TranslateForDebugResult page;
    if ((env->hflags & HF_CS64_MASK) && !(env->hflags & HF_INHIBIT_IRQ_MASK) &&
        cpu_translate_for_debug(cs, env->eip & TARGET_PAGE_MASK, &page)) {
        uint64_t off = page.physaddr >= HIGH_RAM ? page.physaddr - HIGH_RAM + hv.below_4g : page.physaddr;
        if ((page.physaddr >= HIGH_RAM || page.physaddr < hv.below_4g) && off < hv.ram_bytes) {
            uint64_t byte = off + (env->eip & ~TARGET_PAGE_MASK);
            if (byte < hv.ram_bytes) {
                uint8_t *ram = memory_region_get_ram_ptr(current_machine->ram);
                uint8_t opcode = qatomic_read(&ram[byte]);
                if (opcode == 0xc4 || opcode == 0xc5) {
                    instructions = 256;
                    HV(cs)->fallback_quantum = true;
                    HV(cs)->fallback_left = 65536;
                }
            }
        }
    }
    /* A cached fallback is checked against the bytes actually decoded for
     * that TB before every execution. It is never chained or entered as a
     * normal TCG block, even if another CPU compiles the same address. */
    cs->cflags_next_tb = (cs->tcg_cflags & ~CF_COUNT_MASK) | CF_NOIRQ |
        CF_NO_GOTO_TB | CF_NO_GOTO_PTR | CF_HANDOFF_GUARD | instructions;
}

static void hvdbt_run(CPUState *cs)
{
    HvCpu *cpu = HV(cs);
    EngineRunResult result;

    if (!cpu->engine && !(hv.engine ? hvdbt_create_secondary(cs) : hvdbt_create(cs))) {
        hv.dead = true;
        return;
    }
    if (cpu->tcg_ran) { /* the flush runs from the work queue, then the TB miss brings control back. It is exclusive: with several vCPUs, it stops the others at a boundary */
        cpu->tcg_ran = false;
        queue_tb_flush(cs);
    }
    hvdbt_protect_written(cpu);
    qatomic_set_mb(&cpu->in_engine, true);
    if (!cpu_can_run(cs) || !cpu_work_list_empty(cs)) {
        qatomic_set(&cpu->in_engine, false);
        return;
    }
    cpu->fallback_quantum = false;
    cpu->fallback_left = 0;
    cpu->transition_guard = false;
    hvdbt_put_state(cs);
    engine_set_irq_line(cpu->engine, cpu_test_interrupt(cs, CPU_INTERRUPT_HARD | CPU_INTERRUPT_POLL));

    /*
     * Between these, start_exclusive waits for this vCPU as for one running TBs: an atomic step or a TB flush elsewhere finds it stopped at a boundary. It kicks it, and the kick
     * below turns that into a stop. cpu_exec_start itself waits while an exclusive section is on.
     */
    bql_unlock();
    cpu_exec_start(cs);
    result = engine_run(cpu->engine);
    cpu_exec_end(cs);
    bql_lock();
    qatomic_set(&cpu->in_engine, false);
    hvdbt_get_state(cs);

    switch (result) {
    case ENGINE_RUN_FALLBACK:
        hvdbt_prepare_step(cs);
        break;
    case ENGINE_RUN_BOUNDARY: /* whatever asked for the stop is in TCG's loop now */
        break;
    case ENGINE_RUN_ERROR:
        error_report("hvdbt: %s", engine_error_text(cpu->engine));
        hvdbt_finish(cs);
        break;
    default:
        error_report("hvdbt: engine_run returned %d", result);
        hvdbt_finish(cs);
        break;
    }
}

/* Interrupts wake the monitor. A stop, queued work or another vCPU's exclusive section need the state in QEMU's hands. */
static void hvdbt_kick(CPUState *cs)
{
    if (cs->cpu_index >= HVDBT_MAX_CPUS) {
        return;
    }
    HvCpu *cpu = HV(cs);

    if (!qatomic_read(&cpu->in_engine) || !cpu->engine) {
        return;
    }
    if (cpu_exclusive_pending()) {
        engine_count_exclusive_stop(cpu->engine);
        engine_request_boundary(cpu->engine);
    } else if (qatomic_read(&cs->stop) || !cpu_work_list_empty(cs)) {
        engine_request_boundary(cpu->engine);
    } else {
        engine_set_irq_line(cpu->engine, true);
        engine_kick(cpu->engine);
    }
}

/* ---- Stall report ---- */

static void hvdbt_dump_bytes(CPUState *cs, const char *name, uint64_t at, int before, int after)
{
    uint64_t start = (at - before) & ~15ULL;

    fprintf(stderr, "%s, around 0x%" PRIx64 ":\n", name, at);
    for (uint64_t line = start; line < at + after; line += 16) {
        uint8_t b[16];
        fprintf(stderr, "%016" PRIx64 ":", line);
        if (cpu_memory_rw_debug(cs, line, b, 16, false)) {
            fprintf(stderr, " unmapped\n");
            continue;
        }
        for (int i = 0; i < 16; i++) {
            fprintf(stderr, " %02x", b[i]);
        }
        fprintf(stderr, "\n");
    }
}

/* Runs from the work queue, so on the vCPU thread with the state in CPUX86State: a kick brought it off the engine, or out of TCG. */
static void hvdbt_stall_report(CPUState *cs, run_on_cpu_data unused)
{
    X86CPU *cpu = X86_CPU(cs);
    CPUX86State *env = &cpu->env;
    APICCommonState *apic = cpu->apic_state;
    char text[2048];

    qatomic_set(&HV(cs)->dump_done, true);
    engine_debug_text(HV(cs)->engine, text, sizeof text);
    fprintf(stderr, "\nhvdbt: no progress for 5 s. Last off the engine because: %s\n%s", HV(cs)->refused[0] ? HV(cs)->refused : "a stop to take this report", text);
    cpu_dump_state(cs, stderr, CPU_DUMP_CODE | CPU_DUMP_CCOP);
    hvdbt_dump_bytes(cs, "code", env->eip, 64, 64);
    hvdbt_dump_bytes(cs, "stack", env->regs[R_ESP], 0, 64);
    fprintf(stderr, "cpu: halted %d, interrupt_request 0x%x, IF %d, CR4 bits the engine won't take 0x%" PRIx64 "\n", cs->halted,
            qatomic_read(&cs->interrupt_request), !!(env->eflags & IF_MASK), (uint64_t)env->cr[4] & ~ENGINE_CR4);
    fprintf(stderr, "TSC by QEMU 0x%" PRIx64 ", tsc_deadline 0x%" PRIx64 "\n", cpu_get_tsc(env) + env->tsc_offset, (uint64_t)env->tsc_deadline);
    if (apic) {
        fprintf(stderr, "apic: base 0x%x tpr 0x%x svr 0x%x lvt-timer 0x%x initial-count 0x%x timer %s next %" PRId64 " now %" PRId64 "\n", apic->apicbase,
                apic->tpr, apic->spurious_vec, apic->lvt[APIC_LVT_TIMER], apic->initial_count, timer_pending(apic->timer) ? "pending" : "idle",
                apic->next_time, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
        fprintf(stderr, "apic irr/isr, high word first:");
        for (int i = 7; i >= 0; i--) {
            fprintf(stderr, " %08x/%08x", apic->irr[i], apic->isr[i]);
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

/* ---- For looking at a guest that has no other way out: LC_HVDBT_SCREEN and LC_HVDBT_KERNEL ---- */

/* On the main loop, BQL held. A numbered PPM, so a series shows whether the screen moves. */
static void hvdbt_screen_bh(void *opaque)
{
    static int number;
    const char *path = opaque;
    QemuConsole *con = qemu_console_lookup_by_index(0);
    DisplaySurface *surface;

    if (!con) {
        return;
    }
    qemu_console_hw_update(con);
    surface = qemu_console_surface(con);
    if (!surface) {
        return;
    }
    g_autofree char *name = g_strdup_printf("%s.%03d.ppm", path, number++);
    FILE *f = fopen(name, "wb");
    if (!f) {
        return;
    }
    int width = surface_width(surface), height = surface_height(surface);
    pixman_image_t *line = qemu_pixman_linebuf_create(PIXMAN_BE_r8g8b8, width);
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; y++) {
        qemu_pixman_linebuf_fill(line, surface->image, width, 0, y);
        fwrite(pixman_image_get_data(line), 3, width, f);
    }
    pixman_image_unref(line);
    fclose(f);
}

/* The header sits below the hand-off rip. The slide is measured from the unslid base of a 64-bit XNU, from memory; symbolising a rip by it checks that. */
static void hvdbt_find_kernel(CPUState *cs)
{
    const uint64_t unslid_base = 0xffffff8000200000ULL;
    CPUX86State *env = cpu_env(cs);
    uint32_t head[8];

    for (uint64_t at = env->eip & ~0xfffULL; at > env->eip - (64ULL << 20); at -= 4096) {
        if (!cpu_memory_rw_debug(cs, at, head, sizeof head, false) && head[0] == 0xfeedfacf && head[3] == 2 /* MH_EXECUTE */) {
            g_autofree char *line = g_strdup_printf("kernel header at 0x%" PRIx64 ", slide 0x%" PRIx64, at, at - unslid_base);
            hv.kernel_base = at;
            hv.kernel_slide = at - unslid_base;
            engine_tally_note(hv.engine, line);
            return;
        }
    }
    engine_tally_note(hv.engine, "no kernel Mach-O header below the hand-off rip");
}

/*
 * Rebuilds the running kernel's Mach-O from guest memory while its symbols are still loaded: at hand-off, __LINKEDIT hasn't been dropped. nm and otool
 * work on it directly, and a guest rip minus the slide printed here is an unslid address. For reading stalls only: it stays on this machine.
 */
static void hvdbt_dump_kernel(CPUState *cs, const char *path)
{
    uint64_t base = hv.kernel_base;
    uint32_t head[8];

    if (!base || cpu_memory_rw_debug(cs, base, head, sizeof head, false)) {
        fprintf(stderr, "hvdbt: no kernel Mach-O header to dump\n");
        return;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    g_autofree uint8_t *commands = g_malloc(head[5]);
    if (cpu_memory_rw_debug(cs, base + 32, commands, head[5], false)) {
        fclose(f);
        return;
    }
    /* Whether the header's addresses are slid yet depends on who has run. __TEXT starts at the header, whichever it is. */
    uint64_t delta = 0;
    for (uint32_t i = 0, at = 0; i < head[4] && at + 72 <= head[5]; i++, at += ldl_le_p(commands + at + 4)) {
        if (ldl_le_p(commands + at) == 0x19 && !strncmp((char *)commands + at + 8, "__TEXT", 16)) {
            delta = base - ldq_le_p(commands + at + 24);
        }
    }
    fprintf(stderr, "hvdbt: load addresses in the header + 0x%" PRIx64 " are where the segments sit\n", delta);
    uint8_t page[4096];
    uint32_t offset = 0;
    for (uint32_t i = 0; i < head[4] && offset + 8 <= head[5]; i++) {
        uint32_t cmd = ldl_le_p(commands + offset), size = ldl_le_p(commands + offset + 4);
        if (cmd == 0x19 /* LC_SEGMENT_64 */ && size >= 72) {
            if (!strncmp((char *)commands + offset + 8, "__DATA", 16)) {
                hv.kernel_data = ldq_le_p(commands + offset + 24) + delta;
                hv.kernel_data_bytes = ldq_le_p(commands + offset + 32);
            }
            uint64_t vmaddr = ldq_le_p(commands + offset + 24), fileoff = ldq_le_p(commands + offset + 40), filesize = ldq_le_p(commands + offset + 48);
            fprintf(stderr, "hvdbt: segment %.16s at 0x%" PRIx64 " file 0x%" PRIx64 " size 0x%" PRIx64 "\n", (char *)commands + offset + 8, vmaddr, fileoff, filesize);
            for (uint64_t done = 0; done < filesize; done += sizeof page) {
                uint64_t n = MIN(filesize - done, sizeof page);
                if (!cpu_memory_rw_debug(cs, vmaddr + delta + done, page, n, false)) {
                    fseeko(f, fileoff + done, SEEK_SET);
                    fwrite(page, 1, n, f);
                }
            }
        }
        offset += size;
    }
    fclose(f);
    fprintf(stderr, "hvdbt: kernel written to %s\n", path);
    fflush(stderr);
}

/* A separate thread, so none of this costs the vCPU anything until something is wrong. */
static void *hvdbt_watchdog(void *arg)
{
    CPUState *cs = arg;
    HvCpu *cpu = HV(cs);
    CPUX86State *env = cpu_env(cs);
    uint64_t last = ~0ULL;
    int idle = 0, reports = 0, seconds = 0;
    const char *screen = getenv("LC_HVDBT_SCREEN");

    while (!qatomic_read(&hv.dead)) {
        g_usleep(G_USEC_PER_SEC);
        if (screen && !cs->cpu_index && ++seconds % 30 == 0) {
            aio_bh_schedule_oneshot(qemu_get_aio_context(), hvdbt_screen_bh, (void *)screen);
        }
        if (!runstate_is_running()) {
            idle = 0;
            continue;
        }
        /*
         * shortcut: a store by a vCPU on the engine doesn't reach TBs a vCPU staying on TCG already has, so such a stay could run stale code past another's store. Flushing once a
         * second bounds that, and costs an exclusive stop a second, only during a stay. Exact would be TCG hearing of engine stores; if stale code ever shows, start there.
         */
        if (cpu->refused[0] && current_machine->smp.max_cpus > 1) {
            queue_tb_flush(cs);
        }
        if (!qatomic_read(&cpu->in_engine)) { /* racy reads, and only for a human: does TCG's rip move? */
            engine_tally_sample(cpu->engine, env->eip, env->hflags & HF_CPL_MASK, env->cr[3]);
        }
        uint64_t now = engine_progress(cpu->engine);
        if (now != last) {
            last = now;
            idle = 0;
            continue;
        }
        idle++;
        if (idle == 5 && reports < 4) {
            reports++;
            qatomic_set(&cpu->dump_done, false);
            async_run_on_cpu(cs, hvdbt_stall_report, RUN_ON_CPU_NULL);
        } else if (idle == 8 && reports && !qatomic_read(&cpu->dump_done)) {
            /* No stop in 3 s: no x86 boundary reached, or stuck in a host call or on the BQL. All that can be seen from here. */
            char text[2048];
            engine_debug_text(cpu->engine, text, sizeof text);
            fprintf(stderr, "\nhvdbt: vCPU %d: no progress, and it did not stop for a report in 3 s. in_engine %d\n%s", cs->cpu_index, qatomic_read(&cpu->in_engine), text);
            fflush(stderr);
        }
    }
    return NULL;
}

static const TcgHandoffOps hvdbt_ops = {
    .want_engine = hvdbt_want_engine,
    .select_cflags = hvdbt_select_cflags,
    .completed_tb = hvdbt_completed_tb,
    .run = hvdbt_run,
    .kick = hvdbt_kick,
    .note_write = hvdbt_note_write,
};

static void __attribute__((constructor)) hvdbt_init(void)
{
    const char *on = getenv("LC_HVDBT");

    if (on && !strcmp(on, "1")) {
        tcg_handoff = &hvdbt_ops;
    }
}
