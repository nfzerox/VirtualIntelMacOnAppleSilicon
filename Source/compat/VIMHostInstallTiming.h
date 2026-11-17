#pragma once
#import <Foundation/Foundation.h>
#include <sys/mount.h>
#include <math.h>

// Optional presentation history, not an installer/media admission rule. The
// normal run scales one cold-acceptance baseline. Explicit measurement runs
// may save and reuse local observations. No machine identity is bundled. Missing or
// malformed estimates are ignored, never installer failures.
NS_INLINE NSString *VIMHostInstallTimingProfile(NSString *architecture,NSString *filesystem) {
    NSString *storage=filesystem.lowercaseString;
    if(![storage isEqual:@"exfat"])storage=@"apfs";
    return [NSString stringWithFormat:@"%@-%@",architecture,storage];
}
NS_INLINE NSString *VIMHostInstallTimingProfileAtURL(NSURL *attempt) {
    struct statfs filesystem;
    NSString *type=statfs(attempt.fileSystemRepresentation,&filesystem)?@"unknown":@(filesystem.f_fstypename);
#if defined(__arm64__)
    return VIMHostInstallTimingProfile(@"arm64",type);
#else
    return VIMHostInstallTimingProfile(@"x86_64",type);
#endif
}
NS_INLINE BOOL VIMHostInstallTimingHasUpdates(NSDictionary *request) {
    for(NSString *key in request)
        if([key hasSuffix:@"Updates"]&&[request[key] isKindOfClass:NSDictionary.class]&&[request[key] count])return YES;
    return NO;
}
NS_INLINE NSString *VIMHostInstallTimingKey(NSString *profile,NSString *guest,BOOL updates) {
    return [NSString stringWithFormat:@"VIMHostInstallDuration-DirectSparse3-%@-%@-%@",
        profile,updates?@"updated":@"base",guest?:@""];
}
NS_INLINE BOOL VIMHostInstallTimingMeasurementEnabled(NSUserDefaults *defaults) {
    return [defaults boolForKey:@"VIMMeasureInstallTimings"];
}
NS_INLINE double VIMHostInstallTimingLearnedDuration(NSUserDefaults *defaults,NSString *key) {
    return VIMHostInstallTimingMeasurementEnabled(defaults)?[defaults doubleForKey:key]:0;
}
NS_INLINE void VIMHostInstallTimingRecordDuration(NSUserDefaults *defaults,NSString *key,double seconds) {
    if(!VIMHostInstallTimingMeasurementEnabled(defaults))return;
    double previous=[defaults doubleForKey:key];
    [defaults setDouble:previous>0?.6*previous+.4*seconds:seconds forKey:key];
}
NS_INLINE double VIMHostInstallTimingScale(NSDictionary *model,NSString *profile) {
    id scales=model[@"Scales"];
    if(![scales isKindOfClass:NSDictionary.class])return 1;
    double scale=1;
    for(NSString *key in @[@"Intel",@"ExFAT"]) {
        BOOL use=[key isEqual:@"Intel"]?[profile hasPrefix:@"x86_64-"]:[profile hasSuffix:@"-exfat"];
        id value=scales[key];double factor=[value respondsToSelector:@selector(doubleValue)]?[value doubleValue]:0;
        if(use&&isfinite(factor)&&factor>0)scale*=factor;
    }
    return isfinite(scale)&&scale>0?scale:1;
}
NS_INLINE NSArray<NSDictionary *> *VIMHostInstallTimingStages(id raw,double scale,BOOL updates) {
    if(![raw isKindOfClass:NSArray.class])return nil;
    NSMutableArray *stages=[NSMutableArray array];
    for(id entry in raw) {
        if(![entry isKindOfClass:NSDictionary.class])continue;
        NSString *name=entry[@"Stage"];id value=entry[@"Seconds"];
        if(![name isKindOfClass:NSString.class]||!name.length||![value respondsToSelector:@selector(doubleValue)])continue;
        double seconds=[value doubleValue]*scale;
        if(!isfinite(seconds)||seconds<0)continue;
        if(!updates&&[name rangeOfString:@"-update"].location!=NSNotFound)continue;
        [stages addObject:@{@"Stage":name,@"Seconds":@(seconds)}];
    }
    return stages.count?stages:nil;
}
NS_INLINE NSArray<NSDictionary *> *VIMHostInstallTimingPrior(NSDictionary *model,NSString *profile,
                                                           NSString *guest,BOOL updates) {
    id guests=model[@"Guests"];
    if(![guests isKindOfClass:NSDictionary.class])return nil;
    return VIMHostInstallTimingStages(guests[guest],VIMHostInstallTimingScale(model,profile),updates);
}
NS_INLINE NSDictionary *VIMHostInstallTimingLoadModel(NSBundle *bundle) {
    NSURL *URL=[bundle URLForResource:@"HostInstallTimings" withExtension:@"plist"];
    return URL?[NSDictionary dictionaryWithContentsOfURL:URL]:nil;
}
NS_INLINE NSArray<NSDictionary *> *VIMHostInstallTimingHistory(NSDictionary *model,NSUserDefaults *defaults,
                                                            NSString *profile,NSString *guest,BOOL updates) {
    NSString *key=[VIMHostInstallTimingKey(profile,guest,updates) stringByAppendingString:@"-stages"];
    NSArray *local=VIMHostInstallTimingMeasurementEnabled(defaults)?
        VIMHostInstallTimingStages([defaults objectForKey:key],1,YES):nil;
    if(local.count)return local;
    return VIMHostInstallTimingPrior(model,profile,guest,updates);
}
NS_INLINE double VIMHostInstallTimingTotal(NSArray<NSDictionary *> *stages) {
    double total=0;
    for(NSDictionary *stage in stages)total+=[stage[@"Seconds"] doubleValue];
    return total>0?total:-1;
}
// Remaining time in the creation window covers download AND preparation.
// An unavailable byte count/rate must not masquerade as a zero-second download.
NS_INLINE double VIMCreationRemainingSeconds(BOOL hostInstall,BOOL downloadOnly,
    BOOL known,int64_t received,int64_t total,double bytesPerSecond,double preparation) {
    if(!known||total<received||received<0)return -1;
    double download=0;
    if(total>received) {
        if(!(bytesPerSecond>0))return -1;
        download=(total-received)/bytesPerSecond;
    }
    if(downloadOnly)return download;
    if(!hostInstall||preparation<0)return -1;
    return download+preparation;
}

// The UI samples progress, so fast phases can be absent from either history.
// Match named phases in forward order, rather than requiring an exact prefix.
// Unknown work remains unknown until the worker reaches another known phase.
NS_INLINE NSDictionary *VIMHostInstallTimingMatch(NSArray<NSDictionary *> *history,
                                                 NSArray<NSDictionary *> *observed,NSString *current) {
    NSUInteger cursor=0;
    double reference=0,actual=0;
    for(NSDictionary *done in observed) {
        NSUInteger index=cursor;
        while(index<history.count&&![history[index][@"Stage"] isEqual:done[@"Stage"]])index++;
        if(index==history.count)continue;
        reference+=[history[index][@"Seconds"] doubleValue];actual+=[done[@"Seconds"] doubleValue];
        cursor=index+1;
    }
    NSUInteger index=cursor;
    while(index<history.count&&![history[index][@"Stage"] isEqual:current])index++;
    if(index==history.count)return nil;
    double future=0;
    for(NSUInteger i=index+1;i<history.count;i++)future+=[history[i][@"Seconds"] doubleValue];
    return @{@"StageSeconds":history[index][@"Seconds"],@"FutureSeconds":@(future),
        @"ReferenceCompletedSeconds":@(reference),@"ObservedCompletedSeconds":@(actual)};
}

// Unmeasured work gets one scaled minute plus the known remaining suffix.
// The assumed duration is NOT evidence for the observed-pace multiplier.
NS_INLINE NSDictionary *VIMHostInstallTimingBudget(NSArray<NSDictionary *> *history,
    NSArray<NSDictionary *> *observed,NSString *current,double fallbackSeconds) {
    NSDictionary *known=VIMHostInstallTimingMatch(history,observed,current);
    if([known[@"StageSeconds"] doubleValue]>0)return known;
    if(known) {
        NSMutableDictionary *budget=[known mutableCopy];budget[@"StageSeconds"]=@(fallbackSeconds);budget[@"Unmeasured"]=@YES;
        return budget;
    }
    NSUInteger cursor=0;
    for(NSDictionary *done in observed) {
        NSUInteger index=cursor;
        while(index<history.count&&![history[index][@"Stage"] isEqual:done[@"Stage"]])index++;
        if(index<history.count)cursor=index+1;
    }
    double future=history.count?0:-1;
    for(NSUInteger i=cursor;i<history.count;i++)future+=[history[i][@"Seconds"] doubleValue];
    return @{@"StageSeconds":@(fallbackSeconds),@"FutureSeconds":@(future),@"Unmeasured":@YES};
}
