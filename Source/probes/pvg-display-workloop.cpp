#include "PVGDisplayWorkLoop.h"
#include <assert.h>
#include <stdio.h>

int main() {
    uint8_t init[512]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x45,0x89,0xc7,
        0x49,0x89,0xcc,0x49,0x89,0xd5,0x49,0x89,0xf6,0x48,0x89,0xfb};
    uint8_t cursor[32]={0x55,0x48,0x89,0xe5,0x48,0x83,0xec,0x30,0x4c,0x8b,0x87};
    const uintptr_t address=0x100000, factory=0x120000, interrupt=0x130000;
    const size_t site=319;
    const uint8_t call[]={0xe8,0,0,0,0,0x48,0x89,0x83,0,0,0,0,0x48,0x85,0xc0};
    memcpy(init+site,call,sizeof(call));
    uint32_t offset=0x350; int32_t delta=factory-(address+site+5);
    memcpy(init+site+1,&delta,4); memcpy(init+site+8,&offset,4); memcpy(cursor+11,&offset,4);
    for (unsigned i=0;i<2;i++) {
        size_t event=400+i*30;
        memcpy(init+event,call,sizeof(call)); delta=interrupt-(address+event+5);
        uint32_t eventOffset=offset+8+i*8;
        memcpy(init+event+1,&delta,4); memcpy(init+event+8,&eventOffset,4);
    }
    PVGDisplayWorkLoop::Layout found {};
    assert(PVGDisplayWorkLoop::findFactoryCall(init,sizeof(init),cursor,sizeof(cursor),address,factory,interrupt,found)&&found.call==site&&found.workLoop==offset&&found.online==offset+8&&found.offline==offset+16);
    assert(!PVGDisplayWorkLoop::findFactoryCall(init,sizeof(init),cursor,sizeof(cursor),address,factory+1,interrupt,found));
    cursor[11]++;
    assert(!PVGDisplayWorkLoop::findFactoryCall(init,sizeof(init),cursor,sizeof(cursor),address,factory,interrupt,found));
    cursor[11]--; init[20]^=1;
    assert(!PVGDisplayWorkLoop::findFactoryCall(init,sizeof(init),cursor,sizeof(cursor),address,factory,interrupt,found));
    init[20]^=1;
    init[408]^=1;
    assert(!PVGDisplayWorkLoop::findFactoryCall(init,sizeof(init),cursor,sizeof(cursor),address,factory,interrupt,found));
    init[408]^=1;
    memcpy(init+460,init+site,sizeof(call)); delta=factory-(address+465); memcpy(init+461,&delta,4);
    assert(!PVGDisplayWorkLoop::findFactoryCall(init,sizeof(init),cursor,sizeof(cursor),address,factory,interrupt,found));
    puts("PASS matched factory target/cursor accessor, preserved framebuffer ABI, unknown and ambiguous layouts refused");
}
