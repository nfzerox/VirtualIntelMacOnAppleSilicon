/* Exercise the actual view's coordinate/filter path, without posting host
 * input, starting a VM, installing an event tap, or requesting permissions.
 * Key/capture state is supplied explicitly; this is not an AppKit focus test. */
#import "VIMMetalView.h"
#import <QuartzCore/QuartzCore.h>
@interface VIMMetalView (ReleaseShortcutFixture)
- (void)routeCapturedKey:(NSEvent *)event;
@end
@interface PointerWindow : NSWindow
@property BOOL fixtureKey;
@end
@implementation PointerWindow
- (BOOL)isKeyWindow {return self.fixtureKey;}
@end
static NSEvent *Mouse(VIMMetalView *view,NSWindow *window,NSEventType type,NSPoint point)
{
    return [NSEvent mouseEventWithType:type location:[view convertPoint:point toView:nil]
        modifierFlags:0 timestamp:0 windowNumber:window.windowNumber context:nil
        eventNumber:1 clickCount:1 pressure:0];
}
int main(void) {@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    PointerWindow *window=[[PointerWindow alloc]initWithContentRect:NSMakeRect(0,0,200,150)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    PointerWindow *other=[[PointerWindow alloc]initWithContentRect:NSMakeRect(0,0,200,150)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    VIMMetalView *view=[[VIMMetalView alloc]initWithFrame:NSMakeRect(20,30,100,80)];
    [window.contentView addSubview:view];window.fixtureKey=YES;
    CALayer *presentation=[view valueForKey:@"presentationLayer"];
    presentation.frame=CGRectMake(10,5,80,50); // Include real letterbox margins.
    NSMutableArray *events=[NSMutableArray array];
    view.inputHandler=^(NSArray *batch){[events addObjectsFromArray:batch];};
    [view mouseMoved:Mouse(view,window,NSEventTypeMouseMoved,NSMakePoint(50,30))];
    NSCAssert(events.count==2,@"An inside hover must reach the guest");
    NSCAssert([events[0][@"data"][@"value"] intValue]==16383&&
              [events[1][@"data"][@"value"] intValue]==16383,@"Map through the actual presentation rectangle");
    [events removeAllObjects];
    const NSPoint outside[]={{-1,30},{101,30},{50,-1},{50,81},{5,30},{95,30}};
    for(unsigned i=0;i<sizeof(outside)/sizeof(*outside);++i){
        [view mouseMoved:Mouse(view,window,NSEventTypeMouseMoved,outside[i])];
        [view mouseDragged:Mouse(view,window,NSEventTypeLeftMouseDragged,outside[i])];
        [view mouseDown:Mouse(view,window,NSEventTypeLeftMouseDown,outside[i])];
    }
    NSCAssert(!events.count,@"Outside hovers, uncaptured drags and letterbox clicks must not become edge motion");
    window.fixtureKey=NO;
    [view mouseMoved:Mouse(view,window,NSEventTypeMouseMoved,NSMakePoint(50,30))];
    window.fixtureKey=YES;
    [view mouseMoved:Mouse(view,other,NSEventTypeMouseMoved,NSMakePoint(50,30))];
    NSCAssert(!events.count,@"Background and foreign-window hovers must not enter the guest");
    [view setValue:@YES forKey:@"inputCaptured"]; // No tap/AX prompt in this fixture.
    [view mouseMoved:Mouse(view,window,NSEventTypeMouseMoved,NSMakePoint(200,-10))];
    NSCAssert(!events.count,@"Capture must not admit outside ordinary hover");
    [view mouseDragged:Mouse(view,window,NSEventTypeLeftMouseDragged,NSMakePoint(200,-10))];
    NSCAssert(events.count==2&&[events[0][@"data"][@"value"] intValue]==32767&&
              [events[1][@"data"][@"value"] intValue]==32767,@"A captured drag must still clamp to the boundary");
    [events removeAllObjects];
    [view mouseUp:Mouse(view,window,NSEventTypeLeftMouseUp,NSMakePoint(200,-10))];
    NSCAssert(events.count==1&&[events[0][@"data"][@"button"] isEqual:@"left"]&&
              ![events[0][@"data"][@"down"] boolValue],@"Outside release must not leave a button held");
    [view setValue:@NO forKey:@"inputCaptured"];
    NSEvent *(^modifiers)(NSEventModifierFlags)=^NSEvent *(NSEventModifierFlags flags){
        return [NSEvent keyEventWithType:NSEventTypeFlagsChanged location:NSZeroPoint modifierFlags:flags
            timestamp:0 windowNumber:window.windowNumber context:nil characters:@"" charactersIgnoringModifiers:@""
            isARepeat:NO keyCode:58];
    };
    NSCAssert(view.releaseWithControlOption,@"The default release shortcut is Control–Option");
    [view setValue:@YES forKey:@"inputCaptured"];
    [view routeCapturedKey:modifiers(NSEventModifierFlagCommand|NSEventModifierFlagOption)];
    NSCAssert(view.inputCaptured,@"Command–Option remains available to guest apps by default");
    [view routeCapturedKey:modifiers(NSEventModifierFlagControl|NSEventModifierFlagOption)];
    NSCAssert(!view.inputCaptured,@"Control–Option releases capture by default");
    view.releaseWithControlOption=NO;[view setValue:@YES forKey:@"inputCaptured"];
    [view routeCapturedKey:modifiers(NSEventModifierFlagControl|NSEventModifierFlagOption)];
    NSCAssert(view.inputCaptured,@"The unselected shortcut must not release capture");
    [view routeCapturedKey:modifiers(NSEventModifierFlagCommand|NSEventModifierFlagOption)];
    NSCAssert(!view.inputCaptured,@"An explicit Command–Option choice remains supported");
    puts("PASS actual pointer filtering: viewport/letterbox, key/foreign window, capture, outside drag and button release");
    return 0;
}}
