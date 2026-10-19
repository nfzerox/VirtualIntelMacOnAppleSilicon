#import <Foundation/Foundation.h>

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
