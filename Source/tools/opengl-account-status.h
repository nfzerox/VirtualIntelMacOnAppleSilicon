#pragma once
#import <Foundation/Foundation.h>

// The host needs setup state, not the complete private account journal.
// Host installation can include binary timezone data in that journal; it
// cannot be written as JSON. Never export credential material either.
static inline NSDictionary *VIMOpenGLAccountStatus(NSDictionary *journal) {
    NSMutableDictionary *status=[NSMutableDictionary dictionary];
    NSArray *keys=[NSArray arrayWithObjects:@"FormatVersion",@"Phase",@"MachineIdentifier",@"VolumeUUID",
        @"Username",@"UID",@"GeneratedUID",@"AutomaticLogin",
        @"HostStagedFileSharing",@"FileSharingAccountPrepared",@"FileSharingCredentialVersion",
        @"FileSharingConfigured",@"FileSharingDeferredUntilDesktop",@"RemoteLoginConfigured",
        @"FirstLoginPolicyPrepared",@"FirstLoginPolicyMechanism",@"FirstLoginPolicyBootSessionUUID",
        @"FirstLoginPolicyRetired",@"DesktopServicesVersion",@"InitialUserPreferencesPrepared",nil];
    for(NSUInteger index=0;index<[keys count];index++) {
        NSString *key=[keys objectAtIndex:index];
        id value=[journal objectForKey:key];
        if([value isKindOfClass:[NSString class]]||[value isKindOfClass:[NSNumber class]])
            [status setObject:value forKey:key];
    }
    return status;
}
