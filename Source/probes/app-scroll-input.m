#import "VIMMetalView.h"
#include <math.h>

// No event tap, permissions, guest or input injected into the host.
@interface WheelEvent : NSEvent
@property CGFloat x,y,pointsX,pointsY;
@property BOOL precise,inverted;
@property NSEventPhase gesturePhase;
@end
@implementation WheelEvent
- (CGFloat)deltaX{return self.x;}
- (CGFloat)deltaY{return self.y;}
- (CGFloat)scrollingDeltaX{return self.pointsX;}
- (CGFloat)scrollingDeltaY{return self.pointsY;}
- (BOOL)hasPreciseScrollingDeltas{return self.precise;}
- (BOOL)isDirectionInvertedFromDevice{return self.inverted;}
- (NSEventPhase)phase{return self.gesturePhase;}
@end
static void CheckPair(NSArray *events,NSUInteger offset,NSString *button) {
    for(NSUInteger i=0;i<2;i++) {
        NSDictionary *event=events[offset+i];
        NSCAssert([event[@"type"] isEqual:@"btn"]&&[event[@"data"][@"button"] isEqual:button]&&
            [event[@"data"][@"down"] boolValue]==(i==0),@"Expected %@ at %lu: %@",button,(unsigned long)(offset+i),event);
    }
}
int main(void) {@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    VIMMetalView *view=[[VIMMetalView alloc] initWithFrame:NSMakeRect(0,0,32,32)];
    NSMutableArray *events=[NSMutableArray array];__block NSUInteger requests=0;
    view.inputHandler=^(NSArray *batch){requests++;[events addObjectsFromArray:batch];};
    WheelEvent *event=[WheelEvent new];event.precise=YES;event.y=.1;event.pointsY=10;
    for(NSUInteger i=0;i<9;i++)[view scrollWheel:event];
    NSCAssert(!events.count&&!requests,@"Fractional motion must not become nine full detents");
    [view scrollWheel:event];NSCAssert(events.count==2&&requests==1,@"Ten tenths conserve exactly one notch");CheckPair(events,0,@"wheel-up");
    [events removeAllObjects];requests=0;
    event.x=-2;event.y=1;[view scrollWheel:event];
    NSCAssert(events.count==6&&requests==1,@"Diagonal motion sends both axes in one transport request");
    CheckPair(events,0,@"wheel-right");CheckPair(events,2,@"wheel-right");CheckPair(events,4,@"wheel-up");
    [events removeAllObjects];event.inverted=YES;
    NSCAssert(event.isDirectionInvertedFromDevice,@"Fixture inversion must be enabled");[view scrollWheel:event];
    CheckPair(events,0,@"wheel-left");CheckPair(events,4,@"wheel-down");
    [events removeAllObjects];event.precise=NO;event.inverted=NO;event.pointsX=1;event.pointsY=-1;
    [view scrollWheel:event];NSCAssert(events.count==4,@"Discrete wheel uses scrolling deltas, not precise-device fields");
    NSCAssert(!event.hasPreciseScrollingDeltas&&event.scrollingDeltaX==1&&event.scrollingDeltaY==-1,@"Fixture discrete values");
    CheckPair(events,0,@"wheel-left");CheckPair(events,2,@"wheel-down");
    [events removeAllObjects];event.precise=YES;event.x=0;event.y=.6;[view scrollWheel:event];
    event.gesturePhase=NSEventPhaseBegan;[view scrollWheel:event];
    NSCAssert(!events.count,@"New gestures do not inherit an old gesture's fractional tail");
    event.gesturePhase=NSEventPhaseNone;event.y=.4;[view scrollWheel:event];
    NSCAssert(events.count==2,@"Fractions survive within the same gesture");
    [events removeAllObjects];event.y=.6;[view scrollWheel:event];[view releaseInputCapture];[events removeAllObjects];
    event.y=.4;[view scrollWheel:event];NSCAssert(!events.count,@"Capture loss clears residual scrolling");
    event.gesturePhase=NSEventPhaseBegan;event.x=1e200;event.y=-1e200;[view scrollWheel:event];
    NSCAssert(events.count==64,@"Large diagonal motion remains bounded to one 64-event packet");
    [events removeAllObjects];event.x=NAN;event.y=1;[view scrollWheel:event];
    NSCAssert(!events.count,@"Malformed nonfinite deltas cannot produce an input flood");
    [view releaseInputCapture];[events removeAllObjects];
    event.x=0;event.y=.8;event.gesturePhase=NSEventPhaseNone;[view scrollWheel:event];
    event.gesturePhase=NSEventPhaseCancelled;[view scrollWheel:event];
    event.gesturePhase=NSEventPhaseNone;event.y=.2;[view scrollWheel:event];
    NSCAssert(!events.count,@"Cancelled gestures cannot leak their remainder into later input");
    CGEventRef native=CGEventCreateScrollWheelEvent(NULL,kCGScrollEventUnitPixel,2,10,0);
    NSEvent *converted=[NSEvent eventWithCGEvent:native];
    NSCAssert(converted.hasPreciseScrollingDeltas&&converted.scrollingDeltaY==10&&converted.deltaY>0,
        @"Real AppKit precise-event conversion remains usable without posting host events");
    printf("Native AppKit conversion: %.3f points, %.3f normalized wheel units\n",converted.scrollingDeltaY,converted.deltaY);
    CFRelease(native);
    puts("PASS: precise fractions, both wheel axes, natural direction, gesture/focus boundaries and bounded single-request scrolling");return 0;
}}
