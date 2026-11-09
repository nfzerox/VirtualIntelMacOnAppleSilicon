#import "VIMMetalView.h"
#import "VIMDisplayFingerprint.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <IOKit/hidsystem/IOLLEvent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
@import ApplicationServices;

// Both presentation and GPU completion resolve on the main queue. Neither
// callback alone establishes that these particular pixels were displayed.
@interface VIMPresentedFingerprint : NSObject
@property BOOL gpuReady, presentationReady, valid, consumed;
@property uint64_t fingerprint;
@property uint64_t regionFingerprint;
@property CFTimeInterval presentedAt;
@property CFTimeInterval offeredAt, submittedAt, completionObservedAt;
@property BOOL directSurface;
@property NSUInteger epoch, generation, width, height;
@property uint32_t marker;
@end
@implementation VIMPresentedFingerprint
@end

@interface VIMMetalView ()
@property id<MTLDevice> device;
@property id<MTLCommandQueue> queue;
@property id<MTLBuffer> frameBuffer;
@property CAMetalLayer *presentationLayer;
@property BOOL frameInFlight;
@property(nonatomic,strong) dispatch_queue_t drawableAcquisitionQueue;
@property NSUInteger coalescedBeforeEncoding;
// One latest immutable GPU source, held through the caller's completion
// capability. Busy consumers may coalesce intermediate versions, never lose
// the final version merely because the preceding blit has not completed.
@property(nonatomic) id<MTLTexture> pendingSurfaceTexture;
@property(nonatomic) NSUInteger pendingSurfaceWidth,pendingSurfaceHeight,pendingSurfaceEpoch;
@property CFTimeInterval pendingSurfaceOfferedAt;
@property(nonatomic,copy) void (^pendingSurfaceCompletion)(BOOL);
@property NSUInteger displayEpoch;
@property NSUInteger displayPowerUpdates;
@property NSUInteger presentationLayoutUpdates;
@property NSUInteger completedFrames;
@property CFTimeInterval lastFrameCompletedAt;
// Opt-in pacing diagnosis, independent of GPU-completion/ownership ACKs.
@property BOOL measurePresentation;
@property NSUInteger presentedFrames;
@property NSUInteger skippedPresentations;
@property NSUInteger reorderedPresentations;
@property CFTimeInterval firstPresentedAt;
@property CFTimeInterval lastPresentedAt;
@property double nextDrawableSeconds;
@property double nextDrawableMaxSeconds;
@property BOOL measureFingerprint;
@property id<MTLComputePipelineState> fingerprintPipeline;
@property BOOL fingerprintSingleSIMD;
@property id<MTLBuffer> fingerprintBuffer;
@property NSString *fingerprintError;
@property NSUInteger fingerprintFrames, distinctFrames, duplicateFrames, fingerprintFailures, fingerprintReordered;
@property NSUInteger fingerprintGeneration, fingerprintUnpresented;
@property NSMutableArray *fingerprintTrace;
@property NSMutableArray *fingerprintPipelineTrace;
@property NSMutableArray *fingerprintUnpresentedTrace;
@property NSUInteger fingerprintTraceDropped;
@property(nonatomic) NSRect fingerprintMarkerRect;
@property NSUInteger fingerprintEpoch, fingerprintWidth, fingerprintHeight;
@property uint64_t lastFingerprint;
@property CFTimeInterval lastFingerprintAt, lastDistinctAt;
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
@property(nonatomic) BOOL ownsResponderFocus;
@property id keyMonitor;
@property id focusObserver;
@property CFMachPortRef eventTap;
@property CFRunLoopSourceRef eventTapSource;
@property CGFloat wheelRemainderX;
@property CGFloat wheelRemainderY;
- (void)routeCapturedKey:(NSEvent *)event;
- (void)cancelPendingSurface;
- (void)drainPendingSurface;
@end

// Main-thread-owned, fixed-size counters: no image hashing/capture or per-frame
// logging. Bucket N covers [N,N+1) ms; 255 also contains all longer intervals.
typedef struct {
    uint64_t gaps[256];
    uint64_t submissionDelays[256];
    uint64_t gpuQueueDelays[256],gpuDurations[256],gpuCompletionDelays[256];
    uint64_t gpuTimed,gpuUnavailable;
    double gpuQueueSeconds,gpuWorkSeconds;
    uint64_t cpuCompletionDelays[256],cpuCompleted;
    double cpuCompletionSeconds;
} VIMPresentationTiming;

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

@implementation VIMMetalView {
    VIMPresentationTiming _presentationTiming;
    uint64_t _distinctGaps[256];
}
@synthesize displaySleeping=_displaySleeping;
@synthesize guestDisplaySleeping=_guestDisplaySleeping;
- (NSDictionary *)frameDiagnostics {
    NSAssert(NSThread.isMainThread,@"Frame geometry is main-thread owned");
    NSMutableDictionary *result=[@{@"PixelWidth":@(self.lastPresentedTexture.width),@"PixelHeight":@(self.lastPresentedTexture.height),
        @"ViewWidth":@(self.bounds.size.width),@"ViewHeight":@(self.bounds.size.height),
        @"BackingScale":@(self.window.backingScaleFactor),@"Sleeping":@(self.displaySleeping),
        @"GuestDisplaySleeping":@(self.guestDisplaySleeping),@"DisplayPowerUpdates":@(self.displayPowerUpdates),
        @"ApplicationActive":@(NSApp.active),@"WindowKey":@(self.window.keyWindow),
        @"WindowOcclusionVisible":@((self.window.occlusionState&NSWindowOcclusionStateVisible)!=0),
        @"PresentationHidden":@(self.presentationLayer.hidden),@"LayoutUpdates":@(self.presentationLayoutUpdates),
        @"CompletedFrames":@(self.completedFrames),@"LastCompletionTime":@(self.lastFrameCompletedAt),
        @"FrameStorageBytes":@(self.frameBuffer.length),
        @"MaximumDrawableCount":@(self.presentationLayer.maximumDrawableCount),
        @"ScalingFilter":self.presentationLayer.magnificationFilter?:@"",
        @"AsynchronousDrawableAcquisition":@(self.asynchronousDrawableAcquisition),
        @"CoalescedBeforeEncoding":@(self.coalescedBeforeEncoding)} mutableCopy];
    if(self.measurePresentation) {
        NSMutableArray *gaps=[NSMutableArray arrayWithCapacity:256];
        NSMutableArray *delays=[NSMutableArray arrayWithCapacity:256];
        NSMutableArray *gpuQueue=[NSMutableArray arrayWithCapacity:256];
        NSMutableArray *gpuWork=[NSMutableArray arrayWithCapacity:256];
        NSMutableArray *gpuCompletion=[NSMutableArray arrayWithCapacity:256];
        NSMutableArray *cpuCompletion=[NSMutableArray arrayWithCapacity:256];
        for(NSUInteger i=0;i<256;i++) {
            [gaps addObject:@(_presentationTiming.gaps[i])];
            [delays addObject:@(_presentationTiming.submissionDelays[i])];
            [gpuQueue addObject:@(_presentationTiming.gpuQueueDelays[i])];
            [gpuWork addObject:@(_presentationTiming.gpuDurations[i])];
            [gpuCompletion addObject:@(_presentationTiming.gpuCompletionDelays[i])];
            [cpuCompletion addObject:@(_presentationTiming.cpuCompletionDelays[i])];
        }
        result[@"PresentationTiming"]=@{@"PresentedFrames":@(self.presentedFrames),
            @"Skipped":@(self.skippedPresentations),@"ReorderedCallbacks":@(self.reorderedPresentations),
            @"FirstPresentedAt":@(self.firstPresentedAt),@"LastPresentedAt":@(self.lastPresentedAt),
            @"GapMillisecondsHistogram":gaps,@"SubmissionMillisecondsHistogram":delays,
            @"GPUCommandBuffersTimed":@(_presentationTiming.gpuTimed),
            @"GPUCommandBuffersUnavailable":@(_presentationTiming.gpuUnavailable),
            @"SubmissionToGPUStartMillisecondsHistogram":gpuQueue,
            @"GPUExecutionMillisecondsHistogram":gpuWork,
            @"SubmissionToGPUEndMillisecondsHistogram":gpuCompletion,
            @"SubmissionToGPUStartSeconds":@(_presentationTiming.gpuQueueSeconds),
            @"GPUExecutionSeconds":@(_presentationTiming.gpuWorkSeconds),
            @"CPUCommandBuffersCompleted":@(_presentationTiming.cpuCompleted),
            @"SubmissionToCPUCompletionMillisecondsHistogram":cpuCompletion,
            @"SubmissionToCPUCompletionSeconds":@(_presentationTiming.cpuCompletionSeconds),
            @"NextDrawableSeconds":@(self.nextDrawableSeconds),@"NextDrawableMaxSeconds":@(self.nextDrawableMaxSeconds)};
    }
    if(self.measureFingerprint || self.fingerprintFrames || self.fingerprintError.length) {
        NSMutableArray *gaps=[NSMutableArray arrayWithCapacity:256];
        for(NSUInteger i=0;i<256;i++)[gaps addObject:@(_distinctGaps[i])];
        result[@"DistinctPresentationTiming"]=@{@"Enabled":@(self.measureFingerprint),
            @"Generation":@(self.fingerprintGeneration),@"Unpresented":@(self.fingerprintUnpresented),
            @"PresentedFingerprintFrames":@(self.fingerprintFrames),@"DistinctFrames":@(self.distinctFrames),
            @"DuplicateFrames":@(self.duplicateFrames),@"Failures":@(self.fingerprintFailures),
            @"Reordered":@(self.fingerprintReordered),@"LastDistinctAt":@(self.lastDistinctAt),
            @"GapMillisecondsHistogram":gaps,@"Error":self.fingerprintError?:@"",
            @"Reduction":self.fingerprintSingleSIMD?@"one hardware-sized SIMD group per 16x16 tile":@"256-thread tree",
            @"Method":@"64-bit GPU fingerprint of every RGB pixel in the opaque presented layer; excludes alpha and the independent hardware cursor"};
        if(self.fingerprintTrace){
            NSMutableDictionary *d=[result[@"DistinctPresentationTiming"] mutableCopy];
            d[@"Trace"]=self.fingerprintTrace.copy;d[@"TraceDropped"]=@(self.fingerprintTraceDropped);
            d[@"TraceColumns"]=@[@"PresentedAt",@"RGBFingerprint",@"Changed",@"MarkerX",@"MarkerY",@"RegionRGBFingerprint"];
            d[@"PipelineTrace"]=self.fingerprintPipelineTrace.copy;
            d[@"PipelineTraceColumns"]=@[@"OfferedToViewAt",@"MetalCommitAt",@"CompletionObservedAt",@"PresentedAt",@"DirectIOSurface"];
            d[@"UnpresentedTrace"]=self.fingerprintUnpresentedTrace.copy;
            d[@"UnpresentedTraceColumns"]=@[@"SubmittedAt",@"ResolvedAt",@"RGBFingerprint",@"MarkerX",@"MarkerY",@"RegionRGBFingerprint",@"GPUValid"];
            result[@"DistinctPresentationTiming"]=d;
        }
    }
    return result;
}
- (void)setFingerprintMeasurementEnabled:(BOOL)enabled {
    NSAssert(NSThread.isMainThread,@"Fingerprint measurement is main-thread owned");
    self.fingerprintGeneration++;self.lastFingerprintAt=self.lastDistinctAt=0;
    self.measureFingerprint=NO;
    if(!enabled)return;
    if(!self.fingerprintPipeline) {
        // Diagnostic only, compiled once on first enable. No shader or buffer
        // is allocated in normal launches. Each 16x16 tile emits sixteen bytes
        // (eight hash bytes plus a bounded optional marker reduction);
        // the CPU never reads/copies the full guest image for this measurement.
        // One hardware-sized SIMD group visits every pixel of a tile, with
        // exact unsigned sums/minima and no threadgroup barriers. Query the
        // execution width, never assume Intel/AMD/Apple have the same width.
        // Keep the original tree for older families or compilation failure.
        BOOL preferSIMD=[self.device supportsFamily:MTLGPUFamilyMac2] ||
                        [self.device supportsFamily:MTLGPUFamilyApple7];
        NSError *error=nil;
        for(NSUInteger attempt=0;attempt<(preferSIMD?2:1);attempt++){
            BOOL single=preferSIMD && !attempt;
            id<MTLLibrary> library=[self.device newLibraryWithSource:VIMDisplayFingerprintSource(single?2:0) options:nil error:&error];
            id<MTLFunction> function=[library newFunctionWithName:@"fingerprint"];
            id<MTLComputePipelineState> pipeline=function?[self.device newComputePipelineStateWithFunction:function error:&error]:nil;
            if(pipeline.maxTotalThreadsPerThreadgroup>=256 && (!single ||
                (pipeline.threadExecutionWidth>0 && pipeline.threadExecutionWidth<=256))){
                self.fingerprintPipeline=pipeline;self.fingerprintSingleSIMD=single;break;
            }
        }
        if(!self.fingerprintPipeline || self.fingerprintPipeline.maxTotalThreadsPerThreadgroup<256) {
            self.fingerprintError=error.localizedDescription?:@"GPU fingerprint pipeline is unavailable.";return;
        }
    }
    if(self.fingerprintPipeline.maxTotalThreadsPerThreadgroup<256){
        self.fingerprintError=@"GPU fingerprint threadgroup size is unsupported.";return;
    }
    self.fingerprintError=nil;self.measureFingerprint=YES;
}
- (void)setFingerprintTraceEnabled:(BOOL)enabled {
    NSAssert(NSThread.isMainThread,@"Trace is main-thread owned");
    self.fingerprintTrace=enabled?[NSMutableArray array]:nil;self.fingerprintTraceDropped=0;
    self.fingerprintPipelineTrace=enabled?[NSMutableArray array]:nil;
    self.fingerprintUnpresentedTrace=enabled?[NSMutableArray array]:nil;
}
- (void)setFingerprintMarkerRect:(NSRect)rect {
    NSAssert(NSThread.isMainThread,@"Marker is main-thread owned");
    if(!isfinite(rect.origin.x)||!isfinite(rect.origin.y)||!isfinite(rect.size.width)||!isfinite(rect.size.height)||
       rect.origin.x<0||rect.origin.y<0||rect.size.width<0||rect.size.height<0||NSMaxX(rect)>8192||NSMaxY(rect)>8192)rect=NSZeroRect;
    _fingerprintMarkerRect=rect;
}
- (void)resolveFingerprint:(VIMPresentedFingerprint *)frame {
    NSAssert(NSThread.isMainThread,@"Fingerprint counters are main-thread owned");
    if(frame.consumed || !frame.gpuReady || !frame.presentationReady)return;
    frame.consumed=YES;
    if(frame.epoch!=self.displayEpoch || frame.generation!=self.fingerprintGeneration)return;
    if(frame.presentedAt<=0){
        self.fingerprintUnpresented++;
        // Diagnostic only: a completed GPU image is not necessarily displayed.
        // Keep its already-computed metadata separately so a skipped final
        // update can be distinguished from an intentionally coalesced frame.
        // Never feed these rows into visible-frame/latency counters. No new
        // image readback, shader work or normal-launch trace is introduced.
        if(self.fingerprintUnpresentedTrace){
            if(self.fingerprintUnpresentedTrace.count<4096)
                [self.fingerprintUnpresentedTrace addObject:@[@(frame.submittedAt),@(CACurrentMediaTime()),@(frame.fingerprint),
                    frame.marker==UINT32_MAX?@(-1):@(frame.marker&65535),frame.marker==UINT32_MAX?@(-1):@(frame.marker>>16),
                    @(frame.regionFingerprint),@(frame.valid)]];
            else self.fingerprintTraceDropped++;
        }
        return;
    }
    if(!frame.valid){self.fingerprintFailures++;return;}
    if(self.fingerprintEpoch!=frame.epoch) {
        self.fingerprintEpoch=frame.epoch;self.lastFingerprintAt=self.lastDistinctAt=0;
    }
    if(frame.presentedAt<self.lastFingerprintAt){self.fingerprintReordered++;return;}
    self.fingerprintFrames++;
    BOOL changed=!self.lastFingerprintAt || self.lastFingerprint!=frame.fingerprint ||
        self.fingerprintWidth!=frame.width || self.fingerprintHeight!=frame.height;
    if(changed){
        self.distinctFrames++;
        if(self.lastDistinctAt>0)self->_distinctGaps[(NSUInteger)MIN(255.,(frame.presentedAt-self.lastDistinctAt)*1000.)]++;
        self.lastDistinctAt=frame.presentedAt;
    } else self.duplicateFrames++;
    self.lastFingerprint=frame.fingerprint;self.lastFingerprintAt=frame.presentedAt;
    self.fingerprintWidth=frame.width;self.fingerprintHeight=frame.height;
    if(self.fingerprintTrace){
        if(self.fingerprintTrace.count<4096){
            [self.fingerprintTrace addObject:@[@(frame.presentedAt),@(frame.fingerprint),@(changed),
                frame.marker==UINT32_MAX?@(-1):@(frame.marker&65535),frame.marker==UINT32_MAX?@(-1):@(frame.marker>>16),@(frame.regionFingerprint)]];
            [self.fingerprintPipelineTrace addObject:@[@(frame.offeredAt),@(frame.submittedAt),
                @(frame.completionObservedAt),@(frame.presentedAt),@(frame.directSurface)]];
        }
        else self.fingerprintTraceDropped++;
    }
}
- (BOOL)setPresentationDrawableCount:(NSUInteger)count {
    NSAssert(NSThread.isMainThread,@"Presentation queue is main-thread owned");
    if(count!=2 && count!=3)return NO;
    self.presentationLayer.maximumDrawableCount=count;
    return self.presentationLayer.maximumDrawableCount==count;
}
- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.diagnosticsDirectory=NSProcessInfo.processInfo.environment[@"VIM_FRAME_DIAGNOSTICS"];
        self.measurePresentation=[NSProcessInfo.processInfo.environment[@"VIM_DISPLAY_TIMING"] isEqualToString:@"1"];
        self.device = MTLCreateSystemDefaultDevice(); self.queue = [self.device newCommandQueue];
        self.drawableAcquisitionQueue=dispatch_queue_create("com.mac.virtualintelmac.drawable-acquisition",
            dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_USER_INTERACTIVE,0));
        if([NSProcessInfo.processInfo.environment[@"VIM_DISPLAY_DISTINCT"] isEqualToString:@"1"])
            [self setFingerprintMeasurementEnabled:YES];
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
    [self cancelPendingSurface];
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
    if((event.modifierFlags&(NSEventModifierFlagCommand|NSEventModifierFlagOption))==(NSEventModifierFlagCommand|NSEventModifierFlagOption)) {
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
- (BOOL)becomeFirstResponder {
    BOOL accepted=[super becomeFirstResponder];
    if(accepted){
        self.ownsResponderFocus=YES;
        if(self.softwareCursor)[self refreshNativeCursor];
    }
    return accepted;
}
- (BOOL)resignFirstResponder {
    BOOL resigned=[super resignFirstResponder];
    if(resigned){
        self.ownsResponderFocus=NO;
        if(self.inputCaptured||self.pressedKeys.count)[self releaseInputCapture];
        else if(self.softwareCursor)[self refreshNativeCursor];
    }
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
        [self cancelPendingSurface];
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
    [self cancelPendingSurface];
}
- (void)invalidatePresentation {
    NSAssert(NSThread.isMainThread,@"Presentation is main-thread owned");
    self.displayEpoch++;self.lastPresentedTexture=nil;self.presentationLayer.hidden=YES;
    [self cancelPendingSurface];
}
- (void)setSoftwareCursor:(BOOL)value {
    if(_softwareCursor==value)return;
    _softwareCursor=value;
    [self refreshNativeCursor];
}
- (BOOL)acceptsFirstMouse:(NSEvent *)event { return YES; }
- (void)setGuestCursorVisible:(BOOL)visible {
    // QEMU also reports visibility while the mouse moves. Reinstalling the
    // identical cursor invalidates AppKit's cursor rectangles and registers
    // its image with WindowServer on every report, even during window drags.
    if(_guestCursorVisible==visible)return;
    self.cursorVisibilityUpdates++;_guestCursorVisible=visible;[self refreshNativeCursor];
}
- (void)setGuestCursorPixels:(NSData *)pixels width:(NSUInteger)width height:(NSUInteger)height hotspot:(NSPoint)hotspot {
    if(!width||!height||width>256||height>256||pixels.length!=width*height*4||!isfinite(hotspot.x)||!isfinite(hotspot.y)||hotspot.x<0||hotspot.y<0||hotspot.x>=width||hotspot.y>=height)return;
    if(NSEqualSizes(self.cursorSize,NSMakeSize(width,height)) && NSEqualPoints(self.cursorHotspot,hotspot) &&
       [self.cursorPixels isEqualToData:pixels]){[self updateGuestCursorScale];return;}
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
    // VGA already draws the guest cursor. Mouse events reach the focused
    // guest before the first click captures keyboard shortcuts, so don't
    // overlay a second host cursor while the installer view owns focus.
    BOOL hideSoftware=self.softwareCursor&&self.window.isKeyWindow&&
        (self.inputCaptured||self.ownsResponderFocus);
    if(hideSoftware||(self.inputCaptured&&!self.guestCursorVisible)) {
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
    if(!self.window.isKeyWindow)return;
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
- (void)setNearestNeighborScaling:(BOOL)nearestNeighborScaling {
    _nearestNeighborScaling=nearestNeighborScaling;
    [CATransaction begin];[CATransaction setDisableActions:YES];
    NSString *filter=nearestNeighborScaling?kCAFilterNearest:kCAFilterLinear;
    self.presentationLayer.magnificationFilter=filter;
    self.presentationLayer.minificationFilter=filter;
    [CATransaction commit];
}
- (void)presentWidth:(NSUInteger)width height:(NSUInteger)height stride:(NSUInteger)stride
    completion:(void (^)(BOOL))completion {
    CFTimeInterval offered=self.fingerprintTrace?CACurrentMediaTime():0;
    [self presentWidth:width height:height stride:stride sourceTexture:nil offered:offered completion:completion];
}
- (void)presentSurface:(IOSurfaceRef)surface width:(NSUInteger)width height:(NSUInteger)height
    completion:(void (^)(BOOL))completion {
    NSAssert(NSThread.isMainThread, @"Presentation is main-thread owned");
    CFTimeInterval offered=self.fingerprintTrace?CACurrentMediaTime():0;
    if(!surface || width<320 || height<200 || width>8192 || height>8192 ||
       IOSurfaceGetPixelFormat(surface)!=0x42475241 || IOSurfaceGetPlaneCount(surface)!=0 ||
       IOSurfaceGetBytesPerElement(surface)!=4 || IOSurfaceGetWidth(surface)<width ||
       IOSurfaceGetHeight(surface)<height || IOSurfaceGetBytesPerRow(surface)<width*4) {
        completion(NO);return;
    }
    MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:IOSurfaceGetWidth(surface) height:IOSurfaceGetHeight(surface) mipmapped:NO];
    descriptor.usage=MTLTextureUsageShaderRead;
    id<MTLTexture> texture=[self.device newTextureWithDescriptor:descriptor iosurface:surface plane:0];
    if(!texture){completion(NO);return;}
    [self presentWidth:width height:height stride:0 sourceTexture:texture offered:offered completion:completion];
}
- (void)cancelPendingSurface {
    void (^completion)(BOOL)=self.pendingSurfaceCompletion;
    self.pendingSurfaceCompletion=nil;self.pendingSurfaceTexture=nil;
    self.pendingSurfaceWidth=self.pendingSurfaceHeight=self.pendingSurfaceEpoch=0;
    self.pendingSurfaceOfferedAt=0;
    if(completion)completion(NO);
}
- (void)drainPendingSurface {
    NSAssert(NSThread.isMainThread,@"Presentation queue is main-thread owned");
    if(self.frameInFlight || !self.pendingSurfaceCompletion)return;
    id<MTLTexture> texture=self.pendingSurfaceTexture;
    NSUInteger width=self.pendingSurfaceWidth,height=self.pendingSurfaceHeight,epoch=self.pendingSurfaceEpoch;
    CFTimeInterval offered=self.pendingSurfaceOfferedAt;
    void (^completion)(BOOL)=self.pendingSurfaceCompletion;
    self.pendingSurfaceCompletion=nil;self.pendingSurfaceTexture=nil;
    self.pendingSurfaceWidth=self.pendingSurfaceHeight=self.pendingSurfaceEpoch=0;
    self.pendingSurfaceOfferedAt=0;
    if(epoch!=self.displayEpoch){completion(NO);return;}
    [self presentWidth:width height:height stride:0 sourceTexture:texture offered:offered completion:completion];
}
- (void)presentWidth:(NSUInteger)width height:(NSUInteger)height stride:(NSUInteger)stride
    sourceTexture:(id<MTLTexture>)sourceTexture offered:(CFTimeInterval)offered completion:(void (^)(BOOL))completion {
    NSAssert(NSThread.isMainThread, @"Presentation is main-thread owned");
    if (self.displaySleeping || width < 320 || height < 200 ||
        width > 8192 || height > 8192 || (!sourceTexture && (!self.frameBuffer || stride < width * 4 || stride % 256 ||
        stride > self.frameBuffer.length / height))) { completion(NO); return; }
    if (!self.window || !self.window.visible || self.window.miniaturized) { completion(NO); return; }
    if(self.frameInFlight) {
        // CPU shared storage has one writer and its own ACK backpressure;
        // never retain it as a mutable queued image. GPU versions are
        // immutable until their completion callback releases the producer.
        if(!sourceTexture){completion(NO);return;}
        void (^superseded)(BOOL)=self.pendingSurfaceCompletion;
        self.pendingSurfaceTexture=sourceTexture;self.pendingSurfaceWidth=width;self.pendingSurfaceHeight=height;
        self.pendingSurfaceEpoch=self.displayEpoch;self.pendingSurfaceCompletion=completion;
        self.pendingSurfaceOfferedAt=offered;
        if(superseded)superseded(NO);
        return;
    }
    CGSize drawableSize=CGSizeMake(width,height);
    if(!CGSizeEqualToSize(self.presentationLayer.drawableSize,drawableSize))self.presentationLayer.drawableSize=drawableSize;
    [self layout];
    CFTimeInterval submitted=self.measurePresentation?CACurrentMediaTime():0;
    NSUInteger epoch=self.displayEpoch;
    self.frameInFlight=YES; // Includes drawable acquisition; one job per view.
    if(self.asynchronousDrawableAcquisition){
        CAMetalLayer *layer=self.presentationLayer;
        BOOL measure=self.measurePresentation;
        dispatch_async(self.drawableAcquisitionQueue,^{
            @autoreleasepool {
            CFTimeInterval began=measure?CACurrentMediaTime():0;
            id<CAMetalDrawable> drawable=[layer nextDrawable];
            CFTimeInterval elapsed=measure?CACurrentMediaTime()-began:0;
            dispatch_async(dispatch_get_main_queue(),^{
                if(measure){self.nextDrawableSeconds+=elapsed;self.nextDrawableMaxSeconds=MAX(self.nextDrawableMaxSeconds,elapsed);}
                [self encodeWidth:width height:height stride:stride sourceTexture:sourceTexture drawable:drawable
                    acquisitionEpoch:epoch submitted:submitted offered:offered completion:completion];
            });
            }
        });
    }else{
        id<CAMetalDrawable> drawable=[self.presentationLayer nextDrawable];
        if(self.measurePresentation){
            double elapsed=CACurrentMediaTime()-submitted;self.nextDrawableSeconds+=elapsed;
            self.nextDrawableMaxSeconds=MAX(self.nextDrawableMaxSeconds,elapsed);
        }
        [self encodeWidth:width height:height stride:stride sourceTexture:sourceTexture drawable:drawable
            acquisitionEpoch:epoch submitted:submitted offered:offered completion:completion];
    }
}
- (void)encodeWidth:(NSUInteger)width height:(NSUInteger)height stride:(NSUInteger)stride
    sourceTexture:(id<MTLTexture>)sourceTexture drawable:(id<CAMetalDrawable>)drawable
    acquisitionEpoch:(NSUInteger)acquisitionEpoch submitted:(CFTimeInterval)submitted offered:(CFTimeInterval)offered
    completion:(void (^)(BOOL))completion {
    NSAssert(NSThread.isMainThread,@"Frame selection and encoding are main-thread owned");
    if(!drawable || acquisitionEpoch!=self.displayEpoch || self.displaySleeping ||
       !self.window.visible || self.window.miniaturized ||
       drawable.texture.width!=width || drawable.texture.height!=height ||
       (self.pendingSurfaceCompletion && (self.pendingSurfaceWidth!=width || self.pendingSurfaceHeight!=height ||
                                         self.pendingSurfaceEpoch!=acquisitionEpoch))){
        self.frameInFlight=NO;[self drainPendingSurface];completion(NO);return;
    }
    if(self.pendingSurfaceCompletion){
        // Acquisition may wait for scanout. No pixel source was consumed on
        // that worker: choose the held NEWEST version now, never upload an
        // obsolete image simply because it asked for a drawable first.
        void (^superseded)(BOOL)=completion;
        sourceTexture=self.pendingSurfaceTexture;completion=self.pendingSurfaceCompletion;
        self.pendingSurfaceTexture=nil;self.pendingSurfaceCompletion=nil;
        self.pendingSurfaceWidth=self.pendingSurfaceHeight=self.pendingSurfaceEpoch=0;
        offered=self.pendingSurfaceOfferedAt;self.pendingSurfaceOfferedAt=0;
        self.coalescedBeforeEncoding++;
        if(self.measurePresentation)submitted=CACurrentMediaTime();
        superseded(NO);
    }
    id<MTLCommandBuffer> command = [self.queue commandBuffer];
    if (!command) { self.frameInFlight=NO;[self drainPendingSurface];completion(NO);return; }
    // Obtain/retain the texture before presentation. CAMetalDrawable forbids
    // querying texture after present; the completion handler needs this same
    // resource, not a new drawable or an additional copy.
    id<MTLTexture> presentedTexture=drawable.texture;
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    if (!blit) { self.frameInFlight=NO;[self drainPendingSurface];completion(NO);return; }
    // Same GPU copy required by the command-line renderer; no CPU readback,
    // new pixel allocation, network frame encoding or intermediate texture.
    if(sourceTexture) {
        [blit copyFromTexture:sourceTexture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(width,height,1) toTexture:presentedTexture destinationSlice:0
            destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    } else [blit copyFromBuffer:self.frameBuffer sourceOffset:0 sourceBytesPerRow:stride
        sourceBytesPerImage:stride * height sourceSize:MTLSizeMake(width, height, 1)
        toTexture:presentedTexture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    NSUInteger epoch=self.displayEpoch;
    VIMPresentedFingerprint *fingerprint=nil;id<MTLBuffer> fingerprintOutput=nil;NSUInteger fingerprintWords=0;
    if(self.measureFingerprint) {
        NSUInteger gx=(width+15)/16,gy=(height+15)/16;fingerprintWords=gx*gy*4;
        if(self.fingerprintBuffer.length<fingerprintWords*sizeof(uint32_t))
            self.fingerprintBuffer=[self.device newBufferWithLength:fingerprintWords*sizeof(uint32_t) options:MTLResourceStorageModeShared];
        id<MTLComputeCommandEncoder> compute=self.fingerprintBuffer?[command computeCommandEncoder]:nil;
        if(compute){
            fingerprint=[VIMPresentedFingerprint new];fingerprint.epoch=epoch;fingerprint.width=width;fingerprint.height=height;
            fingerprint.generation=self.fingerprintGeneration;
            fingerprint.offeredAt=offered;fingerprint.directSurface=sourceTexture!=nil;
            fingerprintOutput=self.fingerprintBuffer;
            [compute setComputePipelineState:self.fingerprintPipeline];[compute setTexture:presentedTexture atIndex:0];
            [compute setBuffer:fingerprintOutput offset:0 atIndex:0];
            NSRect roi=self.fingerprintMarkerRect;uint32_t marker[]={roi.origin.x,roi.origin.y,NSMaxX(roi),NSMaxY(roi)};
            [compute setBytes:marker length:sizeof(marker) atIndex:1];
            MTLSize threads=self.fingerprintSingleSIMD?MTLSizeMake(self.fingerprintPipeline.threadExecutionWidth,1,1):MTLSizeMake(16,16,1);
            [compute dispatchThreadgroups:MTLSizeMake(gx,gy,1) threadsPerThreadgroup:threads];
            [compute endEncoding];
        } else self.fingerprintFailures++;
    }
    if(fingerprint)[drawable addPresentedHandler:^(id<MTLDrawable> shown){
        CFTimeInterval timestamp=shown.presentedTime;
        dispatch_async(dispatch_get_main_queue(),^{fingerprint.presentedAt=timestamp;fingerprint.presentationReady=YES;[self resolveFingerprint:fingerprint];});
    }];
    if(self.measurePresentation) {
        // The SDK explicitly defines 0 as not presented/skipped. Completion of
        // the blit is not evidence that this drawable appeared on the display.
        [drawable addPresentedHandler:^(id<MTLDrawable> shown) {
            CFTimeInterval timestamp=shown.presentedTime;
            dispatch_async(dispatch_get_main_queue(), ^{
                if(epoch!=self.displayEpoch)return;
                if(timestamp<=0) {self.skippedPresentations++;return;}
                self.presentedFrames++;
                if(!self.firstPresentedAt)self.firstPresentedAt=timestamp;
                if(self.lastPresentedAt>0) {
                    if(timestamp<self.lastPresentedAt)self.reorderedPresentations++;
                    else {
                        NSUInteger bucket=(NSUInteger)MIN(255.,(timestamp-self.lastPresentedAt)*1000.);
                        self->_presentationTiming.gaps[bucket]++;
                    }
                }
                self.lastPresentedAt=MAX(self.lastPresentedAt,timestamp);
                if(timestamp>=submitted) {
                    NSUInteger bucket=(NSUInteger)MIN(255.,(timestamp-submitted)*1000.);
                    self->_presentationTiming.submissionDelays[bucket]++;
                }
            });
        }];
    }
    [command presentDrawable:drawable]; self.frameInFlight = YES;
    [command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
        // SDK GPUStartTime/GPUEndTime are host seconds and valid only after
        // real execution completes. Separate queueing from GPU work rather
        // than calling the whole submission-to-present interval "blit time".
        // Diagnostic only: no GPU query/readback, timer, log or ownership change.
        CFTimeInterval gpuStart=self.measurePresentation?finished.GPUStartTime:0;
        CFTimeInterval gpuEnd=self.measurePresentation?finished.GPUEndTime:0;
        CFTimeInterval cpuCompleted=(self.measurePresentation||fingerprint)?CACurrentMediaTime():0;
        uint64_t hash=UINT64_C(1469598103934665603)^width^((uint64_t)height<<32);
        uint64_t regionHash=hash;
        uint32_t marker=UINT32_MAX;
        if(fingerprint && finished.status==MTLCommandBufferStatusCompleted){
            const uint32_t *words=fingerprintOutput.contents;
            for(NSUInteger i=0;i<fingerprintWords;i+=4){
                hash=(hash^words[i])*UINT64_C(1099511628211);hash=(hash^words[i+1])*UINT64_C(1099511628211);
                regionHash=(regionHash^words[i+3])*UINT64_C(1099511628211);
                marker=MIN(marker,words[i+2]);
            }
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if(self.measurePresentation && epoch==self.displayEpoch){
                if(finished.status==MTLCommandBufferStatusCompleted && submitted>0 && cpuCompleted>=submitted){
                    self->_presentationTiming.cpuCompleted++;
                    self->_presentationTiming.cpuCompletionSeconds+=cpuCompleted-submitted;
                    self->_presentationTiming.cpuCompletionDelays[(NSUInteger)MIN(255.,(cpuCompleted-submitted)*1000.)]++;
                }
                // Real Intel control on macOS26 reports end timestamps ~27ms
                // AFTER the CPU has already observed completion. Do not turn
                // that cross-clock failure into a fictitious queue bottleneck.
                if(finished.status==MTLCommandBufferStatusCompleted && submitted>0 &&
                   isfinite(gpuStart) && isfinite(gpuEnd) && gpuStart>=submitted && gpuEnd>=gpuStart && gpuEnd<=cpuCompleted){
                    self->_presentationTiming.gpuTimed++;
                    self->_presentationTiming.gpuQueueSeconds+=gpuStart-submitted;
                    self->_presentationTiming.gpuWorkSeconds+=gpuEnd-gpuStart;
                    self->_presentationTiming.gpuQueueDelays[(NSUInteger)MIN(255.,(gpuStart-submitted)*1000.)]++;
                    self->_presentationTiming.gpuDurations[(NSUInteger)MIN(255.,(gpuEnd-gpuStart)*1000.)]++;
                    self->_presentationTiming.gpuCompletionDelays[(NSUInteger)MIN(255.,(gpuEnd-submitted)*1000.)]++;
                }else self->_presentationTiming.gpuUnavailable++;
            }
            if(fingerprint){fingerprint.fingerprint=hash;fingerprint.regionFingerprint=regionHash;fingerprint.marker=marker;fingerprint.valid=finished.status==MTLCommandBufferStatusCompleted;
                fingerprint.completionObservedAt=cpuCompleted;
                fingerprint.gpuReady=YES;[self resolveFingerprint:fingerprint];}
            self.frameInFlight = NO;
            BOOL current=finished.status==MTLCommandBufferStatusCompleted&&epoch==self.displayEpoch&&!self.displaySleeping;
            if(current){self.lastPresentedTexture=presentedTexture;self.presentationLayer.hidden=NO;
                self.completedFrames++;self.lastFrameCompletedAt=CACurrentMediaTime();}
            // Explicit developer diagnostic only. Snapshot our own shared frame
            // after successful GPU presentation, before returning its ownership.
            // No capture or filesystem work occurs in normal app launches.
            NSString *directory=self.diagnosticsDirectory;
            if(directory.length && current && !sourceTexture) {
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
            // Start the held newest version before invoking the old owner's
            // callback. Reentrant newer submissions then queue behind that
            // blit instead of allowing an older pending image to run last.
            [self drainPendingSurface];
            completion(current);
        });
    }];
    if(fingerprint)fingerprint.submittedAt=CACurrentMediaTime();
    [command commit];
}
@end
