/*
 * The per-CPU TranslationBlock jump cache.
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ACCEL_TCG_TB_JMP_CACHE_H
#define ACCEL_TCG_TB_JMP_CACHE_H

#include "qemu/rcu.h"
#include "exec/cpu-common.h"

#define TB_JMP_CACHE_BITS 12
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)

bool pvg_alternate_lookup_enabled(void);
void pvg_alternate_lookup_set(bool value);
bool pvg_lookup_diagnostics_enabled(void);
void pvg_lookup_diagnostics_set(bool value);

/*
 * Invalidated in parallel; all accesses to 'tb' must be atomic.
 * A valid entry is read/written by a single CPU, therefore there is
 * no need for qatomic_rcu_read() and pc is always consistent with a
 * non-NULL value of 'tb'.  Strictly speaking pc is only needed for
 * CF_PCREL, but it's used always for simplicity.
 */
typedef struct CPUJumpCache {
    struct rcu_head rcu;
    /* Atomic: whole-cache invalidation can come from another vCPU. */
    uint64_t generation;
    /* Owning-vCPU context key; remote invalidation never changes it. */
    uint64_t context_key;
    bool lazy_flush;
    struct {
        TranslationBlock *tb;
        vaddr pc;
        /* Like pc, only the owning CPU reads/writes this field. */
        uint64_t generation;
        uint64_t context_key;
    } array[TB_JMP_CACHE_SIZE], alternate[TB_JMP_CACHE_SIZE];
    /* Local opt-in C lookup diagnostics. Generated fast hits are NOT counted.
     * One writer (owning vCPU); monitor takes approximate atomic snapshots. */
    uint64_t lookup_reasons[8];
} CPUJumpCache;

#endif /* ACCEL_TCG_TB_JMP_CACHE_H */
