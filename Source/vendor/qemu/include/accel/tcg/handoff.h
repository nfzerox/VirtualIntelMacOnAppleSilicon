/*
 * Lets another x86 engine take a TCG vCPU's instructions, and give single ones
 * back. See target/i386/tcg/system/hvdbt.c. NULL unless that is built and asked
 * for. Guarded fallback quanta also visit the block-selection/completion hooks.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef ACCEL_TCG_HANDOFF_H
#define ACCEL_TCG_HANDOFF_H

#include "exec/cpu-common.h"
#include "exec/vaddr.h"
#include "hw/core/cpu.h"

typedef struct TcgHandoffOps {
    /* A normal TB is missing for this state: should it run elsewhere instead? */
    bool (*want_engine)(CPUState *cpu, vaddr pc, uint32_t flags);
    /* Preserve exact requested steps; otherwise select a guarded TCG block
     * or request the normal EXCP_HANDOFF path before looking up a TB. */
    uint32_t (*select_cflags)(CPUState *cpu, vaddr pc, uint32_t flags,
                              uint32_t cflags, bool exact, bool *handoff);
    /* A guarded, unchained TB completed (also in serial atomic execution). */
    void (*completed_tb)(CPUState *cpu, unsigned instructions);
    /* cpu_exec returned EXCP_HANDOFF. On the vCPU thread with the BQL held. */
    void (*run)(CPUState *cpu);
    void (*kick)(CPUState *cpu);
    /* RAM was written from outside the engine: by TCG's softmmu, or by DMA. */
    void (*note_write)(uint64_t ram_addr, uint64_t len, bool by_tcg);
} TcgHandoffOps;

extern const TcgHandoffOps *tcg_handoff;

#endif
