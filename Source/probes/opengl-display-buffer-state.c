#include "../PassthroughOpenGL/display-buffer-state.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    struct VIMDisplayBufferState s;uint32_t a,b,c,index;
    VIMDisplayBuffersInit(&s,10);
    uint64_t ta=VIMDisplayBuffersSubmit(&s,&a);
    assert(a==0 && ta==1 && VIMDisplayBuffersIndex(&s,&index) && index==0x80000001u);
    uint64_t tb=VIMDisplayBuffersSubmit(&s,&b),tc=VIMDisplayBuffersSubmit(&s,&c);
    assert(b==1 && c==2 && s.draw==VIM_DISPLAY_NO_BUFFER);
    assert(!VIMDisplayBuffersSubmit(&s,&index));
    assert(!VIMDisplayBuffersIndex(&s,&index));
    assert(!VIMDisplayBuffersPresented(&s,9,a,ta));
    assert(!VIMDisplayBuffersPresented(&s,10,b,tb));
    assert(VIMDisplayBuffersPresented(&s,10,a,ta));
    assert(s.draw==VIM_DISPLAY_NO_BUFFER && VIMDisplayBuffersPin(&s,a));
    assert(VIMDisplayBuffersPresented(&s,10,b,tb));
    assert(s.draw==VIM_DISPLAY_NO_BUFFER); /* retained old front cannot be reused */
    assert(!VIMDisplayBuffersUnpin(&s,10,a,ta+1));
    assert(VIMDisplayBuffersUnpin(&s,10,a,ta) && s.draw==a);
    assert(VIMDisplayBuffersPresented(&s,10,c,tc));
    assert(!VIMDisplayBuffersPresented(&s,10,c,tc));
    assert(VIMDisplayBuffersIndex(&s,&index) && index==a);
    VIMDisplayBuffersInit(&s,11);
    assert(!VIMDisplayBuffersPresented(&s,10,a,ta));
    ta=VIMDisplayBuffersSubmit(&s,&a);
    tb=VIMDisplayBuffersSubmit(&s,&b);
    tc=VIMDisplayBuffersSubmit(&s,&c);
    assert(!VIMDisplayBuffersDiscard(&s,10,a,ta));
    assert(VIMDisplayBuffersDiscard(&s,11,b,tb));
    assert(s.draw==b && s.front==VIM_DISPLAY_NO_BUFFER);
    assert(!VIMDisplayBuffersDiscard(&s,11,b,tb));
    assert(VIMDisplayBuffersPresented(&s,11,a,ta));
    assert(VIMDisplayBuffersPresented(&s,11,c,tc));
    assert(VIMDisplayBuffersIndex(&s,&index) && index==(0x80000000u|b));
    for(unsigned n=0;n<100000;++n){
        uint32_t slot;uint64_t ticket=VIMDisplayBuffersSubmit(&s,&slot);
        assert(ticket && VIMDisplayBuffersPresented(&s,11,slot,ticket));
        assert(s.draw!=slot && s.slots[s.draw].phase==VIMDisplayDrawing);
    }
    puts("PASS display-buffer ownership, saturation, pins, FIFO, stale completion and rotation");
    return 0;
}
