#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef KERNEL
#include <libkern/libkern.h>
#else
#include <string.h>
#endif

namespace PVGDisplayWorkLoop {
struct Layout {
    size_t call;
    uint32_t workLoop, online, offline;
};
// Recognize the donor's init ABI and the matching cursor work-loop accessor.
// The replacement factory thunk uses its preserved R12 framebuffer parameter;
// no private object field is read or written by our replacement factory.
inline bool findFactoryCall(const uint8_t *init, size_t initSize,
    const uint8_t *cursor, size_t cursorSize, uintptr_t initAddress,
    uintptr_t factoryAddress, uintptr_t interruptFactoryAddress, Layout &layout) {
    const uint8_t prefix[] = {0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x45,0x89,0xc7,
        0x49,0x89,0xcc,0x49,0x89,0xd5,0x49,0x89,0xf6,0x48,0x89,0xfb};
    if (!init || !cursor || initSize<sizeof(prefix) || cursorSize<15 ||
        memcmp(init,prefix,sizeof(prefix)) ||
        memcmp(cursor,"\x55\x48\x89\xe5\x48\x83\xec\x30\x4c\x8b\x87",11)) return false;
    uint32_t cursorOffset;
    memcpy(&cursorOffset,cursor+11,4);
    if (cursorOffset<0x100 || cursorOffset>0x10000 || cursorOffset%8) return false;
    unsigned matches=0, interrupts=0; size_t found=0;
    uint32_t eventOffsets[2]={};
    for (size_t i=sizeof(prefix); i+15<=initSize; ++i) {
        if (init[i]!=0xe8 || memcmp(init+i+5,"\x48\x89\x83",3) ||
            memcmp(init+i+12,"\x48\x85\xc0",3)) continue;
        uint32_t storedOffset; int32_t relative;
        memcpy(&relative,init+i+1,4); memcpy(&storedOffset,init+i+8,4);
        uintptr_t target=initAddress+i+5+relative;
        if (storedOffset==cursorOffset && target==factoryAddress) {
            found=i; ++matches;
        }
        if (target==interruptFactoryAddress && interrupts<2)
            eventOffsets[interrupts++]=storedOffset;
        else if (target==interruptFactoryAddress) return false;
    }
    if (matches!=1 || interrupts!=2 || eventOffsets[0]!=cursorOffset+8 ||
        eventOffsets[1]!=cursorOffset+16) return false;
    layout={found,cursorOffset,eventOffsets[0],eventOffsets[1]}; return true;
}
} // namespace PVGDisplayWorkLoop
