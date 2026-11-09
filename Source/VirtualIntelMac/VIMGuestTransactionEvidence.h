#import <Foundation/Foundation.h>

// Apple's standalone packages can replace SystemVersion.plist and the boot
// cache in place, with no installer boot target. That pre-reboot report can
// select software video, but cannot authorize replacing any guest files.
static inline BOOL VIMPendingInPlaceUpdate(NSDictionary *observation,NSString *guest,NSDictionary *baseline) {
    if(!baseline||![observation[@"PrivilegedAgentVerified"] isEqual:@YES]||
       ![observation[@"Report"] isKindOfClass:NSDictionary.class])return NO;
    NSDictionary *report=observation[@"Report"];
    NSString *build=report[@"ProductBuildVersion"],*session=report[@"BootSessionUUID"];
    return [report[@"Guest"] isEqual:guest]&&[report[@"InstalledSystemCandidate"] isEqual:@YES]&&
        [report[@"LiveRoot"] isEqual:@YES]&&[report[@"KernelMatchesSystem"] isEqual:@NO]&&
        [report[@"VolumeUUID"] isEqual:baseline[@"VolumeUUID"]]&&
        [report[@"KernelBuild"] isEqual:baseline[@"PreviousBuild"]]&&
        [build isKindOfClass:NSString.class]&&build.length&&![build isEqual:baseline[@"PreviousBuild"]]&&
        [session isKindOfClass:NSString.class]&&[[NSUUID alloc] initWithUUIDString:session]!=nil;
}

// Pure evidence checks shared by update authorization and journal retirement.
// An old *verified* transaction is not an uncertain new write merely because
// a macOS update removed its legacy /var/tmp verifier. Never replay that write.
static inline BOOL VIMVerifiedInstallationSuperseded(NSDictionary *state,NSDictionary *report) {
    if(![state isKindOfClass:NSDictionary.class]||![report isKindOfClass:NSDictionary.class])return NO;
    NSString *identity=state[@"TransactionID"],*build=report[@"ProductBuildVersion"];
    NSDictionary *result=state[@"GuestResult"],*prepared=state[@"Preparation"];
    NSDictionary *verified=state[@"PostBootVerification"]?:state[@"PreviousPostBootVerification"];
    if(![state[@"FormatVersion"] isEqual:@1]||![identity isKindOfClass:NSString.class]||![[NSUUID alloc] initWithUUIDString:identity]||
       ![result isKindOfClass:NSDictionary.class]||![prepared isKindOfClass:NSDictionary.class]||
       ![prepared[@"GuestReport"] isKindOfClass:NSDictionary.class]||![verified isKindOfClass:NSDictionary.class])return NO;
    BOOL terminal=[@[@"graphics-verified",@"awaiting-reboot"] containsObject:state[@"Phase"]];
    BOOL readFailed=[state[@"Phase"] isEqual:@"outcome-unknown"]&&
        [state[@"ReconciliationError"] isKindOfClass:NSString.class]&&[state[@"ReconciliationError"] length]>0;
    if((!terminal&&!readFailed)||![result[@"Phase"] isEqual:@"awaiting-reboot"]||
       ![result[@"FilesPublished"] isEqual:@YES]||![result[@"RebootRequired"] isEqual:@YES]||
       ![result[@"TransactionID"] isEqual:identity]||![prepared[@"TransactionID"] isEqual:identity]||
       ![verified[@"TransactionID"] isEqual:identity]||![verified[@"VerifiedAfterReboot"] isEqual:@YES]||
       ![verified[@"MetalCommandVerified"] isEqual:@YES])return NO;
    if(![report[@"InstalledSystemCandidate"] isEqual:@YES]||![report[@"LiveRoot"] isEqual:@YES]||
       ![report[@"KernelMatchesSystem"] isEqual:@YES]||
       ![@[@"mojave",@"catalina",@"bigsur"] containsObject:report[@"Guest"]]||
       ![prepared[@"GuestReport"][@"Guest"] isEqual:report[@"Guest"]])return NO;
    for(NSString *key in @[@"VolumeUUID",@"BootSessionUUID",@"ProductBuildVersion"])
        if(![report[key] isKindOfClass:NSString.class]||![report[key] length])return NO;
    NSString *previous=verified[@"ProductBuildVersion"];
    return [previous isKindOfClass:NSString.class]&&previous.length&&![build isEqual:previous]&&
        [result[@"ProductBuildVersion"] isEqual:previous]&&[prepared[@"GuestReport"][@"ProductBuildVersion"] isEqual:previous]&&
        [verified[@"VolumeUUID"] isEqual:report[@"VolumeUUID"]]&&[result[@"VolumeUUID"] isEqual:report[@"VolumeUUID"]]&&
        [prepared[@"GuestReport"][@"VolumeUUID"] isEqual:report[@"VolumeUUID"]]&&
        [verified[@"BootSessionUUID"] isKindOfClass:NSString.class]&&[verified[@"BootSessionUUID"] length]>0&&
        ![verified[@"BootSessionUUID"] isEqual:report[@"BootSessionUUID"]];
}
