// Layout and calling conventions shared by the translator, the EL1 monitor (Monitor/) and the Swift control plane.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "note-rescan-profile.h"

// Register pinning for translated code (EL0). x86 rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8-r15 are x0-x15, xmm0-15 are v0-15.
#define R_S0 16 // scratch
#define R_S1 17 // scratch
#define R_CTX 18 // EngineCtx, in private memory
#define R_HASH 19 // indirect-branch table of the current view
#define R_AD 20 // scratch, usually an address
#define R_PF 21 // lazy PF: low byte of the last flag-setting result
#define R_S2 22 // scratch
#define R_S3 23 // scratch
#define R_S4 24 // scratch
#define R_AF 25 // lazy AF: bit 4 of (R_AF ^ R_PF)
#define R_RS 26 // shadow return stack pointer of the current view
#define R_GS 27 // x86 GS base
#define R_FS 28 // x86 FS base
#define R_S5 29 // scratch
#define R_LR 30 // scratch, bl/ret pairing

// Private memory. Mapped from PRIV_IPA and in every shadow root at PRIV_VA (non-canonical for x86: bit 47 set, top bits clear).
#define PRIV_IPA 0xF00000000ULL // 60 GB; x86 RAM and device GPAs stay below it
#define PRIV_VA 0xF00000000000ULL
#define PRIV_SIZE (2ULL << 30) // reserved, touched lazily

#define OFF_MON_TEXT 0ULL // EL1 rx: vectors, monitor text and rodata
#define OFF_MON_DATA (2ULL << 20) // EL1 rw: monitor data, stack
#define OFF_STUBS (4ULL << 20) // EL0 execute-only: lookup and helper stubs, within bl reach of the code cache
#define OFF_CC (6ULL << 20) // EL0 execute-only, EL1 rw: translated code
#define CC_SIZE (118ULL << 20)
#define OFF_CTX (124ULL << 20) // EL0 rw: one EngineCtx per vCPU, CTX_SLOT apart. x18 holds this vCPU's
#define MAX_VCPUS 8
#define CTX_SLOT (64ULL << 10)
#define OFF_MON_CPU (OFF_MON_DATA + (3ULL << 19)) // EL1 rw: per vCPU, CTX_SLOT apart too, the monitor's own state and stack
#define OFF_CTX_END (126ULL << 20)
#define OFF_HASH (1024ULL << 20) // EL0 ro, per view: indirect-branch table
#define HASH_ENTRIES (1ULL << 16)
#define HASH_BYTES (HASH_ENTRIES * 16)
// The gigabyte that XNU nests its shared libraries into from one page directory, whatever process maps it. Elsewhere, or under another OS, nothing is shared.
#define SHARED_USER_BASE 0x7FFF00000000ULL // Mavericks' shared region starts at 0x7FFF80000000, and Mojave's lower
#define SHARED_USER_END 0x7FFFC0000000ULL
#define SHARED_USER_DIRS 512
// 32-bit shared regions reuse 2MB page tables, not necessarily entire 1GB
// directories. Keep the virtual slot in the identity too: a keeper must map
// this same table at this same address. Physical RAM is bounded to 32GB.
#define SHARED_COMPAT_TAG (1ULL << 63)
#define SHARED_COMPAT_KERNEL (1ULL << 62)
static inline bool engine_shared_compat_address(uint64_t rip) {
    return rip < (1ULL << 32) && (rip & ((1ULL << 21) - 1)) < (1ULL << 21) - 4096;
}
static inline uint64_t engine_shared_compat_domain(uint64_t rip, uint64_t table) {
    return SHARED_COMPAT_TAG | ((rip >> 21) << 35) | table;
}
#define MAX_VIEWS 256 // two or three per x86 address space, and one that goes takes its blocks with it: a desktop has a hundred processes
#define OFF_RSTACKS (OFF_HASH + MAX_VIEWS * HASH_BYTES) // EL0 rw, per vCPU and view: 16 KB guard (unmapped), then a 16 KB shadow return stack
#define OFF_BLOCKS (196ULL << 20) // EL1 ro: block table, pc maps
#define BLOCKS_MAX (1ULL << 19) // the table has 16 MB
#define OFF_PCMAP (212ULL << 20)
#define PCMAP_BYTES (44ULL << 20)
#define OFF_PT (256ULL << 20) // EL1 rw: static hole tables, then the shadow pool
#define PT_STATIC_BYTES (1ULL << 20)
#define SHADOW_POOL_BYTES (255ULL << 20) // after the static tables, up to OFF_SHADOW_META
#define OFF_SHADOW_META (512ULL << 20) // EL1 rw: shadow page descriptors, hash, rmap
#define OFF_PAGEINFO (768ULL << 20) // EL1 rw, one byte per guest page frame (4 GB of RAM per MB)
// The code cache's second part, for blocks of one view: as far as this is from the first, so with stubs of its own in reach, and tables of its own.
#define OFF_STUBS2 (816ULL << 20)
#define OFF_CC2 (818ULL << 20)
#define OFF_BLOCKS2 (936ULL << 20)
#define OFF_PCMAP2 (952ULL << 20)
#define CC_PART2_WORD ((OFF_CC2 - OFF_CC) / 4)

// RAM alias, EL1 only: x86 GPA + RAM_ALIAS_VA reaches guest RAM for the monitor's walker.
#define RAM_ALIAS_VA 0xF10000000000ULL

// Stubs at fixed offsets from OFF_STUBS, as laid out in Monitor/vectors.s.
#define STUB_OFF_REP_MOVS 0x200ULL
#define STUB_OFF_REP_MOVS_HEAD 0x240ULL
#define STUB_OFF_REP_STOS 0x280ULL
#define STUB_OFF_REP_STOS_HEAD 0x2C0ULL
#define STUB_OFF_REP_END 0x300ULL
// Experimental page service entries; the original bulk entries above remain the control.
#define STUB_OFF_REP_PAGE_MOVS 0x300ULL
#define STUB_OFF_REP_PAGE_MOVS_HEAD 0x340ULL
#define STUB_OFF_REP_PAGE_STOS 0x380ULL
#define STUB_OFF_REP_PAGE_STOS_HEAD 0x3C0ULL
#define STUB_OFF_REP_PAGE_END 0x400ULL

#define RSTACK_BYTES (16ULL << 10)
#define RSTACK_SLOT (32ULL << 10)
static inline uint64_t engine_off_rstacks(uint32_t cpu) { return OFF_RSTACKS + (uint64_t)cpu * MAX_VIEWS * RSTACK_SLOT; }

// Page info bits, one byte per GPA frame, owned by the monitor.
#define PI_CODE 1 // blocks were translated from this frame
#define PI_PT 2 // a shadow page tracks this frame as a guest page table
#define PI_RAM 4 // RAM, as opposed to a device hole
#define PI_DIRECT 8 // device memory mapped straight into the VM: shadows may point at it, and nothing else about RAM holds

// svc immediates from translated code and stubs. Translator requests come first, then x86 events and helpers.
enum {
    SVC_DIRECT = 1, // literal target follows: fall through or jump stub. ELR points at the literal.
    SVC_JCC, // as above, a third word holds the distance back to the b.cond
    SVC_CALL, // as SVC_DIRECT, x30-4 is the bl site
    SVC_LOOKUP_MISS, // x16 target, x30 return
    SVC_DEAD, // this block was invalidated; ELR-4 finds it
    SVC_STOP, // literal: u32 stop reason index
    SVC_SINGLE_DONE, // end of a one-off single-instruction block, literal target follows
    // x86 events
    SVC_SYSCALL = 16, // x16 next rip
    SVC_SYSRET,
    SVC_IRET,
    SVC_INT, // x16 next rip, x17 vector
    SVC_INT3,
    SVC_INTO_UNUSED,
    SVC_UD, // #UD at this instruction, literal rip
    SVC_DE, // #DE
    SVC_GP0, // #GP(0)
    SVC_HLT,
    SVC_CPUID,
    SVC_RDTSC,
    SVC_RDTSCP,
    SVC_RDMSR,
    SVC_WRMSR,
    SVC_CLI,
    SVC_STI,
    SVC_POPF, // x16 value, x17 next rsp
    SVC_MOV_CR_READ, // x16 cr number, result in x16
    SVC_MOV_CR_WRITE, // x16 cr number, x17 value
    SVC_INVLPG, // x16 va
    SVC_INVPCID,
    SVC_WBINVD,
    SVC_LGDT, // x16 base, x17 limit
    SVC_LIDT,
    SVC_SGDT, // result x16 base, x17 limit
    SVC_SIDT,
    SVC_LTR, // x16 selector
    SVC_STR, // result x16
    SVC_LLDT,
    SVC_SLDT,
    SVC_MOV_SEG_WRITE, // x16 segment index, x17 selector
    SVC_MOV_SEG_READ, // x16 segment index, result x16
    SVC_IN, // x16 port, x17 size, result x16
    SVC_OUT, // x16 port, x17 size, x22 value
    SVC_SWAPGS,
    SVC_LDMXCSR, // x16 value
    SVC_STMXCSR, // result x16
    SVC_FXSAVE, // x16 address of 512-byte buffer, already probed writable
    SVC_FXRSTOR,
    SVC_MFENCE_UNUSED,
    SVC_CLTS,
    SVC_SYSENTER,
    SVC_SYSEXIT, // x16 1 with REX.W
    SVC_FAR, // x16 offset, x17 selector, x22 kind (0 retf, 1 call, 2 jmp) | operand size << 8 | retf's immediate << 16
    SVC_X87, // literal: u32 op, u64 rip. x20 the effective address, x16 and x17 a value loaded. Stores: x16 and x17 the value, x23 the state word to commit after it
    // Helpers: operands in x16, x17, x22, x23. Results as documented in monitor.c. Flags come back in x22 (nzcv), x23 (pf), x24 (aux).
    SVC_H_SHIFT = 64, // narrow and rotate-through-carry shifts: x16 value, x17 count, x22 kind|size<<8. x16 result
    SVC_H_SHD, // shld/shrd: x16 dst, x17 src, x22 count, x23 size|isright<<8. x16 result
    SVC_H_MUL, // mul/imul narrow: x16 a, x17 b, x22 size|signed<<8. x16 low, x17 high
    SVC_H_DIV, // x16 low, x17 high, x22 divisor, x23 size|signed<<8. x16 quotient, x17 remainder
    SVC_H_BITSCAN_UNUSED,
    SVC_H_PCMPISTRI, // x16, x17 the first operand, x22, x23 the second, x24 imm8. x16 the index, x22 to x24 flags as data
    SVC_H_RMWFLAGS_UNUSED,
    SVC_REP_PAGE = 71, // internal only: exact page-REP stub head; x17=32/element bytes
};

// Opt-in metadata for SVC_H_SHIFT only. The semantic kind is bits 0..2 and
// operand bytes are bits 8..15; profiling must never become part of the width.
#define SHIFT_HELPER_PROFILE_MEM (1ULL << 16)
#define SHIFT_HELPER_PROFILE_CL (1ULL << 17)
#define SHIFT_HELPER_PROFILE_MARK (1ULL << 18)
#define SHIFT_HELPER_PROFILE_BUCKETS 256

// Hypervisor calls from monitor to host: hvc #code, arguments and results in EngineCtx.req.
enum {
    HC_NONE = 0,
    HC_TRANSLATE, // req.a[0] view, a[1] rip, a[2] site VA or 0, a[3] link kind (svc number) or 0, a[4] 1 for single instruction
                  // result r[0] arm, r[1] nonzero if fetch fault at VA r[1]-1. Host reports frames used in r[2], r[3] (GPA|1 or 0)
    HC_SMC, // a[0] gpa: invalidate frame
    HC_VIEW_INVALIDATE_VA, // a[0] view, a[1] va: invalidate blocks at this page in the view
    HC_STOP, // a[0] reason code, a[1] x86 rip, a[2..4] detail
    HC_SYSCALL, // user-mode shim (no IDT model): regs in ctx
    HC_MMIO, // a[0] gpa, a[1] size, a[2] is write, a[3] value. r[0] read value
    HC_PORT, // a[0] port, a[1] size, a[2] is write, a[3] value. r[0] read value
    HC_RDMSR, // a[0] msr. r[0] value, r[1] 1 if unhandled (#GP)
    HC_WRMSR, // a[0] msr, a[1] value. r[1] 1 if unhandled
    HC_CPUID, // a[0] leaf, a[1] subleaf. r[0..3] eax,ebx,ecx,edx
    HC_HLT, // guest halted with interrupts possibly enabled. Host returns when an interrupt may be pending.
    HC_IRQ, // refresh the host IRQ line; a[0] allows acknowledging a Hybrid vector. r[0] vector or -1
    HC_APIC_EOI, // a[0] vector: EOI of a level-triggered vector, for the host's IO-APIC
    HC_APIC_ICR, // a[0] icr: an IPI the monitor cannot finish itself
    HC_BOUNDARY, // a[0] x86 rip: the vCPU stopped at an instruction boundary on request. r[0] rip to continue at
    HC_VIEW_DROP, // a[0] view, being reused for another address space
    HC_DEBUG, // a[0] a[1] printable
    HC_NOTE, // hybrid, for the tally. a[0] 1 process syscall (a[1] next rip), 2 wait4 returning (a[1] rip, a[2] status pointer), 3 user fault (a[1] rip, a[2] vector, a[3] error)
    HC_FALLBACK, // layout as HC_STOP. The host runs this x86 instruction elsewhere and sets the state; r[0] is the rip to continue at
    HC_YIELD, // a lock has been held a long time: let the host run something else
    HC_PARK, // out of the code cache for a moment, in case another vCPU is resetting it
    HC_VIEW_FORGET, // a[0] view: empty its lookup table, as a slot above a shared directory changed
    HC_CALL_TAIL, // a[0] a cross-key call/jump site, a[1] its x86 target: use a view lookup without repeatedly entering the monitor
    HC_CODE_FETCH_RETRY, // a[0] ARM pc, a[1] x86 block entry or 0, a[2] this vCPU's retry count (only the first few are reported)
};

// Stop reasons for HC_STOP.
enum {
    STOP_GUEST_HALT = 0,
    STOP_UNTRANSLATED, // a[2]: index into the translator's reason list
    STOP_MONITOR_FAULT, // exception taken while in the monitor. a[2] esr, a[3] far, a[4] elr
    STOP_UNSUPPORTED, // a[2]: pointer-free reason code, see monitor.c
    STOP_TRIPLE_FAULT,
    STOP_GUEST_FAULT_NO_IDT, // user-mode shim: x86 exception with no IDT model. a[2] vector, a[3] error code, a[4] cr2
    STOP_SHADOW_FULL,
    STOP_FALLBACK, // hybrid only, a[2]: why, FB_*
};

// Why a hybrid engine hands an instruction to the host although it could have decoded it. The state these belong to lives in QEMU's CPU.
enum {
    FB_UD, // the decoder knows no such instruction; only the host's CPU model can say whether it exists
    FB_HLT,
    FB_FX, // fxsave or fxrstor on a device or tracked frame, with an unmasked MXCSR, or under CR4 and EFER settings the monitor leaves to QEMU
    FB_CR8, // the TPR is in QEMU's APIC
    FB_RDMSR, // a[3] msr
    FB_WRMSR, // a[3] msr
    FB_FETCH_NOT_RAM, // a[3] gpa
    FB_CLTS, // only with LC_HVDBT_CLTS_TCG=1, to compare
    FB_VMPORT, // the VMware backdoor port: its device model reads and writes the CPU's registers, which only TCG's CPU can show it
    FB_X87, // an x87 operation with an exception unmasked or pending, a[3] its opcode: only TCG raises #MF
};

// What the kernel model just did, for a person reading the tally: delivery, iret, syscall, sysret, CR3 writes, oldest overwritten. Hybrid only.
typedef struct {
    uint64_t rip, a, cr3; // a: error code, iret target, or the CR3 value written
    uint32_t kind, vector; // kind: 'E' event delivered, 'I' iret (vector: target CS), 'C' CR3 write, 'S' syscall, 'R' sysret
} EngineEvent;
#define EVENT_RING 128

// Why translated blocks die, for the tally.
enum { KILL_STORE, KILL_NOTE, KILL_PT_LEAF, KILL_PT_TABLE, KILL_PT_FLOOD, KILL_RECLAIM, KILL_VIEW_DROP, KILL_DEVICE_MOVED, KILL_KINDS };

typedef struct {
    uint64_t x[31]; // +0: x0-x30, saved on every monitor entry
    uint64_t elr; // +248
    uint64_t spsr; // +256
    uint64_t esr; // +264
    uint64_t far; // +272
    // Read and written by translated code. Offsets are used in stubs; keep them fixed.
    int64_t df; // +280: +1 or -1
    uint64_t kernel_gs_base; // +288
    uint64_t rs_top; // +296: reset value for R_RS in the current view
    uint64_t rflags_sys; // +304: every RFLAGS bit except CF PF AF ZF SF OF DF. Bit 1 always set
    uint64_t rsvd_el0[3];

    // Host request block.
    uint64_t req_a[6]; // +344
    uint64_t req_r[4];

    // Monitor-owned x86 system state; the host may read it stopped.
    uint64_t cr0, cr2, cr3, cr4, efer;
    uint64_t star, lstar, cstar, sfmask;
    uint64_t gdtr_base, idtr_base;
    uint32_t gdtr_limit, idtr_limit;
    uint16_t tr, ldtr, seg_sel[6]; // es cs ss ds fs gs
    uint64_t tss_base;
    uint32_t tss_limit;
    uint32_t cpl;
    uint32_t mxcsr; // control bits; status bits live in FPSR
    uint32_t view; // current view index
    uint32_t model_kernel; // 0 user-mode shim: syscall and faults go to the host. 1 full x86 kernel model.
    uint32_t vcpu_count; // in the first context: how many this VM runs. Set before any of them starts
    uint64_t irq_shadow_rip; // interrupts wait until the instruction at this rip has run: the sti shadow
    uint32_t irq_pending; // host or in-VM APIC has an interrupt ready
    uint32_t halted;
    uint32_t stepping; // walking to an x86 boundary with the software step
    uint32_t pool_limit; // shadow pool pages, 0 for all of them; tests shrink it
    uint32_t tr_flushed; // the translator reset the code cache; every return stack is stale
    uint64_t shared_dirs[SHARED_USER_DIRS]; // witnessed 64-bit directories or tagged 32-bit table/virtual-slot identities
    uint32_t shared_dir_count;
    uint32_t share_user; // 0: LC_HVDBT_NO_SHARE, or not hybrid
    uint32_t direct_flush; // device memory left the VM: shadows may still point at it, drop them before running on
    // x87 and MMX state, by physical register. mm holds the significands, and translated code loads and stores them.
    uint64_t mm[8];
    uint16_t fp_exp[8]; // sign and exponent
    // One word, so that an x87 store commits with one ARM store once its memory operand is written. MMX writes the first two bytes alone.
    uint8_t fp_top;
    uint8_t fp_tags; // bit set: register in use, as FXSAVE's abridged tag
    uint16_t fsw; // the TOP bits stay zero, TOP is fp_top
    uint16_t fcw, fop, fcs, fds;
    uint64_t fip, fdp;
    uint32_t compat; // CS.L clear: 32-bit user code. In this state the low eight GPRs always have their top halves clear, and the view is a separate one
    uint32_t seg_bad; // user eligibility in bits0–5, supervisor in8–13; see compat_segments.h
    uint64_t ldt_base;
    uint32_t ldt_limit, pad_ldt;
    uint64_t sysenter_cs, sysenter_esp, sysenter_eip;
    uint64_t ram_size;
    uint64_t kernel_view_tag;

    // Virtual local APIC. The host sets bits in apic_host_irr atomically from any thread and raises the IRQ line.
    uint64_t apic_base, hybrid_apic_page;
    uint32_t apic_enabled, apic_id, apic_tpr, apic_ldr, apic_dfr, apic_svr, apic_icr_lo, apic_icr_hi;
    uint32_t apic_lvt_timer, apic_lvt[5], apic_tmict, apic_tdcr, apic_timer_shift, stop_requested;
    uint32_t apic_irr[8], apic_isr[8], apic_tmr[8], apic_host_irr[8], apic_host_level[8];
    uint64_t apic_deadline_cnt, apic_timer_start_cnt, apic_tsc_deadline;
    uint64_t stat_apic_timer, stat_ipi_self;

    // Blocks, published by the host.
    uint64_t block_count, block_count_private; // the code cache's two parts: see "code cache" in translate.c
    uint64_t tsc_offset;

    // Statistics, read by the host.
    uint64_t stat_svc[128];
    uint64_t stat_fills, stat_dirty, stat_pf_delivered, stat_pt_writes, stat_pt_writes_zap, stat_pt_step, stat_smc,
        stat_mon_wait, stat_mon_hold[6], stat_irq, stat_steps, stat_hlt_polled, stat_cr3, stat_iret, stat_syscall, stat_mmio, stat_mmio_vm, stat_view_evictions, stat_pt_unshadow, stat_pool_reclaim;

    // Hybrid mode: QEMU's TCG owns everything the engine does not, and takes single instructions back through HC_FALLBACK.
    uint32_t hybrid;
    uint32_t irq_line; // set from any thread: QEMU's APIC may have an interrupt. Level, not edge: stays set while IF is clear.
    uint32_t resume_mxcsr;
    uint32_t clts_on_host;
    uint64_t tsc_ns_offset, tsc_num, tsc_den; // TSC = ns + ns * num / den + tsc_offset; see tsc_counter_offset below.
    // Frames written behind the monitor's back (DMA, TCG), holding code or page tables. Many producers, consumed in the monitor.
    uint32_t note_head, note_tail, note_overflow, pad4;
    uint32_t note_ring[256]; // frame + 1
    uint64_t stat_note_code, stat_note_pt, stat_nm, stat_pt_stale, stat_links_here, stat_stack_deepest;
    uint64_t stat_vector[256], vector_last_rip[256], vector_last_err[256], vector_last_cr3[256]; // every delivery, software int and #GP included
    uint64_t event_next;
    EngineEvent events[EVENT_RING];
    uint64_t user_pf_next;
    EngineEvent user_pf[32]; // user page faults: rip, a is CR2, vector is the error code. What a process that dies of a signal touched last
    // Opt-in observations only, appended to preserve every existing assembly/emitter offset. Per-vCPU, not c0 aggregates.
    uint32_t profile_mmu_observations, pad_mmu_observations;
    uint64_t stat_mmu_counter_hz;
    uint64_t stat_mmu_first_walk_calls, stat_mmu_first_walk_samples, stat_mmu_first_walk_ticks;
    uint64_t stat_mmu_locked_rewalk_calls, stat_mmu_locked_rewalk_samples, stat_mmu_locked_rewalk_ticks;
    // Indices: plain32, plain64, LSE32, LSE64. Compare operand-width values observed before/after; no store is skipped.
    uint64_t stat_mmu_store_observed[4], stat_mmu_store_unchanged[4];
    // Per-vCPU count-only census: cause(threshold/inactive/both)*2 + keeper; X marks reason*4 + keeper*2 + valid.
    uint64_t stat_mmu_flood_decisions[6], stat_mmu_xmark_zaps[KILL_KINDS * 4];
    // Experimental/default off. State is per-vCPU; counters do not imply elapsed time or saved body kills.
    uint32_t keeper_inactive_guard, pad_keeper_inactive_guard;
    uint64_t stat_keeper_inactive_suppressed, stat_keeper_inactive_threshold_fallbacks;
    // Opt-in, per-vCPU helper-entry counts, not retired instructions or time.
    // Index: ((((kind*4 + log2(bytes))*2 + memory)*2 + CL)*2 + masked_zero).
    // masked_zero tests the x86 31/63 mask, before a rotate's further modulo.
    uint32_t profile_shift_helpers, pad_shift_helpers;
    uint64_t stat_shift_helpers_unclassified;
    uint64_t stat_shift_helpers[SHIFT_HELPER_PROFILE_BUCKETS];
    // Experimental/default off. Append only: preserve all earlier emitter/assembly offsets.
    // Exchanges/equal/changed count stores; leaf/xmark skips count nonempty shadow mirrors, not saved bodies or time.
    uint32_t pte_same64, pad_pte_same64;
    uint64_t stat_pte_same64_exchanges, stat_pte_same64_equal, stat_pte_same64_changed;
    uint64_t stat_pte_same64_leaf_skips, stat_pte_same64_xmark_skips;
    uint64_t stat_rep_page_prefixes, stat_rep_page_bytes; // per-vCPU positive commits only; no invocation/decline inference
    // Add the HVF virtual-counter offset BEFORE converting to nanoseconds.
    // Splitting the conversion into two rounded terms differs from QEMU.
    uint64_t tsc_counter_offset;
    // Opt-in helper-entry census, not retired instructions/time. Append only.
    // 0..63: memory esc*8+reg;64..575: register esc*64+(ModRM&63);576:FWAIT.
    uint32_t profile_x87_helpers, pad_x87_helpers;
    uint64_t stat_x87_helpers_unclassified, stat_x87_helpers[577];
    // Enabled only with the existing explicit MMU-observation diagnostic.
    // Append only: preserve earlier assembly/emitter offsets and slot size.
    EngineNoteRescanRing note_rescans;
    // vCPU zero publishes the VM's effective FPCR.NEP before translating.
    uint32_t fpcr_merges_scalar;
    // Code publication generation: the host translator stores a new value in
    // every vCPU's slot after writing code and before publishing a lookup for it.
    // code_synced is the last value this vCPU saw before executing ISB in
    // stub_lookup. A hit runs code another thread wrote, and on Apple M3 (H15)
    // cores only this vCPU's own ISB discards instructions fetched before that write.
    uint64_t code_gen __attribute__((aligned(8)));
    uint64_t code_synced;
} EngineCtx;
_Static_assert(sizeof(EngineCtx) <= CTX_SLOT, "EngineCtx must fit its existing per-vCPU slot");
// Monitor/vectors.s reads these two from x18 with fixed offsets.
#define CTX_CODE_GEN_OFFSET 30680
#define CTX_CODE_SYNCED_OFFSET 30688
_Static_assert(__builtin_offsetof(EngineCtx, code_gen) == CTX_CODE_GEN_OFFSET, "update CTX_CODE_GEN in Monitor/vectors.s");
_Static_assert(__builtin_offsetof(EngineCtx, code_synced) == CTX_CODE_SYNCED_OFFSET, "update CTX_CODE_SYNCED in Monitor/vectors.s");

// Block table, one entry per translated block, ordered by arm_off because the code cache only grows.
typedef struct {
    uint64_t rip;
    uint32_t arm_off; // words from the code cache base
    uint32_t arm_words;
    uint32_t map_off; // index of the first PcMap in the map array
    uint32_t map_count;
    uint32_t key, pad; // a view, or a shared domain: links stay within one
} BlockEntry;

// One x86 instruction boundary inside a block.
typedef struct {
    uint16_t arm_word;
    uint16_t rip_off;
} PcMap;
