// Bounded visible, production-view measurement check. No VM, guest input,
// screen-recording privilege or full-image readback is required.
#import <Cocoa/Cocoa.h>
#import "../VirtualIntelMac/VIMMetalView.h"
#import "../VirtualIntelMac/VIMResearchInput.h"
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
@interface VIMMetalView (FingerprintMetadataFixture)
- (void)resolveFingerprint:(id)frame;
@end
static void check(BOOL good,const char *message)
{if(!good){fprintf(stderr,"FAIL %s\n",message);exit(1);}}
static NSDictionary *counts(VIMMetalView *view)
{
    __block NSDictionary *result;
    dispatch_sync(dispatch_get_main_queue(),^{result=view.frameDiagnostics[@"DistinctPresentationTiming"];});return result;
}
static void waitFor(VIMMetalView *view,NSUInteger frames)
{
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:3];
    while([counts(view)[@"PresentedFingerprintFrames"] unsignedIntegerValue]<frames && deadline.timeIntervalSinceNow>0)
        [NSThread sleepForTimeInterval:.005];
    if([counts(view)[@"PresentedFingerprintFrames"] unsignedIntegerValue]!=frames){
        dispatch_sync(dispatch_get_main_queue(),^{fprintf(stderr,"%s\n",view.frameDiagnostics.description.UTF8String);});
    }
    check([counts(view)[@"PresentedFingerprintFrames"] unsignedIntegerValue]==frames,"actual presentation and GPU fingerprint both resolve");
}
static void retinaPacing(VIMMetalView *view,uint32_t *pixels)
{
    const NSUInteger width=2560,height=1600,stride=width*4;
    for(NSUInteger i=0;i<width*height;i++)pixels[i]=0xff336699;
    // Same production presentation view and full-resolution fingerprint.
    // This isolates diagnostic/presentation capacity, NOT guest rendering or
    // desktop latency. Keep writes ordered after each GPU completion, and
    // use actual presented counters rather than the submitted update rate.
    for(NSUInteger phase=0;phase<3;phase++){
        BOOL enabled=phase==1;
        __block NSDictionary *before;
        dispatch_sync(dispatch_get_main_queue(),^{
            [view setFingerprintMeasurementEnabled:enabled];[view setFingerprintTraceEnabled:enabled];
            before=view.frameDiagnostics;
        });
        CFTimeInterval start=CACurrentMediaTime();NSUInteger submitted=0;
        while(CACurrentMediaTime()-start<3){
            pixels[width*height-1]=0xff000000u|(uint32_t)(submitted+phase*1000);
            dispatch_semaphore_t complete=dispatch_semaphore_create(0);
            dispatch_sync(dispatch_get_main_queue(),^{
                [view presentWidth:width height:height stride:stride completion:^(BOOL good){
                    check(good,"Retina pacing GPU completion");dispatch_semaphore_signal(complete);
                }];
            });
            check(!dispatch_semaphore_wait(complete,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC)),"Retina pacing completion bound");
            submitted++;
            double delay=start+(double)submitted/120.-CACurrentMediaTime();
            if(delay>0)[NSThread sleepForTimeInterval:delay];
        }
        CFTimeInterval end=CACurrentMediaTime();
        [NSThread sleepForTimeInterval:.1];
        CFTimeInterval observed=CACurrentMediaTime();
        __block NSDictionary *after;
        dispatch_sync(dispatch_get_main_queue(),^{after=view.frameDiagnostics;});
        NSUInteger displayed=[after[@"PresentationTiming"][@"PresentedFrames"] unsignedIntegerValue]-
            [before[@"PresentationTiming"][@"PresentedFrames"] unsignedIntegerValue];
        NSMutableDictionary *report=[@{@"Phase":@(phase),@"FingerprintEnabled":@(enabled),@"Width":@(width),@"Height":@(height),
            @"SubmissionSeconds":@(end-start),@"CounterObservationSeconds":@(observed-start),
            @"Submitted":@(submitted),@"PresentedIncludingDrain":@(displayed),
            @"CounterPresentedPerSecondIncludingDrain":@(displayed/(observed-start)),
            @"Caution":@"Standalone production-view capacity, not a guest desktop or latency qualification."} mutableCopy];
        if(enabled){
            NSUInteger changed=0;NSMutableArray *gaps=[NSMutableArray array];double previous=0;
            for(NSArray *row in after[@"DistinctPresentationTiming"][@"Trace"]){
                double at=[row[0] doubleValue];if(at<start||at>end||![row[2] boolValue])continue;
                changed++;if(previous)[gaps addObject:@((at-previous)*1000)];previous=at;
            }
            report[@"DistinctDuringSubmission"]=@(changed);report[@"DistinctPerSubmissionSecond"]=@(changed/(end-start));
            report[@"DistinctGapsMilliseconds"]=gaps;
        }
        NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingSortedKeys error:NULL];
        printf("RETINA_PACING %s\n",[[[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding] UTF8String]);fflush(stdout);
    }
    dispatch_sync(dispatch_get_main_queue(),^{[view setFingerprintMeasurementEnabled:NO];});
}
int main(int argc,const char **argv){@autoreleasepool{
    BOOL pacing=argc==2&&!strcmp(argv[1],"--retina-pacing");
    check(argc==1||pacing,"optional --retina-pacing argument");
    setenv("VIM_DISPLAY_TIMING","1",1);
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,640,400)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    // This fixture supplies its own finite input batches; real pointer
    // movement through its test window must not contaminate those counts.
    window.ignoresMouseEvents=YES;
    VIMMetalView *view=[[VIMMetalView alloc] initWithFrame:NSMakeRect(0,0,640,400)];
    window.contentView=view;window.title=@"Distinct-frame measurement check";[window center];[window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/5),dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
    char name[]="/vim-fingerprint-XXXXXX";snprintf(name,sizeof(name),"/vim-fingerprint-%d",getpid());
    int fd=shm_open(name,O_CREAT|O_EXCL|O_RDWR,0600);check(fd>=0,"anonymous shared frame");shm_unlink(name);
    const NSUInteger width=640,height=400,stride=2560,bytes=8192ULL*8192*4;
    check(!ftruncate(fd,bytes),"frame extent");uint32_t *pixels=mmap(NULL,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    check(pixels!=MAP_FAILED,"frame alias");
    dispatch_sync(dispatch_get_main_queue(),^{check([view installFrameDescriptor:fd length:bytes error:NULL],"production frame mapping");});
    for(NSUInteger i=0;i<width*height;i++)pixels[i]=0xff336699;
    // The initial layer may legitimately skip its first drawable while the
    // new window commits its layout. Do not mislabel that GPU completion as
    // a displayed fingerprint; establish the window before measuring.
    dispatch_semaphore_t warm=dispatch_semaphore_create(0);
    dispatch_sync(dispatch_get_main_queue(),^{[view presentWidth:width height:height stride:stride completion:^(BOOL good){
        check(good,"warm-up GPU completion");dispatch_semaphore_signal(warm);
    }];});
    check(!dispatch_semaphore_wait(warm,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC)),"window warm-up");
    [NSThread sleepForTimeInterval:.1];
    dispatch_sync(dispatch_get_main_queue(),^{[view setFingerprintMeasurementEnabled:YES];
        [view setFingerprintTraceEnabled:YES];[view setFingerprintMarkerRect:NSMakeRect(10,10,40,30)];});
    check([counts(view)[@"Enabled"] boolValue],[[counts(view)[@"Error"] description] UTF8String]);
    for(NSUInteger frame=0;frame<4;frame++){
        // Identical second frame, then a single changed pixel in the final
        // threadgroup, then an alpha-only change (not visible in our opaque
        // layer): no sampling/downscaling or alpha-only false frame counts.
        if(frame==2)pixels[width*height-1]^=0x00000001;
        if(frame==3)pixels[width*height-1]^=0x01000000;
        dispatch_sync(dispatch_get_main_queue(),^{[view presentWidth:width height:height stride:stride completion:^(BOOL good){check(good,"GPU completion");}];});
        waitFor(view,frame+1);
    }
    NSDictionary *d=counts(view);check([d[@"DistinctFrames"] unsignedIntegerValue]==2,"two different complete RGB images");
    check([d[@"DuplicateFrames"] unsignedIntegerValue]==2,"identical and alpha-only image changes not counted twice");
    check(![d[@"Failures"] unsignedIntegerValue] && ![d[@"Reordered"] unsignedIntegerValue],"complete ordered measurements");
    check([d[@"Trace"] count]==4 && [d[@"Trace"][0][3] intValue]==-1,"bounded ordered trace and absent marker");
    pixels[5*width+5]=0xffff4040; // Red outside the search rectangle is ignored.
    pixels[19*width+21]=0xffcd7445; // Actual Snow minimize rim RGB205/116/69 is not red.
    pixels[20*width+20]=0xffff4040;
    dispatch_sync(dispatch_get_main_queue(),^{[view presentWidth:width height:height stride:stride completion:^(BOOL good){check(good,"marker GPU completion");}];});
    waitFor(view,5);d=counts(view);
    check([d[@"Trace"][4][3] intValue]==20 && [d[@"Trace"][4][4] intValue]==20,"GPU marker coordinates in backing pixels");
    pixels[20*width+20]=0xff336699;pixels[25*width+30]=0xffff4040;
    dispatch_sync(dispatch_get_main_queue(),^{[view presentWidth:width height:height stride:stride completion:^(BOOL good){check(good,"moved marker GPU completion");}];});
    waitFor(view,6);d=counts(view);
    check([d[@"Trace"][5][3] intValue]==30 && [d[@"Trace"][5][4] intValue]==25,"moved marker joins the actually presented image");
    check(![d[@"TraceDropped"] unsignedIntegerValue],"bounded fixture did not overflow its trace");
    [NSThread sleepForTimeInterval:.8];pixels[width*height-1]^=0x00000001;
    dispatch_sync(dispatch_get_main_queue(),^{[view presentWidth:width height:height stride:stride completion:^(BOOL good){check(good,"post-idle GPU completion");}];});
    waitFor(view,7);
    // Synthetic owned-metadata control, NOT a simulated display success:
    // an unpresented GPU image must remain out of visible counters/latencies.
    dispatch_sync(dispatch_get_main_queue(),^{
        NSDictionary *before=view.frameDiagnostics[@"DistinctPresentationTiming"];
        id frame=[NSClassFromString(@"VIMPresentedFingerprint") new];
        check(frame!=nil,"owned fingerprint metadata class");
        for(NSString *key in @[@"gpuReady",@"presentationReady",@"valid"])[frame setValue:@YES forKey:key];
        [frame setValue:[view valueForKey:@"displayEpoch"] forKey:@"epoch"];
        [frame setValue:before[@"Generation"] forKey:@"generation"];
        [frame setValue:@((33u<<16)|22u) forKey:@"marker"];
        [frame setValue:@(CACurrentMediaTime()) forKey:@"submittedAt"];
        [frame setValue:@123 forKey:@"fingerprint"];
        [frame setValue:@456 forKey:@"regionFingerprint"];
        [view resolveFingerprint:frame];[view resolveFingerprint:frame];
        NSDictionary *after=view.frameDiagnostics[@"DistinctPresentationTiming"];
        check([after[@"Unpresented"] unsignedIntegerValue]==[before[@"Unpresented"] unsignedIntegerValue]+1,
              "unpresented metadata consumed exactly once");
        check([after[@"PresentedFingerprintFrames"] isEqual:before[@"PresentedFingerprintFrames"]]&&
              [after[@"DistinctFrames"] isEqual:before[@"DistinctFrames"]]&&[after[@"Trace"] isEqual:before[@"Trace"]],
              "unpresented metadata cannot manufacture visible frames");
        NSArray *row=[after[@"UnpresentedTrace"] lastObject];
        check(row.count==7&&[row[0] doubleValue]>0&&[row[1] doubleValue]>=[row[0] doubleValue]&&
              [row[2] intValue]==123&&[row[3] intValue]==22&&[row[4] intValue]==33&&
              [row[5] intValue]==456&&[row[6] boolValue],"separate skipped marker/fingerprint metadata");
        [view setFingerprintTraceEnabled:NO];
        check(!view.frameDiagnostics[@"DistinctPresentationTiming"][@"UnpresentedTrace"],"no trace while disabled");
    });
    dispatch_sync(dispatch_get_main_queue(),^{[view setFingerprintMeasurementEnabled:NO];});
    check(![counts(view)[@"Enabled"] boolValue],"same-session disable without changing presentation");
    dispatch_semaphore_t inputDone=dispatch_semaphore_create(0);
    __block NSUInteger downs=0,ups=0,batches=0;
    dispatch_sync(dispatch_get_main_queue(),^{
        __weak VIMMetalView *weakView=view;
        view.inputHandler=^(NSArray *events){
            if(!weakView.diagnosticInputObserver)return;
            batches++;
            for(NSDictionary *event in events)if([event[@"type"] isEqual:@"btn"]){
                NSNumber *down=event[@"data"][@"down"];
                check(CFGetTypeID((__bridge CFTypeRef)down)==CFBooleanGetTypeID(),"strict QMP button boolean, not numeric 0/1");
                if(down.boolValue)downs++;else ups++;
            }
            if(weakView.diagnosticInputObserver)weakView.diagnosticInputObserver(CACurrentMediaTime(),nil);
        };
        VIMResearchPointerWorkload(view,@{@"Pattern":@"circle",@"Seconds":@1,@"Hz":@60,@"Origin":@[@16000,@16000],
            @"Amplitude":@[@1000,@1000],@"MarkerRect":@[@0,@0,@0,@0]},^BOOL{return YES;},^(NSDictionary *result){
            if(result[@"Error"]||[result[@"Inputs"] count]!=batches)
                fprintf(stderr,"INPUT_FIXTURE batches=%lu inputs=%lu error=%s\n",(unsigned long)batches,
                    (unsigned long)[result[@"Inputs"] count],[result[@"Error"] description].UTF8String?:"none");
            check(!result[@"Error"]&&[result[@"Inputs"] count]==batches,"finite production-path input trace");
            check([result[@"InputReplies"] count]==batches&&downs==1&&ups==1,"ordered input replies and one button release");
            check(!view.diagnosticInputObserver,"diagnostic observer retired");
            view.inputHandler=nil;dispatch_semaphore_signal(inputDone);
        });
    });
    check(!dispatch_semaphore_wait(inputDone,dispatch_time(DISPATCH_TIME_NOW,4*NSEC_PER_SEC)),"finite pointer workload completion");
    dispatch_semaphore_t commandDone=dispatch_semaphore_create(0);
    __block NSUInteger commandDowns=0,commandUps=0;
    __block BOOL commandHeld=NO,mouseHeld=NO;
    dispatch_sync(dispatch_get_main_queue(),^{
        __weak VIMMetalView *weakView=view;
        view.inputHandler=^(NSArray *events){
            if(!weakView.diagnosticInputObserver)return;
            for(NSDictionary *event in events){
                if([event[@"type"] isEqual:@"key"]){
                    BOOL down=[event[@"data"][@"down"] boolValue];
                    check([event[@"data"][@"key"][@"data"] isEqual:@"meta_l"],"only the requested guest modifier");
                    check(!mouseHeld,"Command press precedes mouse down and release follows mouse up");
                    commandHeld=down;if(down)commandDowns++;else commandUps++;
                } else if([event[@"type"] isEqual:@"btn"]){
                    check(commandHeld,"guest Command held through the entire drag");
                    mouseHeld=[event[@"data"][@"down"] boolValue];
                }
            }
            if(weakView.diagnosticInputObserver)weakView.diagnosticInputObserver(CACurrentMediaTime(),nil);
        };
        VIMResearchPointerWorkload(view,@{@"Pattern":@"circle",@"Seconds":@1,@"Hz":@60,@"Origin":@[@16000,@16000],
            @"Amplitude":@[@1000,@1000],@"MarkerRect":@[@0,@0,@0,@0],@"CommandDrag":@YES},^BOOL{return YES;},^(NSDictionary *result){
            check(!result[@"Error"]&&[result[@"CommandDrag"] boolValue],"Command-drag reported explicitly");
            check(commandDowns==1&&commandUps==1&&!commandHeld&&!mouseHeld,"no held guest keys or buttons remain");
            view.inputHandler=nil;dispatch_semaphore_signal(commandDone);
        });
    });
    check(!dispatch_semaphore_wait(commandDone,dispatch_time(DISPATCH_TIME_NOW,4*NSEC_PER_SEC)),"finite Command-drag completion");
    if(pacing)retinaPacing(view,pixels);
    munmap(pixels,bytes);dispatch_sync(dispatch_get_main_queue(),^{[view discardFrameStorage];[window orderOut:nil];});
    puts("PASS actual-presented whole-image GPU fingerprints: duplicate, final-pixel/alpha changes, bounded trace, clipped moving marker, disable");exit(0);
    });
    [NSApp run];return 0;
}}
