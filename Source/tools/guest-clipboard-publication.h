#pragma once
// clearContents/declareTypes publish ownership before the owner supplies data;
// setString need not advance changeCount again. Never commit that empty gap as
// the final copy. Back off only pending reads, without a deadline that discards
// a slow owner's eventual content. A new generation is always tried at once.
typedef struct {
    NSInteger change;
    NSTimeInterval nextRead, delay;
} VIMClipboardPendingPublication;
static inline BOOL VIMClipboardPublicationDue(VIMClipboardPendingPublication *state,
    NSInteger change,NSTimeInterval now) {
    if(!state->delay||state->change!=change){state->change=change;state->nextRead=0;state->delay=.2;}
    return now>=state->nextRead;
}
static inline void VIMClipboardPublicationPending(VIMClipboardPendingPublication *state,
    NSTimeInterval now) {
    state->nextRead=now+state->delay;state->delay=MIN(1,state->delay*2);
}
