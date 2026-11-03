// Reproducible real-input desktop workload. No GL injection, screen capture,
// private window-server API, preference change or system-driver installation.
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <dlfcn.h>
#include <unistd.h>

static volatile sig_atomic_t cancelled;
static CGEventSourceRef mouse_source;
static int64_t mouse_number;
static CGPoint previous_mouse;
static pid_t window_owner;
static void cancel_run(int signal_number) { (void)signal_number; cancelled=1; }
static double now(void) {
    static mach_timebase_info_data_t scale;
    if(!scale.denom)mach_timebase_info(&scale);
    return (double)mach_absolute_time()*scale.numer/scale.denom/1e9;
}
static void wait_until(double target) {
    double remaining;
    while(!cancelled && (remaining=target-now())>0)
        usleep((useconds_t)MIN(remaining*1e6,10000.));
}
static void mouse(CGEventType type,CGPoint point) {
    // The period SDK recommends CombinedSessionState for login-session input.
    // Share its accumulated state, but give each posted event its own object.
    if(!mouse_source)mouse_source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    CGEventRef mouse_event=CGEventCreateMouseEvent(mouse_source,type,point,kCGMouseButtonLeft);
    CGEventSetTimestamp(mouse_event,(CGEventTimestamp)(now()*1e9));
    if(type==kCGEventLeftMouseDown)mouse_number++;
    CGEventSetIntegerValueField(mouse_event,kCGMouseEventNumber,mouse_number);
    CGEventSetIntegerValueField(mouse_event,kCGMouseEventClickState,1);
    CGEventSetIntegerValueField(mouse_event,kCGMouseEventDeltaX,llround(point.x-previous_mouse.x));
    CGEventSetIntegerValueField(mouse_event,kCGMouseEventDeltaY,llround(point.y-previous_mouse.y));
    CGEventSetDoubleValueField(mouse_event,kCGMouseEventPressure,type==kCGEventLeftMouseUp?0:1);
    CGEventPost(kCGHIDEventTap,mouse_event);
    CFRelease(mouse_event);
    previous_mouse=point;
}
static void escape_selection(void) {
    // Real Preview input only: cancel the previous selection before starting
    // another, rather than accidentally dragging its interior. No image edit
    // or preferences change; leave the final selection visible for host capture.
    for(int down=1;down>=0;down--) {
        CGEventRef event=CGEventCreateKeyboardEvent(NULL,53,down);
        CGEventPost(kCGHIDEventTap,event);CFRelease(event);
    }
    wait_until(now()+.15);
}
static const char *operation_name(NSUInteger operations) {
    switch(operations) {
        case 1:return "drag";case 2:return "resize";
        case 3:return "selection-finder";case 4:return "selection-desktop";
        case 5:return "selection-preview";default:return "all";
    }
}
static BOOL bounds_for(CGWindowID number,CGRect *bounds) {
    NSArray *windows=(id)CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,number);
    BOOL found=NO;
    for(NSDictionary *window in windows) {
        if([[window objectForKey:(id)kCGWindowNumber] unsignedIntValue]==number &&
            [[window objectForKey:(id)kCGWindowOwnerPID] intValue]==window_owner) {
            found=CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)[window objectForKey:(id)kCGWindowBounds],bounds);
            break;
        }
    }
    [windows release];return found;
}
static NSDictionary *rect_json(CGRect r) {
    return @{@"X":@(r.origin.x),@"Y":@(r.origin.y),@"Width":@(r.size.width),@"Height":@(r.size.height)};
}
static BOOL close_rect(CGRect a,CGRect b) {
    return fabs(a.origin.x-b.origin.x)<=3 && fabs(a.origin.y-b.origin.y)<=3 &&
        fabs(a.size.width-b.size.width)<=3 && fabs(a.size.height-b.size.height)<=3;
}
// Selection trials keep every window fixed. Desktop trials choose a leftmost
// exposed patch outside ordinary windows, the menu bar and Dock. The caller
// should leave that patch and Finder's content area free of icons/files; window
// metadata cannot prove that a visible rubber-band was actually drawn.
static CGRect selection_rect(CGRect window,CGRect screen,BOOL desktop) {
    if(!desktop) {
        double x=MAX(window.origin.x+220,CGRectGetMidX(window));
        return CGRectMake(x,window.origin.y+100,
            MIN(220.,CGRectGetMaxX(window)-x-32),MIN(160.,window.size.height-140));
    }
    NSArray *windows=(id)CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly,kCGNullWindowID);
    CGRect selected=CGRectZero;
    for(double x=screen.origin.x+24;x+220<CGRectGetMaxX(screen)-24 && !selected.size.width;x+=24)
        for(double y=screen.origin.y+64;y+160<CGRectGetMaxY(screen)-96;y+=24) {
            CGRect candidate=CGRectMake(x,y,220,160);BOOL clear=YES;
            for(NSDictionary *entry in windows) {
                CGRect r;
                if([[entry objectForKey:(id)kCGWindowLayer] intValue]==0 &&
                   CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)[entry objectForKey:(id)kCGWindowBounds],&r) &&
                   CGRectIntersectsRect(candidate,CGRectInset(r,-12,-12))) { clear=NO;break; }
            }
            if(clear){selected=candidate;break;}
        }
    [windows release];return selected;
}
static BOOL wait_for_bounds(CGWindowID window,CGRect expected,CGRect *observed) {
    double until=now()+3;
    do {
        if(bounds_for(window,observed)&&close_rect(*observed,expected))return YES;
        wait_until(now()+.05);
    }while(!cancelled&&now()<until);
    return NO;
}
// Setup/cleanup also uses real input, not AX frame setters. These short moves
// are outside measured phases; the original user's geometry is restored.
static BOOL position_window(CGWindowID window,CGRect target,BOOL resize) {
    CGRect before=CGRectZero;if(!bounds_for(window,&before))return NO;
    CGPoint from=resize?CGPointMake(CGRectGetMaxX(before)-4,CGRectGetMaxY(before)-4):
        CGPointMake(CGRectGetMinX(before)+120,CGRectGetMinY(before)+10);
    CGPoint to=resize?CGPointMake(from.x+target.size.width-before.size.width,from.y+target.size.height-before.size.height):
        CGPointMake(from.x+target.origin.x-before.origin.x,from.y+target.origin.y-before.origin.y);
    mouse(kCGEventMouseMoved,from);wait_until(now()+.08);
    mouse(kCGEventLeftMouseDown,from);wait_until(now()+.08);
    mouse(kCGEventLeftMouseDragged,to);wait_until(now()+.08);
    mouse(kCGEventLeftMouseUp,to);
    CGRect expected=resize?CGRectMake(before.origin.x,before.origin.y,target.size.width,target.size.height):
        CGRectMake(target.origin.x,target.origin.y,before.size.width,before.size.height),observed=CGRectZero;
    BOOL matched=wait_for_bounds(window,expected,&observed);
    if(!matched)fprintf(stderr,"Preparation/restore %s mismatch: expected %.0f,%.0f %.0fx%.0f; got %.0f,%.0f %.0fx%.0f\n",
        resize?"resize":"move",expected.origin.x,expected.origin.y,expected.size.width,expected.size.height,
        observed.origin.x,observed.origin.y,observed.size.width,observed.size.height);
    // Geometry may be visible before AppKit consumes the button-up ending its
    // tracking loop. This untimed settling allowance never slows a drag path.
    if(matched)wait_until(now()+.35);
    return matched;
}

@interface VIMInteractionRunner:NSObject <NSApplicationDelegate>
@property(assign) pid_t targetPID;
@property(assign) CGWindowID targetWindow;
@property(assign) NSUInteger cycles;
@property(assign) double seconds;
@property(assign) double rate;
@property(assign) uint32_t seed;
@property(assign) NSUInteger operations; // 0=all, 1=drag, 2=resize
@property(assign) int result;
@end

@implementation VIMInteractionRunner
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    (void)notification;
    // Deliberately exercise a real system app, not a synthetic test window.
    NSArray *finders=[NSRunningApplication runningApplicationsWithBundleIdentifier:@"com.apple.finder"];
    if(!self.targetPID && finders.count)self.targetPID=[[finders objectAtIndex:0] processIdentifier];
    [[NSRunningApplication runningApplicationWithProcessIdentifier:self.targetPID]
        activateWithOptions:NSApplicationActivateIgnoringOtherApps];
    [NSThread detachNewThreadSelector:@selector(runWorkload) toTarget:self withObject:nil];
}
- (void)runWorkload {
    @autoreleasepool {
        NSMutableArray *phases=[NSMutableArray array];
        window_owner=self.targetPID;
        wait_until(now()+.6);
        if(!self.targetWindow) {
            NSArray *windows=(id)CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly,kCGNullWindowID);
            double area=0;
            for(NSDictionary *entry in windows) {
                CGRect r;
                if([[entry objectForKey:(id)kCGWindowOwnerPID] intValue]!=self.targetPID ||
                    [[entry objectForKey:(id)kCGWindowLayer] intValue]!=0 ||
                    !CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)[entry objectForKey:(id)kCGWindowBounds],&r))continue;
                if(r.size.width*r.size.height>area) {
                    area=r.size.width*r.size.height;self.targetWindow=[[entry objectForKey:(id)kCGWindowNumber] unsignedIntValue];
                }
            }
            [windows release];
        }
        if(self.operations==5 && ![[NSRunningApplication runningApplicationWithProcessIdentifier:self.targetPID].bundleIdentifier
                                  isEqualToString:@"com.apple.Preview"]) {
            fprintf(stderr,"selection-preview requires the real Preview application.\n");
            self.result=2;dispatch_async(dispatch_get_main_queue(),^{[NSApp terminate:nil];});return;
        }
        CGRect userOriginal=CGRectZero,screen=CGDisplayBounds(CGMainDisplayID());
        BOOL selection=self.operations>=3;
        BOOL found=bounds_for(self.targetWindow,&userOriginal);
        CGEventRef cursorEvent=CGEventCreate(NULL);CGPoint userCursor=CGEventGetLocation(cursorEvent);CFRelease(cursorEvent);
        // Canonical logical geometry makes trials comparable while retaining
        // the guest's actual display resolution/Retina density and effects.
        CGRect canonical=CGRectMake(screen.origin.x+floor((screen.size.width-720)/3),screen.origin.y+100,720,440);
        CGRect corner=userOriginal;corner.origin=CGPointMake(screen.origin.x+16,screen.origin.y+36);
        BOOL prepared=self.targetPID>0&&found&&userOriginal.size.width>=300&&userOriginal.size.height>=200&&
            screen.size.width>=1024&&screen.size.height>=720;
        BOOL preparationAttempted=prepared;
        if(prepared && !selection)prepared=position_window(self.targetWindow,corner,NO)&&
            position_window(self.targetWindow,canonical,YES)&&position_window(self.targetWindow,canonical,NO);
        CGRect original=CGRectZero;
        found=bounds_for(self.targetWindow,&original);
        // Leave the menu bar, Dock and title-bar controls alone. Refuse a badly
        // placed target rather than silently clamp the path and change workload.
        double dx=MIN(160.,screen.size.width-CGRectGetMaxX(original)-24);
        double dy=MIN(90.,screen.size.height-CGRectGetMaxY(original)-90);
        double left=MIN(dx,original.origin.x-16),up=MIN(dy,original.origin.y-30);
        CGRect selectionBox=selection?selection_rect(original,screen,self.operations==4):CGRectZero;
        BOOL valid=prepared && found && original.size.width>=300 && original.size.height>=200 &&
            (selection?(selectionBox.size.width>=64 && selectionBox.size.height>=64):
             (dx>=48 && dy>=32 && left>=32 && up>=16));
        BOOL trusted=AXIsProcessTrusted();
        double started=now();
        if(!valid) {
            fprintf(stderr,"Choose an ordinary visible Finder window away from screen edges, with room to move and grow.\n");
            self.result=2;
        }
        if(valid)fprintf(stderr,"Ready: real Finder/window workload, pid=%d window=%u operations=%s.\n",self.targetPID,self.targetWindow,
            operation_name(self.operations));
        for(NSUInteger cycle=0;valid && !cancelled && cycle<self.cycles;cycle++) {
            for(NSUInteger phase=0;phase<7 && !cancelled;phase++) {
                @autoreleasepool {
                    BOOL resize=phase>=4,reverse=phase==2||phase==5;
                    if(selection && phase!=3)continue;
                    if((self.operations==1&&resize)||(self.operations==2&&!resize))continue;
                    NSString *pattern=phase==0?@"circle":phase==3?@"seeded-reversals":phase==6?@"resize-reversals":@"diagonal";
                    double duration=(phase==0||phase==3||phase==6)?self.seconds:self.seconds*.35;
                    CGRect before=original,expected=original;
                    if(!resize && reverse){before.origin.x+=dx;before.origin.y+=dy;}
                    if(resize && reverse){before.size.width+=dx;before.size.height+=dy;}
                    if(!reverse && (phase==1||phase==4)) {
                        if(resize){expected.size.width+=dx;expected.size.height+=dy;}
                        else {expected.origin.x+=dx;expected.origin.y+=dy;}
                    }
                    // Use the observed start, not accumulated assumptions about
                    // AppKit's integral resize increments across earlier phases.
                    if(!bounds_for(self.targetWindow,&before)){valid=NO;self.result=3;break;}
                    // The centered Finder title contains a draggable folder
                    // proxy. Use blank title-bar space, not that proxy icon.
                    CGPoint from=resize?CGPointMake(CGRectGetMaxX(before)-4,CGRectGetMaxY(before)-4):
                        CGPointMake(CGRectGetMinX(before)+120,CGRectGetMinY(before)+10);
                    if(selection)from=selectionBox.origin;
                    CGPoint to=resize?CGPointMake(from.x+expected.size.width-before.size.width,from.y+expected.size.height-before.size.height):
                        CGPointMake(from.x+expected.origin.x-before.origin.x,from.y+expected.origin.y-before.origin.y);
                    if(phase==0||phase==3||phase==6)to=from;
                    NSUInteger requested=(NSUInteger)ceil(duration*self.rate),capacity=requested+2,count=0;
                    typedef struct {double time,x,y,post_ms;} InputSample;
                    InputSample *input=calloc(capacity,sizeof(*input));
                    if(!input){self.result=5;valid=NO;break;}
                    // Fixed seed, normalized waypoints, independent of machine
                    // speed. Both axes reverse concurrently, ending at origin.
                    CGPoint points[13]={{0,0}};uint32_t state=self.seed;
                    for(NSUInteger j=1;j<12;j++) {
                        state=state*1664525u+1013904223u;points[j].x=(state>>8)/(double)0xffffff;
                        state=state*1664525u+1013904223u;points[j].y=(state>>8)/(double)0xffffff;
                        if(!resize && !selection){points[j].x=points[j].x*2-1;points[j].y=points[j].y*2-1;}
                    }
                    if(self.operations==5) {
                        escape_selection();
                        // A nonempty final rectangle makes post-run captures
                        // reproducible. Finder's original closed path stays as-is.
                        points[12]=CGPointMake(.95,.85);
                    }
                    mouse(kCGEventMouseMoved,from);wait_until(now()+.05);
                    mouse(kCGEventLeftMouseDown,from);wait_until(now()+.05);
                    double begin=now(),maxLate=0,maxPost=0,observerSeconds=0;NSUInteger next=1,check=0;
                    fprintf(stderr,"Phase begin cycle=%lu operation=%s pattern=%s reverse=%d at=%.3f duration=%.3f\n",
                        (unsigned long)cycle,selection?operation_name(self.operations):
                        resize?"resize":"drag",pattern.UTF8String,reverse,begin-started,duration);
                    NSMutableArray *observations=[NSMutableArray array];BOOL movedDuringPath=NO;
                    CGPoint last=from;
                    while(!cancelled && count<capacity) {
                        double deadline=begin+next/self.rate;wait_until(MIN(deadline,begin+duration));
                        double actual=now(),t=MIN(1.,(actual-begin)/duration);
                        maxLate=MAX(maxLate,actual-deadline);
                        CGPoint p=CGPointMake(from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t);
                        if(phase==0) {
                            // Two complete circles in two seconds by default:
                            // rapid multidirectional travel, not a slow sweep.
                            p=CGPointMake(from.x+left*sin(4*M_PI*t),from.y+dy*(1-cos(4*M_PI*t))*.5);
                        } else if(phase==3||phase==6) {
                            double segment=t*12;NSUInteger a=MIN(11,(NSUInteger)segment);double f=segment-a;
                            double x=points[a].x*(1-f)+points[a+1].x*f;
                            double y=points[a].y*(1-f)+points[a+1].y*f;
                            p=CGPointMake(from.x+x*(selection?selectionBox.size.width:resize?dx:left),
                                from.y+y*(selection?selectionBox.size.height:resize?dy:up));
                        }
                        mouse(kCGEventLeftMouseDragged,p);
                        double posted=now(),cost=(posted-actual)*1000;maxPost=MAX(maxPost,cost);
                        input[count++]=(InputSample){actual-begin,p.x,p.y,cost};last=p;
                        // Three metadata-only checkpoints prove a closed path
                        // really moved/resized, not merely that it ended where
                        // it began. Account for this observer cost separately.
                        const double checkpoints[]={.2,.55,.8};
                        if(check<3&&t>=checkpoints[check]) {
                            CGRect r=CGRectZero;double readStarted=now();
                            if(bounds_for(self.targetWindow,&r)) {
                                if(fabs(r.origin.x-before.origin.x)>8||fabs(r.origin.y-before.origin.y)>8||
                                    fabs(r.size.width-before.size.width)>8||fabs(r.size.height-before.size.height)>8)movedDuringPath=YES;
                                [observations addObject:@{@"Seconds":@(now()-begin),@"Bounds":rect_json(r)}];
                            }
                            observerSeconds+=now()-readStarted;check++;
                        }
                        if(t>=1)break;
                        // Never replay overdue positions in slow motion. Match
                        // live input coalescing by jumping to the current path
                        // point; preserve missed deadlines as measured evidence.
                        NSUInteger following=MIN((NSUInteger)ceil(duration*self.rate)+1,(NSUInteger)floor((posted-begin)*self.rate)+1);
                        next=MAX(next+1,following);
                    }
                    mouse(kCGEventLeftMouseUp,last);
                    double emittedDuration=now()-begin;
                    NSUInteger missed=requested>count?requested-count:0;
                    NSMutableArray *samples=[NSMutableArray arrayWithCapacity:count];
                    for(NSUInteger j=0;j<count;j++)[samples addObject:@[@(input[j].time),@(input[j].x),@(input[j].y),@(input[j].post_ms)]];
                    free(input);
                    // Bounded acceptance wait is outside the timed movement.
                    CGRect observed=CGRectZero;BOOL matched=wait_for_bounds(self.targetWindow,expected,&observed)&&
                        (selection?!movedDuringPath:movedDuringPath);
                    [phases addObject:@{@"Cycle":@(cycle),@"Operation":selection?
                        [NSString stringWithUTF8String:operation_name(self.operations)]:resize?@"resize":@"drag",@"Reverse":@(reverse),
                        @"Pattern":pattern,@"StartSeconds":@(begin-started),@"DurationSeconds":@(duration),@"Events":samples,
                        @"ActualInputSeconds":@(emittedDuration),@"MissedInputDeadlines":@(missed),@"MaxPostMilliseconds":@(maxPost),
                        @"ObservedDuringPath":observations,@"MovedDuringPath":@(movedDuringPath),@"ObserverSeconds":@(observerSeconds),
                        @"MaxDeadlineLatenessSeconds":@(maxLate),@"Expected":rect_json(expected),
                        @"Observed":rect_json(observed),@"GeometryVerified":@(matched)}];
                    fprintf(stderr,"Phase end cycle=%lu operation=%s pattern=%s actual=%.3f events=%lu verified=%d\n",
                        (unsigned long)cycle,selection?operation_name(self.operations):
                        resize?"resize":"drag",pattern.UTF8String,emittedDuration,(unsigned long)count,matched);
                    if(!matched) {
                        fprintf(stderr,"The window did not follow the real mouse %s. No successful benchmark is reported. Accessibility permission may be required for this executable/SSH.\n",resize?"resize":"drag");
                        self.result=3;valid=NO;break;
                    }
                    wait_until(now()+.15);
                }
            }
        }
        // Always release the synthetic button, including interrupted runs.
        CGEventRef location=CGEventCreate(NULL);mouse(kCGEventLeftMouseUp,CGEventGetLocation(location));CFRelease(location);
        BOOL interrupted=cancelled;cancelled=0;
        if(preparationAttempted&&found&&self.targetPID>0 && !selection) {
            position_window(self.targetWindow,corner,NO);
            position_window(self.targetWindow,userOriginal,YES);
            position_window(self.targetWindow,userOriginal,NO);
            mouse(kCGEventMouseMoved,userCursor);
        }
        if(selection)mouse(kCGEventMouseMoved,userCursor);
        CGRect final=CGRectZero;BOOL restored=bounds_for(self.targetWindow,&final)&&close_rect(final,userOriginal);
        if(interrupted)self.result=130;
        // Mavericks' CGDisplayPixelsWide/High can report logical dimensions
        // in HiDPI mode. Record the mode's explicit backing pixels instead.
        CGDisplayModeRef mode=CGDisplayCopyDisplayMode(CGMainDisplayID());
        size_t (*pixelWidthOfMode)(CGDisplayModeRef)=dlsym(RTLD_DEFAULT,"CGDisplayModeGetPixelWidth");
        size_t (*pixelHeightOfMode)(CGDisplayModeRef)=dlsym(RTLD_DEFAULT,"CGDisplayModeGetPixelHeight");
        // Lion lacks these later exports. AppKit's actual backing conversion
        // reports the native pixel geometry without mislabelling its logical
        // CGDisplayPixelsWide/High aliases as backing pixels.
        NSScreen *mainScreen=[NSScreen mainScreen];
        NSRect backing=[mainScreen convertRectToBacking:mainScreen.frame];
        size_t pixelWidth=mode&&pixelWidthOfMode?pixelWidthOfMode(mode):(size_t)backing.size.width;
        size_t pixelHeight=mode&&pixelHeightOfMode?pixelHeightOfMode(mode):(size_t)backing.size.height;
        if(mode)CGDisplayModeRelease(mode);
        NSDictionary *report=@{@"Schema":@2,@"TargetPID":@(self.targetPID),@"WindowID":@(self.targetWindow),
            @"Screen":rect_json(screen),@"DisplayPixelWidth":@(pixelWidth),
            @"DisplayPixelHeight":@(pixelHeight),
            @"GuestBuild":[[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"] objectForKey:@"ProductBuildVersion"]?:@"unknown",
            @"Seed":@(self.seed),@"EventColumns":@[@"Seconds",@"X",@"Y",@"PostMilliseconds"],
            @"SelectionRegion":rect_json(selectionBox),
            @"AccessibilityTrusted":@(trusted),@"Original":rect_json(userOriginal),@"WorkloadGeometry":rect_json(original),@"Final":rect_json(final),
            @"OriginalGeometryRestored":@(restored),@"InputRateHz":@(self.rate),@"Cycles":@(self.cycles),
            @"ElapsedSeconds":@(now()-started),@"Phases":phases,@"Success":@(valid&&!interrupted&&restored),
            @"Caution":selection?@"Selection input workload only: fixed window geometry is verified, not visible rubber-band contents, displayed FPS or input-to-photon latency.":
                @"Input/geometry workload only; does not measure displayed FPS or input-to-photon latency."};
        NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:NULL];
        fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);fflush(stdout);
        if(!restored&&!self.result)self.result=4;
        dispatch_async(dispatch_get_main_queue(),^{[NSApp terminate:nil];});
    }
}
@end

int main(int argc,char **argv) {
    @autoreleasepool {
        VIMInteractionRunner *runner=[VIMInteractionRunner new];runner.cycles=2;runner.seconds=2;runner.rate=120;runner.seed=7411;
        for(int i=1;i<argc;i++) {
            if(i+1>=argc){fprintf(stderr,"Usage: %s [--pid PID] [--window-id ID] [--cycles 1..20] [--seconds 1..10] [--hz 15..120] [--seed 1..4294967295] [--operations all|drag|resize|selection-finder|selection-desktop|selection-preview]\n",argv[0]);return 2;}
            const char *option=argv[i++];
            if(!strcmp(option,"--operations")) {
                if(!strcmp(argv[i],"all"))runner.operations=0;
                else if(!strcmp(argv[i],"drag"))runner.operations=1;
                else if(!strcmp(argv[i],"resize"))runner.operations=2;
                else if(!strcmp(argv[i],"selection-finder"))runner.operations=3;
                else if(!strcmp(argv[i],"selection-desktop"))runner.operations=4;
                else if(!strcmp(argv[i],"selection-preview"))runner.operations=5;
                else return 2;
                continue;
            }
            char *end=NULL;double value=strtod(argv[i],&end);
            if(!end||*end||!isfinite(value)||value<=0)return 2;
            if(!strcmp(option,"--pid")&&value<=INT_MAX&&floor(value)==value)runner.targetPID=(pid_t)value;
            else if(!strcmp(option,"--window-id")&&value<=UINT32_MAX&&floor(value)==value)runner.targetWindow=(CGWindowID)value;
            else if(!strcmp(option,"--cycles")&&value<=20&&floor(value)==value)runner.cycles=(NSUInteger)value;
            else if(!strcmp(option,"--seconds")&&value>=1&&value<=10)runner.seconds=value;
            else if(!strcmp(option,"--hz")&&value>=15&&value<=120)runner.rate=value;
            else if(!strcmp(option,"--seed")&&value<=UINT32_MAX&&floor(value)==value)runner.seed=(uint32_t)value;
            else return 2;
        }
        signal(SIGINT,cancel_run);signal(SIGTERM,cancel_run);
        [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp setDelegate:runner];[NSApp run];return runner.result;
    }
}
