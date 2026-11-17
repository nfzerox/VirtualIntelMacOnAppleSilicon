// Finite offscreen AppKit chrome/frame-order regression. No VM or UI automation.
#import <Cocoa/Cocoa.h>
#import "../VirtualIntelMac/VIMToolbarAppearance.h"

static NSWindow *Window(void) {
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(80,80,800,600)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskResizable
        backing:NSBackingStoreBuffered defer:NO];
    window.releasedWhenClosed=NO;
    return window;
}
static void Chrome(NSWindow *window) {
    window.toolbar=[[NSToolbar alloc] initWithIdentifier:@"GuestStopped"];
    VIMApplyToolbarAppearance(window);
}
int main(void) {@autoreleasepool {
    [NSApplication sharedApplication];
    NSWindow *initial=Window();Chrome(initial);
    NSRect saved=initial.frame;NSSize content=initial.contentView.bounds.size;
    [initial close];
    NSRect old=saved;
    for(unsigned cycle=0;cycle<5;cycle++) {
        NSWindow *window=Window();[window setFrame:old display:NO];Chrome(window);
        old=window.frame;[window close];
    }
    printf("restore before chrome: height %.0f -> %.0f in five cycles\n",saved.size.height,old.size.height);
    for(unsigned cycle=0;cycle<5;cycle++) {
        NSWindow *window=Window();Chrome(window);[window setFrame:saved display:NO];
        NSCAssert(NSEqualRects(window.frame,saved),@"Saved outer frame must survive toolbar installation");
        NSCAssert(NSEqualSizes(window.contentView.bounds.size,content),@"Viewport must not gain toolbar height per boot");
        [window close];
    }
    puts("PASS: chrome before saved frame retains outer frame and viewport across five boots");
}}
