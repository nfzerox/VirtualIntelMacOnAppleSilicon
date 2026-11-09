// Explicit, finite development diagnostic. Uses the production view's existing
// input owner; no second QMP client, guest injection, AX queries during motion,
// global event tap, or recurring work in normal launches.
// Included by VIMSession.m so the research harness cannot drift into a second
// input/display implementation.
#import <QuartzCore/QuartzCore.h>
// The production input handler captures the observer when it queues QMP, not
// when QMP replies. Scope the observer to this one synchronous submission;
// AppKit mouse moves and focus releases between test events aren't test ACKs.
static void VIMResearchSendInput(VIMMetalView *view, NSArray *events,
                                void (^observer)(CFTimeInterval,NSError *)) {
    NSCAssert(NSThread.isMainThread,@"Diagnostic input is main-thread owned");
    view.diagnosticInputObserver=observer;
    view.inputHandler(events);
    view.diagnosticInputObserver=nil;
}
// Shortcuts must not stay held while a file-polling harness waits for its
// next request. This finite sequence uses the SAME production input owner,
// 50-ms USB key holds and host-clock metadata. No guest agent/AX/event tap.
static void VIMResearchKeyWorkload(VIMMetalView *view,NSDictionary *command,
    BOOL (^available)(void),void (^finish)(NSDictionary *)) {
    NSString *workload=command[@"Workload"];
    NSNumber *cycles=command[@"Cycles"];
    if(![@[@"mission-control",@"app-expose",@"spaces",@"all",@"typing"] containsObject:workload?:@""] ||
       ![cycles isKindOfClass:NSNumber.class] || cycles.doubleValue!=cycles.integerValue ||
       cycles.integerValue<1 || cycles.integerValue>4 || !available() || !view.inputHandler) {
        finish(@{@"Error":@"Choose a running guest, a desktop transition or typing workload and 1–4 cycles."});return;
    }
    NSArray *workloads=[workload isEqual:@"all"]?@[@"mission-control",@"app-expose",@"spaces"]:@[workload];
    NSDictionary *(^key)(NSString *,BOOL)=^NSDictionary *(NSString *code,BOOL down){
        return @{@"type":@"key",@"data":@{@"down":@(down),@"key":@{@"type":@"qcode",@"data":code}}};
    };
    NSMutableArray *plan=[NSMutableArray array],*inputs=[NSMutableArray array],*replies=[NSMutableArray array];
    NSMutableSet<NSString *> *held=[NSMutableSet set];
    double offset=.5;
    for(NSString *item in workloads)for(NSInteger cycle=0;cycle<cycles.integerValue;cycle++) {
        if([item isEqual:@"typing"]){
            // Real USB input at ten keys/second. Do not depend on the guest's
            // repeat/Press-and-Hold preferences, script an app's text model,
            // or let diagnostic-file polling pace the event edges. Run only
            // with a deliberately opened throwaway document focused.
            for(NSInteger i=0;i<24;++i){
                [plan addObject:@{@"Offset":@(offset),@"Workload":item,@"Cycle":@(cycle),
                    @"Phase":@"type-down",@"Events":@[key(@"x",YES)]}];
                [plan addObject:@{@"Offset":@(offset+.05),@"Workload":item,@"Cycle":@(cycle),
                    @"Phase":@"type-up",@"Events":@[key(@"x",NO)]}];
                offset+=.1;
            }
            continue;
        }
        NSString *arrow=[item isEqual:@"mission-control"]?@"up":[item isEqual:@"app-expose"]?@"down":@"right";
        [plan addObject:@{@"Offset":@(offset),@"Workload":item,@"Cycle":@(cycle),@"Phase":@"enter-down",
            @"Events":@[key(@"ctrl",YES),key(arrow,YES)]}];
        [plan addObject:@{@"Offset":@(offset+.05),@"Workload":item,@"Cycle":@(cycle),@"Phase":@"enter-up",
            @"Events":@[key(arrow,NO),key(@"ctrl",NO)]}];
        BOOL spaces=[item isEqual:@"spaces"];
        [plan addObject:@{@"Offset":@(offset+.85),@"Workload":item,@"Cycle":@(cycle),@"Phase":@"return-down",
            @"Events":spaces?@[key(@"ctrl",YES),key(@"left",YES)]:@[key(@"esc",YES)]}];
        [plan addObject:@{@"Offset":@(offset+.90),@"Workload":item,@"Cycle":@(cycle),@"Phase":@"return-up",
            @"Events":spaces?@[key(@"left",NO),key(@"ctrl",NO)]:@[key(@"esc",NO)]}];
        offset+=1.7;
    }
    BOOL wasEnabled=[view.frameDiagnostics[@"DistinctPresentationTiming"][@"Enabled"] boolValue];
    if(!wasEnabled)[view setFingerprintMeasurementEnabled:YES];
    if(![view.frameDiagnostics[@"DistinctPresentationTiming"][@"Enabled"] boolValue]) {
        finish(@{@"Error":@"The presentation observer is unavailable."});return;
    }
    [NSApp activateIgnoringOtherApps:YES];[view.window makeKeyAndOrderFront:nil];
    [view setFingerprintMarkerRect:NSZeroRect];[view setFingerprintTraceEnabled:YES];
    NSDictionary *before=view.frameDiagnostics;
    CFTimeInterval start=CACurrentMediaTime();
    __block NSString *inputError=nil;
    void (^observer)(CFTimeInterval,NSError *)=^(CFTimeInterval queued,NSError *error){
        [replies addObject:@[@(queued),@(CACurrentMediaTime()),error.localizedDescription?:@""]];
        if(error)inputError=error.localizedDescription;
    };
    __block NSUInteger next=0;
    __block BOOL released=NO;
    __block dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
    void (^send)(NSArray *,NSDictionary *,CFTimeInterval)=^(NSArray *events,NSDictionary *step,CFTimeInterval due){
        CFTimeInterval queued=CACurrentMediaTime();
        [inputs addObject:@{@"QueuedAt":@(queued),@"ScheduledAt":@(due),@"Step":step,@"Events":events}];
        for(NSDictionary *event in events){
            NSString *code=event[@"data"][@"key"][@"data"];
            if([event[@"data"][@"down"] boolValue])[held addObject:code];else [held removeObject:code];
        }
        VIMResearchSendInput(view,events,observer);
    };
    void (^complete)(NSString *)=^(NSString *error){
        dispatch_source_cancel(timer);timer=nil;
        if(held.count && view.inputHandler){
            NSMutableArray *events=[NSMutableArray array];
            for(NSString *code in held.copy)[events addObject:key(code,NO)];
            send(events,@{@"Phase":@"cleanup-release"},CACurrentMediaTime());
        }
        NSDictionary *after=view.frameDiagnostics;
        if(!error && replies.count!=inputs.count)error=@"Not every input received an acknowledgement; this trial is invalid.";
        for(NSString *field in @[@"PixelWidth",@"PixelHeight",@"BackingScale"])
            if(!error && ![before[field] isEqual:after[field]])error=@"Display geometry changed during the key sequence; this trial is invalid.";
        [view setFingerprintTraceEnabled:NO];
        if(!wasEnabled)[view setFingerprintMeasurementEnabled:NO];
        NSMutableDictionary *result=[@{@"Workload":workload,@"Cycles":cycles,@"Before":before,@"After":after,
            @"StartedAt":@(start),@"FinishedAt":@(CACurrentMediaTime()),@"Inputs":inputs,@"InputReplies":replies.copy,
            @"Caution":@"Host queued-input and actually presented image metadata; confirm visible transitions and scene identity. Not physical HID-to-photon latency or a conformance pass."} mutableCopy];
        if(error)result[@"Error"]=error;
        finish(result);
    };
    dispatch_source_set_event_handler(timer,^{
        if(!available() || !view.inputHandler || inputError){complete(inputError?:@"The guest stopped accepting input.");return;}
        CFTimeInterval now=CACurrentMediaTime();
        if(!released){
            NSMutableArray *events=[NSMutableArray array];
            for(NSString *code in @[@"ctrl",@"meta_l",@"esc",@"up",@"down",@"left",@"right",@"x"])[events addObject:key(code,NO)];
            send(events,@{@"Phase":@"initial-release"},start);released=YES;
        }
        // Never replay a stale press/release pair in the same timer tick.
        // Excessive UI-thread delay invalidates this trial rather than
        // turning two coalesced USB edges into a fictitious transition.
        if(next<plan.count){
            NSDictionary *step=plan[next];CFTimeInterval due=start+[step[@"Offset"] doubleValue];
            if(now>=due){
                if(now-due>.025){complete(@"The host input timer missed its bounded key interval; this trial is invalid.");return;}
                send(step[@"Events"],step,due);next++;
            }
        }
        if(next==plan.count && now>=start+offset)complete(nil);
    });
    dispatch_source_set_timer(timer,DISPATCH_TIME_NOW,NSEC_PER_MSEC*5,NSEC_PER_MSEC/4);
    dispatch_resume(timer);
}
static void VIMResearchPointerWorkload(VIMMetalView *view, NSDictionary *command,
    BOOL (^available)(void), void (^finish)(NSDictionary *)) {
    NSString *pattern=command[@"Pattern"];
    NSArray *origin=command[@"Origin"],*amplitude=command[@"Amplitude"],*marker=command[@"MarkerRect"];
    NSNumber *commandOption=command[@"CommandDrag"];
    if(commandOption&&![commandOption isKindOfClass:NSNumber.class]){
        finish(@{@"Error":@"CommandDrag must be a boolean."});return;
    }
    BOOL commandDrag=commandOption.boolValue;
    BOOL activation=[pattern isEqual:@"activate"];
    double seconds=[command[@"Seconds"] isKindOfClass:NSNumber.class]?[command[@"Seconds"] doubleValue]:NAN;
    double hz=[command[@"Hz"] isKindOfClass:NSNumber.class]?[command[@"Hz"] doubleValue]:NAN;
    if(![@[@"circle",@"reversals",@"steps",@"activate"] containsObject:pattern?:@""] || (activation&&commandDrag) ||
       ![origin isKindOfClass:NSArray.class]||origin.count!=2||
       ![amplitude isKindOfClass:NSArray.class]||amplitude.count!=2||
       ![marker isKindOfClass:NSArray.class]||marker.count!=4||
       !isfinite(seconds)||seconds<1||seconds>15||!isfinite(hz)||hz<15||hz>120) {
        finish(@{@"Error":@"Choose a bounded circle, reversals, steps or activate workload, normalized Origin/Amplitude pairs, backing-pixel MarkerRect, Seconds 1–15 and Hz 15–120. Activation cannot hold Command."});return;
    }
    for(id value in [origin arrayByAddingObjectsFromArray:[amplitude arrayByAddingObjectsFromArray:marker]])
        if(![value isKindOfClass:NSNumber.class]||!isfinite([value doubleValue])) {
            finish(@{@"Error":@"Pointer coordinates must be finite numbers."});return;
        }
    double x=[origin[0] doubleValue],y=[origin[1] doubleValue];
    double ax=[amplitude[0] doubleValue],ay=[amplitude[1] doubleValue];
    NSDictionary *geometry=view.frameDiagnostics;
    NSRect roi=NSMakeRect([marker[0] doubleValue],[marker[1] doubleValue],[marker[2] doubleValue],[marker[3] doubleValue]);
    if(ax<0||ay<0||x-ax<0||x+ax>32767||y-ay<0||y+ay>32767||
       roi.origin.x<0||roi.origin.y<0||roi.size.width<0||roi.size.height<0||
       NSMaxX(roi)>[geometry[@"PixelWidth"] doubleValue]||NSMaxY(roi)>[geometry[@"PixelHeight"] doubleValue]||
       !available()||!view.inputHandler) {
        finish(@{@"Error":@"The workload must stay inside a running guest display."});return;
    }
    BOOL wasEnabled=[geometry[@"DistinctPresentationTiming"][@"Enabled"] boolValue];
    if(!wasEnabled)[view setFingerprintMeasurementEnabled:YES];
    if(![view.frameDiagnostics[@"DistinctPresentationTiming"][@"Enabled"] boolValue]) {
        finish(@{@"Error":@"The GPU presentation measurement is unavailable.",@"Frame":view.frameDiagnostics});return;
    }
    [NSApp activateIgnoringOtherApps:YES];[view.window makeKeyAndOrderFront:nil];
    [view setFingerprintMarkerRect:roi];[view setFingerprintTraceEnabled:YES];
    NSDictionary *before=view.frameDiagnostics;
    // All times share CACurrentMediaTime/MTLDrawable.presentedTime's host clock.
    // The interval excludes activation/calibration and the final button release.
    // Give the genuine guest window manager time to expose the independent
    // calibration move before returning it. Snow can coalesce the old 200-ms
    // out-and-back during drag acquisition. This is diagnostic preroll only;
    // the measured path/duration and strict presented-marker checks do not
    // change, and a missing calibration remains a failed trial.
    CFTimeInterval start=CACurrentMediaTime(),motionStart=start+1.6,motionEnd=motionStart+seconds;
    NSMutableArray *inputs=[NSMutableArray arrayWithCapacity:(NSUInteger)(seconds*hz)+12];
    NSMutableArray *replies=[NSMutableArray arrayWithCapacity:(NSUInteger)(seconds*hz)+12];
    void (^observer)(CFTimeInterval,NSError *)=^(CFTimeInterval queued,NSError *error){
        [replies addObject:@[@(queued),@(CACurrentMediaTime()),error.localizedDescription?:@""]];
    };
    __block dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,dispatch_get_main_queue());
    __block NSUInteger stage=0;__block NSInteger lastTick=-1,lastStep=-1,calibrationTick=-1;
    __block NSUInteger missedTicks=0;__block double maxLate=0;__block BOOL held=NO,modifierHeld=NO;
    __block CFTimeInterval releaseAt=0;
    void (^send)(double,double,NSString *,NSInteger,CFTimeInterval,NSInteger)=
    ^(double px,double py,NSString *phase,NSInteger step,CFTimeInterval scheduled,NSInteger button){
        NSMutableArray *events=[@[
            @{@"type":@"abs",@"data":@{@"axis":@"x",@"value":@((int)llround(px))}},
            @{@"type":@"abs",@"data":@{@"axis":@"y",@"value":@((int)llround(py))}}] mutableCopy];
        // Box an actual BOOL, not C's int-valued relational expression: QMP
        // correctly rejects numeric 0/1 for a JSON boolean.
        // Keyboard and tablet are distinct polled USB devices. Ordering keys
        // before a button in one QMP batch does not order guest HID delivery.
        // Hold Command for the existing 100-ms origin interval before down.
        if(commandDrag&&[phase isEqual:@"origin"]){
            [events insertObject:@{@"type":@"key",@"data":@{@"down":@YES,@"key":@{@"type":@"qcode",@"data":@"meta_l"}}} atIndex:0];
            modifierHeld=YES;
        }
        if(button)[events addObject:@{@"type":@"btn",@"data":@{@"button":@"left",@"down":button>0?@YES:@NO}}];
        if(modifierHeld&&[phase isEqual:@"command-release"]){
            [events addObject:@{@"type":@"key",@"data":@{@"down":@NO,@"key":@{@"type":@"qcode",@"data":@"meta_l"}}}];
            modifierHeld=NO;
        }
        CFTimeInterval queued=CACurrentMediaTime();
        [inputs addObject:@[@(queued),@(scheduled),@((int)llround(px)),@((int)llround(py)),phase,@(step)]];
        VIMResearchSendInput(view,events,observer);
    };
    void (^complete)(NSString *)=^(NSString *error){
        dispatch_source_cancel(timer);timer=nil;
        if(held&&view.inputHandler){send(x,y,@"cancel-release",-1,CACurrentMediaTime(),-1);held=NO;}
        if(modifierHeld&&view.inputHandler)send(x,y,@"command-release",-1,CACurrentMediaTime(),0);
        NSDictionary *after=view.frameDiagnostics;
        [view setFingerprintTraceEnabled:NO];[view setFingerprintMarkerRect:NSZeroRect];
        if(!wasEnabled)[view setFingerprintMeasurementEnabled:NO];
        NSMutableDictionary *result=[@{@"Pattern":pattern,@"Seconds":@(seconds),@"Hz":@(hz),@"CommandDrag":@(commandDrag),
            @"MotionStart":@(motionStart),@"MotionEnd":@(motionEnd),@"InputColumns":@[@"QueuedAt",@"ScheduledAt",@"X",@"Y",@"Phase",@"Step"],
            @"Inputs":inputs,@"MissedHostTimerTicks":@(missedTicks),@"MaximumHostTimerLatenessMilliseconds":@(maxLate*1000),
            @"InputReplies":replies.copy,@"InputReplyColumns":@[@"QueuedAt",@"CompletedAt",@"Error"],
            @"Before":before,@"After":after,
            @"Caution":@"Explicit host input-queued to actually presented guest pixels; not physical HID-to-photon latency. Marker calibration and missed steps must be checked. No guest window queries run during motion."} mutableCopy];
        if(error)result[@"Error"]=error;
        finish(result);
    };
    dispatch_source_set_event_handler(timer,^{
        if(!available()||!view.inputHandler){complete(@"The guest stopped accepting input.");return;}
        CFTimeInterval now=CACurrentMediaTime();
        if(activation){
            // Fixed exposed points in two real windows. No drag/calibration,
            // AX window query or guest screenshot occurs during the trial.
            if(stage==0){send(x-ax,y-ay,@"prepare",-1,start,0);stage=1;}
            if(held&&now>=releaseAt){
                double sign=(lastStep&1)?1:-1;
                send(x+sign*ax,y+sign*ay,@"activation-release",lastStep,releaseAt,-1);held=NO;
            }
            if(now>=motionStart&&now<motionEnd){
                NSInteger step=(NSInteger)floor((now-motionStart)/.7);
                if(step!=lastStep){
                    CFTimeInterval scheduled=motionStart+step*.7;
                    maxLate=MAX(maxLate,now-scheduled);
                    if(step>lastStep+1)missedTicks+=step-lastStep-1;
                    lastStep=step;double sign=(step&1)?1:-1;
                    send(x+sign*ax,y+sign*ay,@"activation-down",step,scheduled,1);
                    held=YES;releaseAt=now+.04;
                }
            }
            if(now>=motionEnd+.6)complete(nil);
            return;
        }
        if(stage==0){send(x,y,@"origin",-1,start,0);stage++;}
        if(stage==1&&now>=start+.1){send(x,y,@"down",-1,start+.1,1);held=YES;stage++;}
        if(stage==2&&now>=start+.2){
            // Use a genuine short drag, not one teleported tablet sample.
            // Older window managers may consume the first displaced sample
            // to begin dragging. Record every lead-in separately; only the
            // final held target is the independently checked calibration.
            double progress=MIN(1.,(now-start-.2)/.25);
            NSInteger tick=(NSInteger)floor(progress*.25*hz);
            if(progress>=1.){send(x+MIN(ax/2,512),y,@"calibration",-1,start+.45,0);stage++;}
            else if(tick!=calibrationTick){
                calibrationTick=tick;
                send(x+MIN(ax/2,512)*progress,y,@"calibration-lead-in",-1,start+.2+tick/hz,0);
            }
        }
        if(stage==3&&now>=start+.9){send(x,y,@"calibration-return",-1,start+.9,0);stage++;}
        if(now>=motionStart&&now<motionEnd){
            NSInteger tick=(NSInteger)floor((now-motionStart)*hz);
            if(tick!=lastTick){
                if(tick>lastTick+1)missedTicks+=tick-lastTick-1;
                lastTick=tick;maxLate=MAX(maxLate,now-(motionStart+tick/hz));
                double progress=(now-motionStart)/seconds,dx=0,dy=0;
                NSInteger step=-1;
                if([pattern isEqual:@"circle"]){double angle=progress*4*M_PI;dx=sin(angle);dy=(1-cos(angle))/2;}
                else {
                    static const double points[][2]={{0,0},{1,1},{-1,-1},{1,-1},{-1,1},{.6,-.5},{-.6,.5},{0,0},
                        {-.7,-.8},{.8,.7},{-.5,.8},{.7,-.7},{-1,0},{1,0},{0,1},{0,0}};
                    double segment=progress*15;step=MIN(14,(NSInteger)floor(segment));
                    if([pattern isEqual:@"steps"]){dx=points[step][0];dy=points[step][1];}
                    else {double f=segment-step;dx=points[step][0]*(1-f)+points[step+1][0]*f;dy=points[step][1]*(1-f)+points[step+1][1]*f;}
                }
                if(![pattern isEqual:@"steps"]||step!=lastStep){
                    send(x+ax*dx,y+ay*dy,@"motion",step,motionStart+tick/hz,0);lastStep=step;
                }
            }
        }
        if(stage==4&&now>=motionEnd){send(x,y,@"up",-1,motionEnd,-1);held=NO;stage++;}
        if(stage==5&&commandDrag&&now>=motionEnd+.1){send(x,y,@"command-release",-1,motionEnd+.1,0);stage++;}
        if(now>=motionEnd+.6)complete(nil);
    });
    dispatch_source_set_timer(timer,DISPATCH_TIME_NOW,(uint64_t)(NSEC_PER_SEC/hz),NSEC_PER_MSEC/4);
    dispatch_resume(timer);
}
