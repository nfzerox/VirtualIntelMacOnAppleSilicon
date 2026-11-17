#import "../compat/VIMHostInstallTiming.h"
#include <assert.h>
#include <stdio.h>

int main(int argc,const char **argv) { @autoreleasepool {
    assert(argc==2);
    NSDictionary *model=[NSDictionary dictionaryWithContentsOfFile:@(argv[1])];
    assert([model[@"Guests"] count]==13&&[model[@"Scales"] count]==2);
    NSString *suite=[@"com.mac.virtualintelmac.timing-test." stringByAppendingString:NSUUID.UUID.UUIDString];
    NSUserDefaults *defaults=[[NSUserDefaults alloc] initWithSuiteName:suite];
    NSString *key=VIMHostInstallTimingKey(@"arm64-apfs",@"mavericks",YES);
    NSString *stageKey=[key stringByAppendingString:@"-stages"];
    NSArray *prior=VIMHostInstallTimingPrior(model,@"arm64-apfs",@"mavericks",YES);
    VIMHostInstallTimingRecordDuration(defaults,key,100);
    assert(![defaults objectForKey:key]);
    // Old production observations must not override the bundled baseline.
    [defaults setDouble:10 forKey:key];
    [defaults setObject:@[@{@"Stage":@"payload",@"Seconds":@10}] forKey:stageKey];
    assert(VIMHostInstallTimingLearnedDuration(defaults,key)==0);
    assert([VIMHostInstallTimingHistory(model,defaults,@"arm64-apfs",@"mavericks",YES) isEqual:prior]);
    [defaults setBool:YES forKey:@"VIMMeasureInstallTimings"];
    VIMHostInstallTimingRecordDuration(defaults,key,20);
    assert(VIMHostInstallTimingLearnedDuration(defaults,key)==14);
    assert([VIMHostInstallTimingHistory(model,defaults,@"arm64-apfs",@"mavericks",YES) isEqual:[defaults objectForKey:stageKey]]);
    [defaults setBool:NO forKey:@"VIMMeasureInstallTimings"];
    VIMHostInstallTimingRecordDuration(defaults,key,100);
    assert([defaults doubleForKey:key]==14);
    assert([VIMHostInstallTimingHistory(model,defaults,@"arm64-apfs",@"mavericks",YES) isEqual:prior]);
    [defaults removePersistentDomainForName:suite];
    puts("PASS timing persistence and local estimate overrides require VIMMeasureInstallTimings");
    assert([VIMHostInstallTimingProfile(@"arm64",@"ExFAT") isEqual:@"arm64-exfat"]);
    assert([VIMHostInstallTimingProfile(@"x86_64",@"hfs") isEqual:@"x86_64-apfs"]);
    assert(!VIMHostInstallTimingHasUpdates(@{@"DownloadSecurityUpdates":@YES}));
    assert(!VIMHostInstallTimingHasUpdates(@{@"MojaveUpdates":@{}}));
    assert(VIMHostInstallTimingHasUpdates(@{@"MojaveUpdates":@{@"Security":@"fixture.pkg"}}));
    NSArray *history=@[@{@"Stage":@"start",@"Seconds":@2},
        @{@"Stage":@"tiny",@"Seconds":@.2},@{@"Stage":@"payload",@"Seconds":@100},
        @{@"Stage":@"catalog",@"Seconds":@10},@{@"Stage":@"convert",@"Seconds":@30}];
    NSArray *done=@[@{@"Stage":@"start",@"Seconds":@4}];
    NSDictionary *match=VIMHostInstallTimingMatch(history,done,@"payload");
    assert([match[@"StageSeconds"] doubleValue]==100);
    assert([match[@"FutureSeconds"] doubleValue]==40);
    assert([match[@"ObservedCompletedSeconds"] doubleValue]==4);
    assert([match[@"ReferenceCompletedSeconds"] doubleValue]==2);
    assert(VIMHostInstallTimingMatch(history,done,@"new-stage")==nil);
    done=[done arrayByAddingObject:@{@"Stage":@"new-stage",@"Seconds":@5}];
    assert(VIMHostInstallTimingMatch(history,done,@"catalog")!=nil);
    assert([VIMHostInstallTimingMatch(history,done,@"convert")[@"FutureSeconds"] doubleValue]==0);
    assert(VIMHostInstallTimingMatch(history,@[@{@"Stage":@"payload",@"Seconds":@110}],@"start")==nil);
    assert(VIMHostInstallTimingMatch(nil,done,@"payload")==nil);
    NSDictionary *fallback=VIMHostInstallTimingBudget(history,@[@{@"Stage":@"start",@"Seconds":@4}],@"new-stage",120);
    assert([fallback[@"StageSeconds"] doubleValue]==120&&[fallback[@"Unmeasured"] boolValue]);
    assert(fabs([fallback[@"FutureSeconds"] doubleValue]-140.2)<1e-9);
    assert([VIMHostInstallTimingBudget(nil,done,@"new-stage",60)[@"StageSeconds"] doubleValue]==60);
    assert(VIMHostInstallTimingPrior(model,@"unmeasured-storage",@"mavericks",YES)!=nil);
    assert(VIMHostInstallTimingPrior(model,@"arm64-apfs",@"tahoe",YES)==nil);
    assert(VIMHostInstallTimingPrior(nil,@"arm64-apfs",@"mavericks",YES)==nil);
    assert(VIMHostInstallTimingPrior(@{@"Guests":@"invalid"},@"arm64-apfs",@"mavericks",YES)==nil);
    assert(VIMHostInstallTimingStages(@"invalid",1,YES)==nil);
    assert(VIMHostInstallTimingStages(@[@"invalid",@{@"Stage":@"bad"},@{@"Stage":@"negative",@"Seconds":@-2}],1,YES)==nil);
    NSDictionary *scales=@{@"Scales":@{@"Intel":@2,@"ExFAT":@3}};
    assert(VIMHostInstallTimingScale(scales,@"arm64-apfs")==1);
    assert(VIMHostInstallTimingScale(scales,@"x86_64-exfat")==6);
    assert(VIMHostInstallTimingScale(@{@"Scales":@"bad"},@"x86_64-exfat")==1);
    assert(VIMHostInstallTimingScale(@{@"Scales":@{@"Intel":@-1}},@"x86_64-apfs")==1);
    assert(VIMHostInstallTimingTotal(nil)==-1);
    // Whole-creation ETA includes future installation while files download.
    assert(VIMCreationRemainingSeconds(YES,NO,YES,200,1000,100,300)==308);
    assert(VIMCreationRemainingSeconds(YES,NO,YES,1000,1000,0,300)==300);
    assert(VIMCreationRemainingSeconds(YES,NO,YES,1000,1000,100,300)==300);
    assert(VIMCreationRemainingSeconds(YES,YES,YES,200,1000,100,300)==8);
    assert(VIMCreationRemainingSeconds(YES,NO,YES,200,1000,0,300)==-1);
    assert(VIMCreationRemainingSeconds(YES,NO,NO,200,1000,100,300)==-1);
    assert(VIMCreationRemainingSeconds(YES,NO,YES,200,1000,100,-1)==-1);
    NSUInteger runs=0,phases=0;
    for(NSString *profile in @[@"arm64-apfs",@"x86_64-apfs",@"arm64-exfat",@"x86_64-exfat"])
      for(NSString *guest in model[@"Guests"]) {
        NSArray *stages=VIMHostInstallTimingPrior(model,profile,guest,YES);
        NSArray *base=VIMHostInstallTimingPrior(model,profile,guest,NO);
        assert(stages.count&&base.count);
        NSMutableArray *observed=[NSMutableArray array];
        for(NSDictionary *stage in stages) {
            // Replay real cold phase histories, with every third short phase
            // absent from the observer. All later named phases still match.
            if(phases++%3==0&&[stage[@"Seconds"] doubleValue]<2)continue;
            NSDictionary *current=VIMHostInstallTimingMatch(stages,observed,stage[@"Stage"]);
            assert(current&&[current[@"FutureSeconds"] doubleValue]>=0);
            [observed addObject:stage];
        }
        for(NSDictionary *stage in base)
            assert([stage[@"Stage"] rangeOfString:@"-update"].location==NSNotFound);
        runs++;
    }
    printf("PASS %lu scaled baseline replays/%lu phases, optional timing failures, whole-creation ETA and skipped stages\n",
           (unsigned long)runs,(unsigned long)phases);
    return 0;
} }
