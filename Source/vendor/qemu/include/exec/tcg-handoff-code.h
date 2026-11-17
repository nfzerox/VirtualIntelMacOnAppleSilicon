/*
 * Exact instruction bytes for locally guarded hybrid fallback TBs.
 * Metadata shares the TB allocation lifetime; ordinary TBs allocate none.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef EXEC_TCG_HANDOFF_CODE_H
#define EXEC_TCG_HANDOFF_CODE_H

#include "exec/translation-block.h"

/* x86 fallback TBs cover at most 4096 bytes, including a crossing insn. */
#define TCG_HANDOFF_CODE_LIMIT 4096

typedef struct TcgHandoffCode {
    uint16_t length;
    bool consistent;
    uint8_t bytes[TCG_HANDOFF_CODE_LIMIT];
    uint8_t fetched[TCG_HANDOFF_CODE_LIMIT / 8];
} TcgHandoffCode;

static inline TcgHandoffCode *tcg_handoff_code(TranslationBlock *tb)
{
    return (void *)(tb + 1);
}

#endif
