#import <Foundation/Foundation.h>
#import "../tools/opengl-account-status.h"
#import "../tools/opengl-graphics-status.h"

int main(void) { @autoreleasepool {
    NSDictionary *journal=@{@"Phase":@"autologin-armed",@"UID":@501,@"Username":@"mac",
        @"HostStagedFileSharing":@YES,@"FileSharingAccountPrepared":@YES,@"FileSharingCredentialVersion":@1,
        @"FirstLoginPolicyPrepared":@YES,@"FirstLoginPolicyBootSessionUUID":@"test-boot",
        @"DesktopServicesVersion":@2,@"HostLocale":@{@"TimeZoneData":[NSData dataWithBytes:"TZif" length:4]},
        @"AutoLoginSHA256":@"private-journal-only",@"CredentialIdentifier":@"private-journal-only",
        @"OtherInternalState":[NSDate date]};
    NSDictionary *status=VIMOpenGLAccountStatus(journal);
    for(NSString *key in @[@"Phase",@"UID",@"Username",@"HostStagedFileSharing",@"FileSharingAccountPrepared",
        @"FileSharingCredentialVersion",@"FirstLoginPolicyPrepared",@"FirstLoginPolicyBootSessionUUID",@"DesktopServicesVersion"])
        NSCAssert([status[key] isEqual:journal[key]],@"Keep host setup contract: %@",key);
    NSCAssert(!status[@"HostLocale"]&&!status[@"AutoLoginSHA256"]&&!status[@"CredentialIdentifier"]&&
        !status[@"OtherInternalState"],@"Private journal contents are not protocol fields");
    NSCAssert([NSJSONSerialization isValidJSONObject:status],@"Public status must serialize as JSON");
    NSData *wire=[NSJSONSerialization dataWithJSONObject:status options:0 error:NULL];
    NSCAssert([[NSJSONSerialization JSONObjectWithData:wire options:0 error:NULL] isEqual:status],@"Round-trip public status");
    NSCAssert([VIMOpenGLAccountStatus(nil) count]==0,@"An account journal need not exist yet");
    NSMutableDictionary *graphics=[journal mutableCopy];
    [graphics addEntriesFromDictionary:@{@"Phase":@"installed",@"ProductBuildVersion":@"8S2167",
        @"MachineIdentifier":@"test-machine",@"VolumeUUID":@"test-volume",
        @"GraphicsSupportRevision":@11,@"EngineSHA256":@"engine",@"BackendSHA256":@"backend",
        @"ChangedThisBoot":@NO,@"InstalledBootSessionUUID":@"test-boot"}];
    NSDictionary *evidence=VIMOpenGLGraphicsStatus(graphics);
    for(NSString *key in @[@"Phase",@"ProductBuildVersion",@"MachineIdentifier",@"VolumeUUID",
        @"GraphicsSupportRevision",@"EngineSHA256",@"BackendSHA256",@"ChangedThisBoot",@"InstalledBootSessionUUID"])
        NSCAssert([evidence[key] isEqual:graphics[key]],@"Keep graphics verification contract: %@",key);
    NSCAssert(!evidence[@"HostLocale"]&&!evidence[@"AutoLoginSHA256"]&&!evidence[@"Username"],
        @"Graphics status is not an account or locale journal");
    NSDictionary *report=@{@"Graphics":evidence,@"Account":status};
    wire=[NSJSONSerialization dataWithJSONObject:report options:0 error:NULL];
    NSCAssert(wire&&[[NSJSONSerialization JSONObjectWithData:wire options:0 error:NULL] isEqual:report],
        @"Host-installed graphics and account status must both serialize");
    NSCAssert([VIMOpenGLGraphicsStatus(nil) count]==0,@"Graphics need not be installed yet");
    puts("PASS account and graphics status preserve public evidence without binary locale data or private credential metadata");return 0;
} }
