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
    uint64_t nop = valid | ENGINE_ARM_BLOCK_NOP;
    assert(engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,nop,true,a,0,0));
    assert(!engine_code_fetch_can_retry(0,nop,true,a,0,0));
    assert(!engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,valid,true,a,0,0));
    assert(!engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,0,true,a,0,0));
    assert(!engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,nop,false,a,0,0));
    assert(!engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,nop,true,a,a,1));
    assert(!engine_code_fetch_can_retry(ENGINE_ARM_UNKNOWN_A64,nop,true,a,b,ENGINE_CODE_FETCH_RETRY_LIMIT));
    puts("PASS bounded fault words and narrow A64 block-entry cache retry policy");
}
