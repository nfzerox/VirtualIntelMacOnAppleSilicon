#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ENGINE_ARM_BLOCK_NOP UINT32_C(0xd503201f)
#define ENGINE_ARM_UNKNOWN_A64 UINT64_C(0x02000000)
#define ENGINE_CODE_FETCH_RETRY_LIMIT 8u

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

/* A synchronous Unknown exception cannot be caused by this defined NOP.
 * Retry only at a published block entry, not at an arbitrary translated
 * instruction, and never skip an instruction or rerun x86 code. */
static inline bool engine_code_fetch_can_retry(uint64_t esr, uint64_t word,
                                               bool block_entry, uint64_t pc,
                                               uint64_t previous_pc, unsigned retries) {
    return esr == ENGINE_ARM_UNKNOWN_A64 && block_entry && pc != previous_pc &&
           retries < ENGINE_CODE_FETCH_RETRY_LIMIT &&
           word == ((UINT64_C(1) << 32) | ENGINE_ARM_BLOCK_NOP);
}
