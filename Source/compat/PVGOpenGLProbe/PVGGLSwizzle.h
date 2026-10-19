#pragma once
#include <stdint.h>

// Catalina Metal MTLTextureSwizzleViewSwizzle (502cb..50301): compose four
// byte-sized selectors. 0/1 are literal zero/one, 2..5 select base RGBA.
// Mask the shift count to match x86 behavior even for out-of-range selectors.
static inline uint32_t PVGGLComposeSwizzle(uint32_t base,uint32_t view) {
    uint32_t result=0;
    for(unsigned shift=0;shift<32;shift+=8) {
        uint32_t component=(view>>shift)&255;
        if(component>=2)component=(base>>(((component-2)*8)&31))&255;
        result|=component<<shift;
    }
    return result;
}
