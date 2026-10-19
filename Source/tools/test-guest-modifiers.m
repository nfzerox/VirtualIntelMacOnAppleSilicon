#import "VIMMetalView.h"
int main(void) {@autoreleasepool {
    VIMMetalView *view=[[VIMMetalView alloc] initWithFrame:NSMakeRect(0,0,64,64)];
    NSMutableArray *events=[NSMutableArray array];
    view.inputHandler=^(NSArray *batch){[events addObjectsFromArray:batch];};
    NSEvent *down=[NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:NSEventModifierFlagCommand
        timestamp:0 windowNumber:0 context:nil characters:@"n" charactersIgnoringModifiers:@"n" isARepeat:NO keyCode:45];
    NSEvent *up=[NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0
        timestamp:1 windowNumber:0 context:nil characters:@"n" charactersIgnoringModifiers:@"n" isARepeat:NO keyCode:45];
    // Direct method test, no synthetic events posted to the user's desktop.
    [view keyDown:down];[view keyUp:up];
    NSCAssert(events.count==4,@"expected modifier and key transitions");
    NSArray *names=@[@"meta_l",@"n",@"meta_l",@"n"];
    for(NSUInteger i=0;i<4;i++) {
        NSCAssert([events[i][@"data"][@"key"][@"data"] isEqual:names[i]],@"key ordering");
        NSCAssert([events[i][@"data"][@"down"] boolValue]==(i<2),@"down/up state");
    }
    puts("PASS: Command-N reconciles missing modifier notifications and releases keys");
}}
