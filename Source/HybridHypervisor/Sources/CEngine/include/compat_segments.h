#pragma once
#include <stdbool.h>
#include <stdint.h>

// Two eligibility masks in the existing seg_bad word: user code in bits0–5,
// supervisor code in bits8–13. Keep both when a 64-bit trampoline loads the
// next mode's selectors: a kernel data selector must never qualify ring3.
// flags is the descriptor's high dword (as cached by QEMU), not its base.
static inline uint32_t engine_compat_segment_bad(unsigned segment,uint16_t selector,
    uint32_t flags,uint64_t base,uint32_t limit) {
    uint32_t bit=UINT32_C(1)<<segment;
    bool null=(selector&~3u)==0;
    bool valid=null?segment>=4:
        (flags&(1u<<15))&&(flags&(1u<<12))&&!(flags&(1u<<11));
    unsigned dpl=(flags>>13)&3;
    if(valid&&!null) {
        valid=dpl>=(selector&3);
        if(segment<4)valid=valid&&!base&&limit==UINT32_MAX&&!(flags&(1u<<10));
    }
    bool kernel=valid;
    bool user=valid&&(null||dpl==3);
    return (user?0:bit)|(kernel?0:bit<<8);
}

static inline uint32_t engine_compat_segments_bad(uint32_t policies,unsigned cpl) {
    const uint32_t data=(1u<<0)|(1u<<3)|(1u<<4)|(1u<<5);
    return (cpl==0?policies>>8:policies)&data;
}
