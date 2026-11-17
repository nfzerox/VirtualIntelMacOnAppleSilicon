#import "VIMMetalView.h"
#import <IOKit/hidsystem/IOLLEvent.h>
#import "VIMKeyboardLayout.h"

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
static NSEvent *Key(unsigned short key, BOOL down, NSString *characters) {
    return [NSEvent keyEventWithType:down?NSEventTypeKeyDown:NSEventTypeKeyUp location:NSZeroPoint
        modifierFlags:0 timestamp:0 windowNumber:0 context:nil characters:characters
        charactersIgnoringModifiers:characters isARepeat:NO keyCode:key];
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
    view.releaseWithControlOption=YES;
    [view captureInput];[view keyDown:key];
    [view routeCapturedKey:Flags(58,NSEventModifierFlagControl|NSEventModifierFlagOption)];
    NSCAssert(!view.inputCaptured&&window.firstResponder!=view&&![[view valueForKey:@"pressedKeys"] count],@"Control-Option releases keyboard focus and all guest keys");
    NSCAssert(lastBatch.count==5,@"The two held keys and three mouse buttons release in one transport request");
    [window makeFirstResponder:view];[view captureInput];[view keyDown:key];
    [window makeFirstResponder:window];
    NSCAssert(!view.inputCaptured&&![[view valueForKey:@"pressedKeys"] count],@"Moving keyboard focus within a window releases held guest keys");
    [window makeFirstResponder:view];[view captureInput];[events removeAllObjects];
    NSDictionary *international=@{@10:@"less",@50:@"grave_accent",@71:@"num_lock",@81:@"kp_equals",
        @93:@"yen",@94:@"ro",@95:@0x5c,@102:@"lang2",@104:@"lang1",@114:@"help",
        @105:@"f13",@107:@"f14",@113:@"f15",@106:@"f16",@64:@"f17",@79:@"f18",@80:@"f19",@90:@"f20"};
    for(NSNumber *code in international) {
        [view keyDown:Key(code.unsignedShortValue,YES,@"あ")];
        [view keyUp:Key(code.unsignedShortValue,NO,@"é")];
        NSCAssert(events.count==2,@"Each ISO/JIS/keypad/function position must have a press and release");
        for(NSUInteger i=0;i<2;i++) {
            NSDictionary *data=events[i][@"data"],*value=data[@"key"];
            NSCAssert([value[@"data"] isEqual:international[code]]&&[data[@"down"] boolValue]==(i==0),@"Physical position, not Unicode characters, determines the key");
            NSCAssert([value[@"type"] isEqual:[international[code] isKindOfClass:NSNumber.class]?@"number":@"qcode"],@"Preserve the QNUM-only JIS keypad-comma identity");
        }
        [events removeAllObjects];
    }
    [view keyDown:Key(95,YES,@",")];[events removeAllObjects];[view releaseInputCapture];
    NSDictionary *commaValue=@{@"type":@"number",@"data":@0x5c};
    NSCAssert([events[0][@"data"][@"key"] isEqual:commaValue]&&![events[0][@"data"][@"down"] boolValue],@"Focus loss must release numeric keys without inventing a QCode");
    [view captureInput];[events removeAllObjects];view.appleISOKeyboard=YES;
    for(NSNumber *code in @[@10,@50]) {
        [view keyDown:Key(code.unsignedShortValue,YES,@"§")];[view keyUp:Key(code.unsignedShortValue,NO,@"§")];
    }
    NSCAssert([events[0][@"data"][@"key"][@"data"] isEqual:@"grave_accent"]&&[events[2][@"data"][@"key"][@"data"] isEqual:@"less"],@"Virtual ISO reverses Apple's USB-to-Carbon swap exactly once");
    NSCAssert([VIMKeyboardLayoutName(kKeyboardANSI) isEqual:@"ansi"]&&[VIMKeyboardLayoutName(kKeyboardISO) isEqual:@"iso"]&&[VIMKeyboardLayoutName(kKeyboardJIS) isEqual:@"jis"],@"Preserve each physical keyboard kind independently of language");
    printf("Host physical keyboard: %s\n",VIMHostKeyboardLayout().UTF8String);
    puts("PASS: modifiers, shortcut/focus release, ISO/JIS language and punctuation keys, keypad and F13-F20; no host input injection");
    return 0;
}}
