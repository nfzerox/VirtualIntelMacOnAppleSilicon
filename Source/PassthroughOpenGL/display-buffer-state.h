/* Display-buffer ownership primitive for the native page-flip path.
 * No allocation, polling, pixel copies or OS dependencies. Caller serializes
 * all access and owns the actual GPU resources/fences. Not enabled by merely
 * including this header: presentation integration must supply real releases.
 */
#ifndef VIM_DISPLAY_BUFFER_STATE_H
#define VIM_DISPLAY_BUFFER_STATE_H
#include <stdint.h>
#define VIM_DISPLAY_BUFFERS 3u
#define VIM_DISPLAY_NO_BUFFER UINT32_MAX
enum VIMDisplayBufferPhase { VIMDisplayFree, VIMDisplayDrawing,
    VIMDisplayQueued, VIMDisplayFront, VIMDisplayRetired };
struct VIMDisplayBufferSlot {
    uint64_t ticket;
    uint32_t phase,pins;
};
struct VIMDisplayBufferState {
    struct VIMDisplayBufferSlot slots[VIM_DISPLAY_BUFFERS];
    uint64_t generation,next_ticket;
    uint32_t draw,front,next,invalidated;
};
/* Initialize a NEW generation only after old resources are retained elsewhere
 * or drained. Generation checks reject delayed callbacks after resize. */
static inline void VIMDisplayBuffersInit(struct VIMDisplayBufferState *s,uint64_t generation)
{
    unsigned i;
    for(i=0;i<VIM_DISPLAY_BUFFERS;++i){
        s->slots[i].ticket=0;s->slots[i].pins=0;s->slots[i].phase=VIMDisplayFree;
    }
    s->generation=generation;s->next_ticket=1;s->draw=0;
    s->front=VIM_DISPLAY_NO_BUFFER;s->next=1;s->invalidated=1;
    s->slots[0].phase=VIMDisplayDrawing;
}
static inline void VIMDisplayBuffersAcquire(struct VIMDisplayBufferState *s)
{
    unsigned i;
    if(s->draw!=VIM_DISPLAY_NO_BUFFER)return;
    for(i=0;i<VIM_DISPLAY_BUFFERS;++i){
        unsigned index=(s->next+i)%VIM_DISPLAY_BUFFERS;
        if(s->slots[index].phase==VIMDisplayFree && !s->slots[index].pins){
            s->slots[index].phase=VIMDisplayDrawing;s->draw=index;
            s->next=(index+1)%VIM_DISPLAY_BUFFERS;return;
        }
    }
}
/* Called only after successful GPU publication/enqueue. Failed submission
 * leaves the drawing slot intact. No slot may be reused while queued/front. */
static inline uint64_t VIMDisplayBuffersSubmit(struct VIMDisplayBufferState *s,uint32_t *index)
{
    uint64_t ticket;
    if(!index || s->draw==VIM_DISPLAY_NO_BUFFER || !s->next_ticket)return 0;
    *index=s->draw;ticket=s->next_ticket++;
    s->slots[*index].ticket=ticket;s->slots[*index].phase=VIMDisplayQueued;
    s->draw=VIM_DISPLAY_NO_BUFFER;VIMDisplayBuffersAcquire(s);return ticket;
}
/* Host presentation completion, NOT glFlush or a socket ACK. FIFO completion
 * is intentional: silently accepting reordered frames would show old pixels. */
static inline int VIMDisplayBuffersPresented(struct VIMDisplayBufferState *s,
    uint64_t generation,uint32_t index,uint64_t ticket)
{
    unsigned i;
    if(generation!=s->generation || index>=VIM_DISPLAY_BUFFERS ||
       s->slots[index].phase!=VIMDisplayQueued || s->slots[index].ticket!=ticket)return 0;
    for(i=0;i<VIM_DISPLAY_BUFFERS;++i)
        if(s->slots[i].phase==VIMDisplayQueued && s->slots[i].ticket<ticket)return 0;
    if(s->front!=VIM_DISPLAY_NO_BUFFER){
        struct VIMDisplayBufferSlot *old=&s->slots[s->front];
        old->phase=old->pins?VIMDisplayRetired:VIMDisplayFree;
    }
    s->front=index;s->slots[index].phase=VIMDisplayFront;
    VIMDisplayBuffersAcquire(s);return 1;
}
/* Pin completed front/retired content for capture/copy-forward. Pins prevent
 * reuse, not GPU synchronization; caller must establish completion first. */
static inline int VIMDisplayBuffersPin(struct VIMDisplayBufferState *s,uint32_t index)
{
    if(index>=VIM_DISPLAY_BUFFERS || s->slots[index].pins==UINT32_MAX ||
       (s->slots[index].phase!=VIMDisplayFront && s->slots[index].phase!=VIMDisplayRetired))return 0;
    ++s->slots[index].pins;return 1;
}
/* Consumer released an unpresented submission (hidden window, sleep or
 * rejected frame). It must no longer access the resource. Do not promote it
 * to front or leave it queued forever. Force fresh history on the next draw. */
static inline int VIMDisplayBuffersDiscard(struct VIMDisplayBufferState *s,
    uint64_t generation,uint32_t index,uint64_t ticket)
{
    if(generation!=s->generation || index>=VIM_DISPLAY_BUFFERS ||
       s->slots[index].phase!=VIMDisplayQueued || s->slots[index].ticket!=ticket)return 0;
    s->slots[index].phase=VIMDisplayFree;s->invalidated=1;
    VIMDisplayBuffersAcquire(s);return 1;
}
static inline int VIMDisplayBuffersUnpin(struct VIMDisplayBufferState *s,
    uint64_t generation,uint32_t index,uint64_t ticket)
{
    if(generation!=s->generation || index>=VIM_DISPLAY_BUFFERS ||
       s->slots[index].ticket!=ticket || !s->slots[index].pins)return 0;
    if(!--s->slots[index].pins && s->slots[index].phase==VIMDisplayRetired)
        s->slots[index].phase=VIMDisplayFree;
    VIMDisplayBuffersAcquire(s);return 1;
}
/* Native control 18 encoding: next drawing index plus lost-history bit.
 * When saturated, report not-ready through control 16; never invent an index. */
static inline int VIMDisplayBuffersIndex(struct VIMDisplayBufferState *s,uint32_t *value)
{
    if(!value || s->draw==VIM_DISPLAY_NO_BUFFER)return 0;
    *value=s->draw | (s->invalidated?0x80000000u:0u);s->invalidated=0;return 1;
}
#endif
