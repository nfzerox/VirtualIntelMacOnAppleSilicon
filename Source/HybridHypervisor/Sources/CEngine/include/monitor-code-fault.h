#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ENGINE_ARM_UNKNOWN_A64 UINT64_C(0x02000000)
#define ENGINE_CODE_FETCH_RETRY_LOGGED 4u

/* Fault path only. Capture before releasing the monitor lock or asking the
 * host to stop: another vCPU may subsequently retire/reuse this code. Bit32
 * distinguishes an actual UDF #0 word from an address we could not inspect.
 * Never dereference an arbitrary guest address or a misaligned instruction. */
static inline uint64_t engine_capture_code_fault(uint64_t pc, uint64_t first,
                                                uint64_t second, uint64_t bytes) {
    if ((pc & 3) || bytes < sizeof(uint32_t) ||
        (pc - first > bytes - sizeof(uint32_t) &&
         pc - second > bytes - sizeof(uint32_t)))
        return 0;
    return (UINT64_C(1) << 32) | *(volatile const uint32_t *)(uintptr_t)pc;
}

/* A vCPU can enter code another vCPU (or the host translator) has just
 * written without a context synchronization event of its own: a linked branch
 * leads straight there. Apple M3 (H15) cores then execute the bytes they
 * fetched before the write, until that vCPU itself executes ISB; ERET alone
 * is not enough there (measured: about six returns per fault without one).
 * Hash hits synchronize in stub_lookup and code-cache resets clear reused
 * space, so what remains is a prefetched zero (UDF) past the old cursor. The
 * monitor returns through an ISB, so the retry of the same ARM PC fetches the
 * written word, and a stale fetch cannot fault twice at one PC in a row. The
 * retry runs no x86 code twice and skips nothing: the faulting instruction
 * never executed. A second consecutive fault at the same PC is a real bad
 * word, and stays fatal with that word captured. `word` is the capture from
 * engine_capture_code_fault: only owned translated code is retried. */
static inline bool engine_code_fetch_can_retry(uint64_t esr, uint64_t word,
                                               uint64_t pc, uint64_t previous_pc) {
    return esr == ENGINE_ARM_UNKNOWN_A64 && (word >> 32) && pc != previous_pc;
}
