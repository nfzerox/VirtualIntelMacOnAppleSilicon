/* Native bounds/word fixture, not a guest execution or M3 qualification. */
#include <assert.h>
#include <stdio.h>
#include "monitor-code-fault.h"
int main(void) {
    const uint32_t first[4] = {0, 0xd503201f, 0xab0f0000, 0xd65f03c0};
    const uint32_t second[4] = {0x02000000, 0, 0, 0};
    uint64_t a = (uintptr_t)first, b = (uintptr_t)second, valid = UINT64_C(1) << 32;
    assert(engine_capture_code_fault(a,a,b,sizeof first) == valid);
    assert(engine_capture_code_fault(a+4,a,b,sizeof first) == (valid|first[1]));
    assert(engine_capture_code_fault(a+12,a,b,sizeof first) == (valid|first[3]));
    assert(engine_capture_code_fault(b,a,b,sizeof second) == (valid|second[0]));
    assert(!engine_capture_code_fault(a+1,a,b,sizeof first));
    assert(!engine_capture_code_fault(a-4,a,b,4));
    assert(!engine_capture_code_fault(a+4,a,b,4));
    assert(!engine_capture_code_fault(0,a,b,sizeof first));
    assert(!engine_capture_code_fault(UINT64_MAX,a,b,sizeof first));
    assert(!engine_capture_code_fault(a,a,b,0));
    uint64_t nop = valid | UINT32_C(0xd503201f), branch = valid | UINT32_C(0x17e82c40);
    // Any owned translated word, not only an entry NOP: M3 stale fetches also hit B and MOVN.
    assert(engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,nop,a,0));
    assert(engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,branch,a+4,a));
    assert(engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,valid,a,b)); // a stale UDF read back as UDF still gets its one retry
    assert(!engine_code_fetch_can_retry(0,nop,a,0));
    assert(!engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,0,a,0)); // outside the code cache
    assert(!engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,nop,a,a)); // the retry faulted again: a real bad word
    puts("PASS bounded fault words and one re-fetch per translated ARM PC");
}
