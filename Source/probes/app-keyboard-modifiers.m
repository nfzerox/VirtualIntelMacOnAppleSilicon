#import "VIMMetalView.h"
#import <IOKit/hidsystem/IOLLEvent.h>

@interface VIMMetalView (InputFixture)
- (void)routeCapturedKey:(NSEvent *)event;
- (void)captureInput;
@end
// Exercise production routing without requesting Accessibility permission or
// installing an event tap. Never post a key to the host or another process.
@interface VIMInputFixtureView : VIMMetalView
@end
@implementation VIMInputFixtureView
- (void)captureInput {[self setValue:@YES forKey:@"inputCaptured"];}
@end

// Direct method test only: never injects events into another app or the OS.
static NSEvent *Flags(unsigned short key, NSEventModifierFlags flags) {
    return [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint
        modifierFlags:flags timestamp:0 windowNumber:0 context:nil
        characters:@"" charactersIgnoringModifiers:@"" isARepeat:NO keyCode:key];
}
int main(void) { @autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    VIMMetalView *view=[[VIMInputFixtureView alloc] initWithFrame:NSMakeRect(0,0,32,32)];
    [view captureInput];
    NSMutableArray *events=[NSMutableArray array];
    __block NSArray *lastBatch=nil;
    view.inputHandler=^(NSArray *batch){lastBatch=batch;[events addObjectsFromArray:batch];};
    [view flagsChanged:Flags(0,NSEventModifierFlagShift)];
    [view flagsChanged:Flags(0,0)];
    [view flagsChanged:Flags(60,NSEventModifierFlagShift)];
    [view flagsChanged:Flags(60,0)];
    [view flagsChanged:Flags(63,NSEventModifierFlagFunction)];
    [view flagsChanged:Flags(63,0)];
    NSArray *expected=@[@"shift",@"shift",@"shift_r",@"shift_r"];
    if(events.count!=expected.count)return 1;
    for(NSUInteger i=0;i<expected.count;i++) {
        NSDictionary *data=events[i][@"data"];
        if(![data[@"key"][@"data"] isEqual:expected[i]] ||
           [data[@"down"] boolValue]!=(i%2==0))return 2;
    }
    [events removeAllObjects];
    [view flagsChanged:Flags(55,NSEventModifierFlagCommand)];
    NSEvent *key=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand timestamp:0 windowNumber:0 context:nil characters:@"q" charactersIgnoringModifiers:@"q" isARepeat:NO keyCode:12];
    [view keyDown:key];[view flagsChanged:Flags(55,0)];
    NSCAssert([events.lastObject[@"data"][@"key"][@"data"] isEqual:@"q"]&&![events.lastObject[@"data"][@"down"] boolValue],@"Command release must release a shortcut whose key-up AppKit consumed");
    [events removeAllObjects];[view flagsChanged:Flags(55,0)];
    NSCAssert(!events.count&&![[view valueForKey:@"pressedKeys"] count],@"A duplicate release must never press Command");
    [view flagsChanged:Flags(55,NSEventModifierFlagCommand|NX_DEVICELCMDKEYMASK)];
    [view flagsChanged:Flags(54,NSEventModifierFlagCommand|NX_DEVICELCMDKEYMASK|NX_DEVICERCMDKEYMASK)];
    [view flagsChanged:Flags(55,NSEventModifierFlagCommand|NX_DEVICERCMDKEYMASK)];
    NSCAssert([[view valueForKey:@"pressedKeys"] isEqual:[NSSet setWithObject:@"meta_r"]],@"Left Command release preserves the held right Command");
    NSUInteger count=events.count;
    [view flagsChanged:Flags(55,NSEventModifierFlagCommand|NX_DEVICERCMDKEYMASK)];
    NSCAssert(events.count==count,@"Duplicate left release while right is held is idempotent");
    [view flagsChanged:Flags(54,0)];NSCAssert(![[view valueForKey:@"pressedKeys"] count],@"Both sides released");
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,100,100) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    window.contentView=view;[window makeFirstResponder:view];[events removeAllObjects];
    [view releaseInputCapture];[events removeAllObjects];
    NSCAssert(![view performKeyEquivalent:key]&&!events.count,@"Uncaptured Command-Q belongs to the host, not the guest");
    [view flagsChanged:Flags(55,NSEventModifierFlagCommand)];[view keyUp:key];
    NSCAssert(!events.count,@"Uncaptured modifier and key-up events must not reach the guest");
    [view captureInput];[view keyDown:key];
    [view routeCapturedKey:Flags(58,NSEventModifierFlagControl|NSEventModifierFlagOption)];
    NSCAssert(!view.inputCaptured&&window.firstResponder!=view&&![[view valueForKey:@"pressedKeys"] count],@"Control-Option releases keyboard focus and all guest keys");
    NSCAssert(lastBatch.count==5,@"The two held keys and three mouse buttons release in one transport request");
    [window makeFirstResponder:view];[view captureInput];[view keyDown:key];
    [window makeFirstResponder:window];
    NSCAssert(!view.inputCaptured&&![[view valueForKey:@"pressedKeys"] count],@"Moving keyboard focus within a window releases held guest keys");
    puts("PASS: aggregate/device modifiers, duplicate releases, both Command keys, consumed shortcut release, uncapture and focus loss; no host input injection");
    return 0;
}}
