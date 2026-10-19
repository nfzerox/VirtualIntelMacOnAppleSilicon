#import "VIMMetalView.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <IOKit/hidsystem/IOLLEvent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
@import ApplicationServices;

@interface VIMMetalView ()
@property id<MTLDevice> device;
@property id<MTLCommandQueue> queue;
@property id<MTLBuffer> frameBuffer;
@property CAMetalLayer *presentationLayer;
@property BOOL frameInFlight;
@property NSUInteger displayEpoch;
@property NSUInteger displayPowerUpdates;
@property NSUInteger presentationLayoutUpdates;
@property id<MTLTexture> lastPresentedTexture;
@property(nonatomic, copy) NSString *diagnosticsDirectory;
@property NSMutableSet<NSString *> *pressedKeys;
@property NSTrackingArea *mouseTracking;
@property NSCursor *guestCursor;
@property NSCursor *hiddenCursor;
@property NSUInteger cursorGlyphUpdates;
@property NSUInteger cursorVisibilityUpdates;
@property NSData *cursorPixels;
@property NSSize cursorSize;
@property NSPoint cursorHotspot;
@property CGFloat cursorPixelsPerPoint;
@property(nonatomic) BOOL guestCursorVisible;
@property(nonatomic) BOOL inputCaptured;
@property id keyMonitor;
@property id focusObserver;
@property CFMachPortRef eventTap;
@property CFRunLoopSourceRef eventTapSource;
@property CGFloat wheelRemainderX;
@property CGFloat wheelRemainderY;
- (void)routeCapturedKey:(NSEvent *)event;
@end

static CGEventRef VIMCapturedKey(CGEventTapProxy proxy,CGEventType type,CGEventRef event,void *context) {
    VIMMetalView *view=(__bridge VIMMetalView *)context;
    if(type==kCGEventTapDisabledByTimeout||type==kCGEventTapDisabledByUserInput){if(view.inputCaptured)CGEventTapEnable(view.eventTap,true);return event;}
    if(!view.inputCaptured||!view.window.isKeyWindow||view.window.firstResponder!=view)return event;
    [view routeCapturedKey:[NSEvent eventWithCGEvent:event]];
    return NULL;
}
static NSArray<NSArray *> *VIMModifierGroups(void) {
    static NSArray *groups;static dispatch_once_t once;
    dispatch_once(&once,^{groups=@[
        @[@(NSEventModifierFlagCommand),@"meta_l",@"meta_r",@(NX_DEVICELCMDKEYMASK),@(NX_DEVICERCMDKEYMASK),@55,@54],
        @[@(NSEventModifierFlagShift),@"shift",@"shift_r",@(NX_DEVICELSHIFTKEYMASK),@(NX_DEVICERSHIFTKEYMASK),@56,@60],
        @[@(NSEventModifierFlagControl),@"ctrl",@"ctrl_r",@(NX_DEVICELCTLKEYMASK),@(NX_DEVICERCTLKEYMASK),@59,@62],
        @[@(NSEventModifierFlagOption),@"alt",@"alt_r",@(NX_DEVICELALTKEYMASK),@(NX_DEVICERALTKEYMASK),@58,@61]];});
    return groups;
}

@implementation VIMMetalView
@synthesize displaySleeping=_displaySleeping;
@synthesize guestDisplaySleeping=_guestDisplaySleeping;
- (NSDictionary *)frameDiagnostics {
    NSAssert(NSThread.isMainThread,@"Frame geometry is main-thread owned");
    return @{@"PixelWidth":@(self.lastPresentedTexture.width),@"PixelHeight":@(self.lastPresentedTexture.height),
        @"ViewWidth":@(self.bounds.size.width),@"ViewHeight":@(self.bounds.size.height),
        @"BackingScale":@(self.window.backingScaleFactor),@"Sleeping":@(self.displaySleeping),
        @"GuestDisplaySleeping":@(self.guestDisplaySleeping),@"DisplayPowerUpdates":@(self.displayPowerUpdates),
        @"PresentationHidden":@(self.presentationLayer.hidden),@"LayoutUpdates":@(self.presentationLayoutUpdates),
        @"FrameStorageBytes":@(self.frameBuffer.length)};
}
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.diagnosticsDirectory=NSProcessInfo.processInfo.environment[@"VIM_FRAME_DIAGNOSTICS"];
        self.device = MTLCreateSystemDefaultDevice(); self.queue = [self.device newCommandQueue];
        self.pressedKeys = [NSMutableSet set];
        self.guestCursorVisible = YES;
        self.wantsLayer = YES; self.layer.backgroundColor = NSColor.blackColor.CGColor;
        self.presentationLayer = [CAMetalLayer layer];
        self.presentationLayer.device = self.device;
        self.presentationLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        self.presentationLayer.framebufferOnly = NO;
        self.presentationLayer.opaque = YES;
        self.presentationLayer.presentsWithTransaction = NO;
        self.presentationLayer.allowsNextDrawableTimeout = YES;
        [self.layer addSublayer:self.presentationLayer];
        __weak VIMMetalView *weakSelf=self;
        self.keyMonitor=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown|NSEventMaskKeyUp|NSEventMaskFlagsChanged handler:^NSEvent *(NSEvent *event){
            VIMMetalView *view=weakSelf;if(view.inputCaptured&&view.window.isKeyWindow&&view.window.firstResponder==view){[view routeCapturedKey:event];return nil;}return event;
        }];
        self.focusObserver=[NSNotificationCenter.defaultCenter addObserverForName:NSWindowDidResignKeyNotification object:nil queue:nil usingBlock:^(NSNotification *note){if(note.object==weakSelf.window)[weakSelf releaseInputCapture];}];
    }
    return self;
}
- (void)dealloc {
    if(self.keyMonitor)[NSEvent removeMonitor:self.keyMonitor];
    if(self.focusObserver)[NSNotificationCenter.defaultCenter removeObserver:self.focusObserver];
    if(_eventTapSource){CFRunLoopRemoveSource(CFRunLoopGetMain(),_eventTapSource,kCFRunLoopCommonModes);CFRelease(_eventTapSource);}
    if(_eventTap){CFMachPortInvalidate(_eventTap);CFRelease(_eventTap);}
}
- (void)captureInput {
    self.inputCaptured=YES;
    if(!self.eventTap) {
        CGEventMask mask=CGEventMaskBit(kCGEventKeyDown)|CGEventMaskBit(kCGEventKeyUp)|CGEventMaskBit(kCGEventFlagsChanged);
        self.eventTap=CGEventTapCreate(kCGSessionEventTap,kCGHeadInsertEventTap,kCGEventTapOptionDefault,mask,VIMCapturedKey,(__bridge void *)self);
        if(self.eventTap){self.eventTapSource=CFMachPortCreateRunLoopSource(NULL,self.eventTap,0);CFRunLoopAddSource(CFRunLoopGetMain(),self.eventTapSource,kCFRunLoopCommonModes);}
        else {
            // Stock macOS requires user-approved Accessibility access to route
            // system-reserved shortcuts such as Cmd-Tab and Cmd-Space. Ask only
            // once; without it ordinary typing still works, and the tap is
            // created automatically on a later capture once access is granted.
            NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
            if(![defaults boolForKey:@"VIMAccessibilityPrompted"]) {
                [defaults setBool:YES forKey:@"VIMAccessibilityPrompted"];
                AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)@{(__bridge NSString *)kAXTrustedCheckOptionPrompt:@YES});
            }
        }
    }
    if(self.eventTap)CGEventTapEnable(self.eventTap,true);
    [self refreshNativeCursor];
}
- (void)releaseInputCapture {
    self.inputCaptured=NO;[self releasePressedKeys];
    self.wheelRemainderX=0;self.wheelRemainderY=0;
    if(self.eventTap)CGEventTapEnable(self.eventTap,false);
    [self refreshNativeCursor];
}
- (void)routeCapturedKey:(NSEvent *)event {
    if((event.modifierFlags&(NSEventModifierFlagControl|NSEventModifierFlagOption))==(NSEventModifierFlagControl|NSEventModifierFlagOption)) {
        [self releaseInputCapture];
        // Releasing only the tap leaves AppKit's first responder forwarding
        // Command equivalents (and later key-ups) straight back to the guest.
        if(self.window.firstResponder==self)[self.window makeFirstResponder:nil];
        return;
    }
    if(event.type==NSEventTypeKeyDown)[self keyDown:event];
    else if(event.type==NSEventTypeKeyUp)[self keyUp:event];
    else if(event.type==NSEventTypeFlagsChanged)[self flagsChanged:event];
}
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)resignFirstResponder {
    BOOL resigned=[super resignFirstResponder];
    if(resigned&&(self.inputCaptured||self.pressedKeys.count))[self releaseInputCapture];
    return resigned;
}
- (void)capturePNG:(void (^)(NSData *))completion {
    id<MTLTexture> texture=self.lastPresentedTexture;
    if(!texture||self.displaySleeping){completion(nil);return;}
    NSUInteger stride=(texture.width*4+255)&~255UL,length=stride*texture.height;
    id<MTLBuffer> buffer=[self.device newBufferWithLength:length options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command=[self.queue commandBuffer];id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
    if(!buffer||!blit){completion(nil);return;}
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(texture.width,texture.height,1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:stride destinationBytesPerImage:length];[blit endEncoding];
    [command addCompletedHandler:^(id<MTLCommandBuffer> finished){
        NSData *png=nil;
        if(finished.status==MTLCommandBufferStatusCompleted) {
            CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();CGContextRef context=CGBitmapContextCreate(buffer.contents,texture.width,texture.height,8,stride,color,kCGBitmapByteOrder32Little|(CGBitmapInfo)kCGImageAlphaNoneSkipFirst);CGColorSpaceRelease(color);
            if(context){CGImageRef cg=CGBitmapContextCreateImage(context);NSBitmapImageRep *image=[[NSBitmapImageRep alloc] initWithCGImage:cg];png=[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}];CGImageRelease(cg);CGContextRelease(context);}
        }
        dispatch_async(dispatch_get_main_queue(),^{completion(png);});
    }];[command commit];
}
- (BOOL)displaySleeping {return _displaySleeping||_guestDisplaySleeping;}
- (void)retireSleepingFrame {
    if(self.displaySleeping) {
        self.displayEpoch++;
        self.lastPresentedTexture=nil;
        self.presentationLayer.hidden=YES;
    }
    // A running CPU is not proof that the guest display has resumed. Keep the
    // layer black until a new frame completes; never expose the pre-sleep image.
}
- (void)setDisplaySleeping:(BOOL)value {
    if(_displaySleeping==value)return;
    _displaySleeping=value;[self retireSleepingFrame];
}
- (void)setGuestDisplaySleeping:(BOOL)value {
    if(_guestDisplaySleeping==value)return;
    _guestDisplaySleeping=value;self.displayPowerUpdates++;[self retireSleepingFrame];
}
- (void)discardFrameStorage {
    NSAssert(NSThread.isMainThread,@"Presentation is main-thread owned");
    self.displaySleeping=YES;
    self.displayEpoch++;self.lastPresentedTexture=nil;
    self.presentationLayer.hidden=YES;self.frameBuffer=nil;
}
- (void)setSoftwareCursor:(BOOL)value {
    _softwareCursor=value;
    [self refreshNativeCursor];
}
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (void)setGuestCursorVisible:(BOOL)visible {
    self.cursorVisibilityUpdates++;_guestCursorVisible=visible;[self refreshNativeCursor];
}
- (void)setGuestCursorPixels:(NSData *)pixels width:(NSUInteger)width height:(NSUInteger)height hotspot:(NSPoint)hotspot {
    if(!width||!height||width>256||height>256||pixels.length!=width*height*4||!isfinite(hotspot.x)||!isfinite(hotspot.y)||hotspot.x<0||hotspot.y<0||hotspot.x>=width||hotspot.y>=height)return;
    self.cursorGlyphUpdates++;
    self.cursorPixels=pixels;self.cursorSize=NSMakeSize(width,height);self.cursorHotspot=hotspot;
    self.cursorPixelsPerPoint=0;[self updateGuestCursorScale];
}
- (void)updateGuestCursorScale {
    if(!self.cursorPixels)return;
    CGFloat scale=self.presentationLayer.frame.size.width>0&&self.presentationLayer.drawableSize.width>0?
        self.presentationLayer.drawableSize.width/self.presentationLayer.frame.size.width:1;
    if(fabs(scale-self.cursorPixelsPerPoint)<.001)return;
    self.cursorPixelsPerPoint=scale;
    NSUInteger width=self.cursorSize.width,height=self.cursorSize.height;NSPoint hotspot=self.cursorHotspot;
    NSData *pixels=self.cursorPixels;
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)pixels);
    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
    CGImageRef cg=CGImageCreate(width,height,8,32,width*4,color,
        kCGBitmapByteOrder32Little|(CGBitmapInfo)kCGImageAlphaPremultipliedFirst,provider,NULL,NO,kCGRenderingIntentDefault);
    if(cg) {
        NSImage *image=[[NSImage alloc] initWithCGImage:cg size:NSMakeSize(width/scale,height/scale)];
        self.guestCursor=[[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(hotspot.x/scale,hotspot.y/scale)];
        CGImageRelease(cg);[self refreshNativeCursor];
    }
    CGColorSpaceRelease(color);CGDataProviderRelease(provider);
}
- (NSCursor *)effectiveCursor {
    NSCursor *cursor=self.guestCursor?:NSCursor.arrowCursor;
    if(self.inputCaptured&&(self.softwareCursor||!self.guestCursorVisible)) {
        if(!self.hiddenCursor)self.hiddenCursor=[[NSCursor alloc] initWithImage:[[NSImage alloc] initWithSize:NSMakeSize(1,1)] hotSpot:NSZeroPoint];
        cursor=self.hiddenCursor;
    }return cursor;
}
- (BOOL)pointerInside {
    return self.window&&NSMouseInRect([self convertPoint:self.window.mouseLocationOutsideOfEventStream fromView:nil],self.bounds,self.isFlipped);
}
- (void)refreshNativeCursor {
    [self.window invalidateCursorRectsForView:self];
    // Rect invalidation alone can leave the previous (including blank) cursor
    // installed while the pointer remains over this view. Apply state changes
    // now, but never override a menu/toolbar cursor or a background window.
    if(self.window.isKeyWindow&&!self.hiddenOrHasHiddenAncestor&&[self pointerInside])[[self effectiveCursor] set];
}
- (void)cursorUpdate:(NSEvent *)event {[[self effectiveCursor] set];}
- (void)resetCursorRects {
    [self addCursorRect:self.bounds cursor:[self effectiveCursor]];
}
- (NSDictionary *)cursorDiagnostics {
    NSUInteger visible=0;const uint8_t *pixels=self.cursorPixels.bytes;
    for(NSUInteger i=3;i<self.cursorPixels.length;i+=4)if(pixels[i])visible++;
    return @{@"GuestVisible":@(self.guestCursorVisible),@"SoftwareCursor":@(self.softwareCursor),@"InputCaptured":@(self.inputCaptured),
        @"WindowKey":@(self.window.isKeyWindow),@"PointerInside":@([self pointerInside]),@"HasGlyph":@(self.guestCursor!=nil),
        @"GlyphUpdates":@(self.cursorGlyphUpdates),@"VisibilityUpdates":@(self.cursorVisibilityUpdates),
        @"NontransparentPixels":@(visible),@"GlyphWidth":@(self.cursorSize.width),@"GlyphHeight":@(self.cursorSize.height),
        @"PixelsPerPoint":@(self.cursorPixelsPerPoint),@"EffectiveCursorHidden":@([self effectiveCursor]==self.hiddenCursor)};
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if(self.mouseTracking)[self removeTrackingArea:self.mouseTracking];
    self.mouseTracking=[[NSTrackingArea alloc] initWithRect:NSZeroRect
        options:NSTrackingMouseMoved|NSTrackingCursorUpdate|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:self.mouseTracking];
}
- (void)emit:(NSArray *)events { if(self.inputHandler)self.inputHandler(events); }
- (void)sendKey:(NSString *)key down:(BOOL)down {
    if([self.pressedKeys containsObject:key]==down)return;
    [self emit:@[@{@"type":@"key",@"data":@{@"down":@(down),@"key":@{@"type":@"qcode",@"data":key}}}]];
    if(down)[self.pressedKeys addObject:key];else [self.pressedKeys removeObject:key];
}
- (NSString *)keyName:(unsigned short)code {
    static NSDictionary *keys;static dispatch_once_t once;
    dispatch_once(&once, ^{keys=@{@0:@"a",@1:@"s",@2:@"d",@3:@"f",@4:@"h",@5:@"g",@6:@"z",@7:@"x",@8:@"c",@9:@"v",@11:@"b",
        @12:@"q",@13:@"w",@14:@"e",@15:@"r",@16:@"y",@17:@"t",@18:@"1",@19:@"2",@20:@"3",@21:@"4",@22:@"6",@23:@"5",
        @24:@"equal",@25:@"9",@26:@"7",@27:@"minus",@28:@"8",@29:@"0",@30:@"bracket_right",@31:@"o",@32:@"u",@33:@"bracket_left",
        @34:@"i",@35:@"p",@36:@"ret",@37:@"l",@38:@"j",@39:@"apostrophe",@40:@"k",@41:@"semicolon",@42:@"backslash",@43:@"comma",
        @44:@"slash",@45:@"n",@46:@"m",@47:@"dot",@48:@"tab",@49:@"spc",@50:@"grave_accent",@51:@"backspace",@53:@"esc",
        @54:@"meta_r",@55:@"meta_l",@56:@"shift",@58:@"alt",@59:@"ctrl",@60:@"shift_r",@61:@"alt_r",@62:@"ctrl_r",
        @65:@"kp_decimal",@67:@"kp_multiply",@69:@"kp_add",@75:@"kp_divide",@76:@"kp_enter",@78:@"kp_subtract",@82:@"kp_0",
        @83:@"kp_1",@84:@"kp_2",@85:@"kp_3",@86:@"kp_4",@87:@"kp_5",@88:@"kp_6",@89:@"kp_7",@91:@"kp_8",@92:@"kp_9",
        @96:@"f5",@97:@"f6",@98:@"f7",@99:@"f3",@100:@"f8",@101:@"f9",@103:@"f11",@109:@"f10",@111:@"f12",
        @115:@"home",@116:@"pgup",@117:@"delete",@118:@"f4",@119:@"end",@120:@"f2",@121:@"pgdn",@122:@"f1",@123:@"left",@124:@"right",@125:@"down",@126:@"up"};});
    return keys[@(code)];
}
- (void)synchronizeModifiers:(NSEvent *)event {
    // A key equivalent or focus transition can arrive without flagsChanged.
    // Reconcile aggregate state without discarding a known right-hand modifier.
    for(NSArray *group in VIMModifierGroups()) {
        BOOL down=(event.modifierFlags & [group[0] unsignedIntegerValue])!=0;
        BOOL left=[self.pressedKeys containsObject:group[1]],right=[self.pressedKeys containsObject:group[2]];
        NSUInteger leftMask=[group[3] unsignedIntegerValue],rightMask=[group[4] unsignedIntegerValue];
        if(down&&(event.modifierFlags&(leftMask|rightMask))) {
            [self sendKey:group[1] down:(event.modifierFlags&leftMask)!=0];
            [self sendKey:group[2] down:(event.modifierFlags&rightMask)!=0];
            continue;
        }
        if(down&&!left&&!right)[self sendKey:group[1] down:YES];
        if(!down) {
            if(left)[self sendKey:group[1] down:NO];
            if(right)[self sendKey:group[2] down:NO];
        }
    }
}
- (void)keyDown:(NSEvent *)event {
    // Typing into a newly focused guest also counts as entering it. After the
    // release shortcut the view no longer owns first responder, so typing is
    // not silently recaptured until the user clicks back into the guest.
    if(!self.inputCaptured&&self.window.isKeyWindow&&self.window.firstResponder==self)[self captureInput];
    if(!self.inputCaptured){[super keyDown:event];return;}
    [self synchronizeModifiers:event];NSString *key=[self keyName:event.keyCode];if(key&&!event.isARepeat)[self sendKey:key down:YES];
}
- (void)keyUp:(NSEvent *)event { if(!self.inputCaptured)return;[self synchronizeModifiers:event];NSString *key=[self keyName:event.keyCode];if(key)[self sendKey:key down:NO]; }
- (BOOL)performKeyEquivalent:(NSEvent *)event {
    if(self.inputCaptured && self.window.firstResponder==self && (event.modifierFlags & NSEventModifierFlagCommand)) {
        // AppKit does not reliably deliver keyUp for consumed command equivalents.
        [self synchronizeModifiers:event];
        NSString *key=[self keyName:event.keyCode];
        if(key){[self sendKey:key down:YES];[self sendKey:key down:NO];}
        return YES;
    }
    return [super performKeyEquivalent:event];
}
- (void)flagsChanged:(NSEvent *)event {
    if(!self.inputCaptured)return;
    BOOL hadCommand=[self.pressedKeys containsObject:@"meta_l"]||[self.pressedKeys containsObject:@"meta_r"];
    if(event.keyCode==57) { [self sendKey:@"caps_lock" down:YES];[self sendKey:@"caps_lock" down:NO];return; }
    // Synthetic/focus modifier notifications can have keyCode 0. It is not an
    // A key transition: only real modifier keycodes may be toggled here.
    for(NSArray *group in VIMModifierGroups()) {
        if(event.keyCode!=[group[5] unsignedShortValue]&&event.keyCode!=[group[6] unsignedShortValue])continue;
        // Hardware events carry per-side bits. Synthetic aggregate-only events
        // retain the physical-keycode fallback, but a cleared aggregate is
        // always an UP, never a toggle which can manufacture a held modifier.
        if(!(event.modifierFlags&([group[3] unsignedIntegerValue]|[group[4] unsignedIntegerValue]))) {
            NSString *key=[self keyName:event.keyCode];
            [self sendKey:key down:(event.modifierFlags&[group[0] unsignedIntegerValue])&&![self.pressedKeys containsObject:key]];
        }
        break;
    }
    [self synchronizeModifiers:event];
    if(hadCommand&&!(event.modifierFlags&NSEventModifierFlagCommand)) {
        // macOS can consume a command shortcut's key-up. Do not leave its
        // ordinary key held when the final Command modifier is released.
        NSSet *modifiers=[NSSet setWithArray:@[@"meta_l",@"meta_r",@"shift",@"shift_r",@"ctrl",@"ctrl_r",@"alt",@"alt_r"]];
        for(NSString *key in self.pressedKeys.allObjects)if(![modifiers containsObject:key])[self sendKey:key down:NO];
    }
}
- (void)releasePressedKeys {
    NSMutableArray *events=[NSMutableArray array];
    for(NSString *key in self.pressedKeys)
        [events addObject:@{@"type":@"key",@"data":@{@"down":@NO,@"key":@{@"type":@"qcode",@"data":key}}}];
    [self.pressedKeys removeAllObjects];
    for(NSString *button in @[@"left",@"right",@"middle"])
        [events addObject:@{@"type":@"btn",@"data":@{@"button":button,@"down":@NO}}];
    // One ordered request, rather than a separate round trip for every held
    // key/button when a menu opens or capture/focus is released.
    [self emit:events];
}
- (void)mouseMoved:(NSEvent *)event {
    NSPoint p=[self convertPoint:event.locationInWindow fromView:nil];CGRect frame=self.presentationLayer.frame;
    if(frame.size.width<=0||frame.size.height<=0)return;
    int x=(int)(MAX(0,MIN(1,(p.x-frame.origin.x)/frame.size.width))*32767);
    int y=(int)(MAX(0,MIN(1,1-(p.y-frame.origin.y)/frame.size.height))*32767);
    [self emit:@[@{@"type":@"abs",@"data":@{@"axis":@"x",@"value":@(x)}},
                 @{@"type":@"abs",@"data":@{@"axis":@"y",@"value":@(y)}}]];
}
- (void)button:(NSString *)button event:(NSEvent *)event down:(BOOL)down {
    [self.window makeFirstResponder:self];if(down&&!self.inputCaptured)[self captureInput];[self mouseMoved:event];
    [self emit:@[@{@"type":@"btn",@"data":@{@"button":button,@"down":@(down)}}]];
}
- (void)mouseDown:(NSEvent *)e { [self button:@"left" event:e down:YES]; }
- (void)mouseUp:(NSEvent *)e { [self button:@"left" event:e down:NO]; }
- (void)rightMouseDown:(NSEvent *)e { [self button:@"right" event:e down:YES]; }
- (void)rightMouseUp:(NSEvent *)e { [self button:@"right" event:e down:NO]; }
- (void)otherMouseDown:(NSEvent *)e { [self button:@"middle" event:e down:YES]; }
- (void)otherMouseUp:(NSEvent *)e { [self button:@"middle" event:e down:NO]; }
- (void)mouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)rightMouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)otherMouseDragged:(NSEvent *)e { [self mouseMoved:e]; }
- (void)scrollWheel:(NSEvent *)e {
    if(e.phase==NSEventPhaseCancelled){self.wheelRemainderX=0;self.wheelRemainderY=0;return;}
    // Precise scrollingDelta values are points, not wheel detents. AppKit's
    // legacy delta representation supplies its device-normalized wheel scale
    // without assuming a fixed points-per-line setting. Conserve fractions:
    // rounding every tiny Magic Mouse event UP amplifies motion and jitter.
    CGFloat x=e.hasPreciseScrollingDeltas?e.deltaX:e.scrollingDeltaX;
    CGFloat y=e.hasPreciseScrollingDeltas?e.deltaY:e.scrollingDeltaY;
    if(!isfinite(x)||!isfinite(y))return;
    if(e.phase==NSEventPhaseBegan){self.wheelRemainderX=0;self.wheelRemainderY=0;}
    // Send physical direction exactly once; macOS in the guest applies its
    // own natural-scrolling preference. Include both axes for diagonal motion.
    CGFloat direction=e.isDirectionInvertedFromDevice?-1:1;
    CGFloat totals[2]={self.wheelRemainderX+x*direction,self.wheelRemainderY+y*direction};
    NSMutableArray *events=[NSMutableArray array];
    for(NSUInteger axis=0;axis<2;axis++) {
        // Bound anomalous/accelerated inputs to one <=64-event request. Don't
        // retain a large backlog that would scroll after the gesture ends.
        CGFloat total=MAX(-16,MIN(16,totals[axis]));
        NSUInteger count=(NSUInteger)floor(fabs(total)+1e-9);
        NSString *button=axis==0?(total>0?@"wheel-left":@"wheel-right"):
            (total>0?@"wheel-up":@"wheel-down");
        for(NSUInteger i=0;i<count;i++) {
            [events addObject:@{@"type":@"btn",@"data":@{@"button":button,@"down":@YES}}];
            [events addObject:@{@"type":@"btn",@"data":@{@"button":button,@"down":@NO}}];
        }
        totals[axis]=total-copysign((CGFloat)count,total);
        if(fabs(totals[axis])<1e-9)totals[axis]=0;
    }
    self.wheelRemainderX=totals[0];self.wheelRemainderY=totals[1];
    if(events.count)[self emit:events];
}
- (BOOL)installFrameDescriptor:(int)fd length:(NSUInteger)length error:(NSError **)error {
    NSAssert(NSThread.isMainThread, @"Metal view state is main-thread owned");
    struct stat st;
    BOOL valid = fd >= 0 && !fstat(fd, &st) && st.st_size >= 0 &&
        length == 8192ULL * 8192 * 4 && (uint64_t)st.st_size == length && !self.frameInFlight;
    void *mapping = valid ? mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    if (fd >= 0) close(fd);
    if (mapping == MAP_FAILED || !self.device || !self.queue) {
        if (mapping != MAP_FAILED) munmap(mapping, length);
        if (error) *error = [NSError errorWithDomain:@"VirtualIntelMac.Display" code:1
            userInfo:@{NSLocalizedDescriptionKey:@"The shared graphics surface could not be opened."}];
        return NO;
    }
    id<MTLBuffer> buffer = [self.device newBufferWithBytesNoCopy:mapping length:length
        options:MTLResourceStorageModeShared deallocator:^(void *pointer, NSUInteger size) {
            munmap(pointer, size);
        }];
    if (!buffer) {
        munmap(mapping, length);
        if (error) *error = [NSError errorWithDomain:@"VirtualIntelMac.Display" code:2
            userInfo:@{NSLocalizedDescriptionKey:@"Metal could not import the shared graphics memory."}];
        return NO;
    }
    self.frameBuffer = buffer; return YES;
}
- (void)layout {
    [super layout];
    CGSize pixels = self.presentationLayer.drawableSize;
    CGRect bounds = self.bounds, content = bounds;
    if (pixels.width > 0 && pixels.height > 0) {
        CGFloat scale = MIN(bounds.size.width / pixels.width, bounds.size.height / pixels.height);
        content.size = CGSizeMake(pixels.width * scale, pixels.height * scale);
        content.origin = CGPointMake(CGRectGetMidX(bounds) - content.size.width / 2,
                                    CGRectGetMidY(bounds) - content.size.height / 2);
    }
    if(!CGRectEqualToRect(self.presentationLayer.frame,content)) {
        [CATransaction begin]; [CATransaction setDisableActions:YES];
        self.presentationLayer.frame = content;self.presentationLayoutUpdates++;
        [CATransaction commit];
    }
    [self updateGuestCursorScale];
}
- (void)presentWidth:(NSUInteger)width height:(NSUInteger)height stride:(NSUInteger)stride
    completion:(void (^)(BOOL))completion {
    NSAssert(NSThread.isMainThread, @"Presentation is main-thread owned");
    if (self.displaySleeping || !self.frameBuffer || self.frameInFlight || width < 320 || height < 200 ||
        width > 8192 || height > 8192 || stride < width * 4 || stride % 256 ||
        stride > self.frameBuffer.length / height) { completion(NO); return; }
    if (!self.window || !self.window.visible || self.window.miniaturized) { completion(NO); return; }
    CGSize drawableSize=CGSizeMake(width,height);
    if(!CGSizeEqualToSize(self.presentationLayer.drawableSize,drawableSize))self.presentationLayer.drawableSize=drawableSize;
    [self layout];
    id<CAMetalDrawable> drawable = [self.presentationLayer nextDrawable];
    id<MTLCommandBuffer> command = [self.queue commandBuffer];
    if (!drawable || !command) { completion(NO); return; }
    // Obtain/retain the texture before presentation. CAMetalDrawable forbids
    // querying texture after present; the completion handler needs this same
    // resource, not a new drawable or an additional copy.
    id<MTLTexture> presentedTexture=drawable.texture;
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    if (!blit) { completion(NO); return; }
    // Same GPU copy required by the command-line renderer; no CPU readback,
    // new pixel allocation, network frame encoding or intermediate texture.
    [blit copyFromBuffer:self.frameBuffer sourceOffset:0 sourceBytesPerRow:stride
        sourceBytesPerImage:stride * height sourceSize:MTLSizeMake(width, height, 1)
        toTexture:presentedTexture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding]; [command presentDrawable:drawable]; self.frameInFlight = YES;
    NSUInteger epoch=self.displayEpoch;
    [command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.frameInFlight = NO;
            BOOL current=finished.status==MTLCommandBufferStatusCompleted&&epoch==self.displayEpoch&&!self.displaySleeping;
            if(current){self.lastPresentedTexture=presentedTexture;self.presentationLayer.hidden=NO;}
            // Explicit developer diagnostic only. Snapshot our own shared frame
            // after successful GPU presentation, before returning its ownership.
            // No capture or filesystem work occurs in normal app launches.
            NSString *directory=self.diagnosticsDirectory;
            if(directory.length && current) {
                NSString *request=[directory stringByAppendingPathComponent:@"capture.request"];
                if([NSFileManager.defaultManager fileExistsAtPath:request]) {
                    CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
                    CGContextRef context=CGBitmapContextCreate(self.frameBuffer.contents,width,height,8,stride,color,
                        kCGBitmapByteOrder32Little|(CGBitmapInfo)kCGImageAlphaPremultipliedFirst);
                    CGColorSpaceRelease(color);
                    if(context) {
                        CGImageRef image=CGBitmapContextCreateImage(context);
                        NSBitmapImageRep *rep=[[NSBitmapImageRep alloc] initWithCGImage:image];
                        NSData *png=[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                        NSError *captureError=nil;
                        if([png writeToFile:[directory stringByAppendingPathComponent:@"frame.png"] options:NSDataWritingAtomic error:&captureError]) {
                            [@{@"Width":@(width),@"Height":@(height),@"PresentedAt":NSDate.date,
                               @"MetalCompleted":@YES} writeToFile:[directory stringByAppendingPathComponent:@"frame.plist"] atomically:YES];
                            [NSFileManager.defaultManager removeItemAtPath:request error:NULL];
                        }
                        CGImageRelease(image);CGContextRelease(context);
                    }
                }
            }
            completion(current);
        });
    }];
    [command commit];
}
@end
