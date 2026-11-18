#import <Foundation/Foundation.h>
#import "../VirtualIntelMac/VIMOpenGLRestartEvidence.h"
#import "../tools/opengl-support-revision.h"
#include <assert.h>
int main(void){@autoreleasepool {
    NSUserDefaults *preferences=[[NSUserDefaults alloc] initWithSuiteName:NSUUID.UUID.UUIDString];
    [preferences setVolatileDomain:@{@"VIMDevelopmentAutoRestartGraphicsSupport":@NO} forName:NSArgumentDomain];
    assert(!VIMGraphicsSetupRestartsAutomatically(NO,preferences));
    assert(VIMGraphicsSetupRestartsAutomatically(YES,preferences));
    [preferences setVolatileDomain:@{@"VIMDevelopmentAutoRestartGraphicsSupport":@YES} forName:NSArgumentDomain];
    assert(VIMGraphicsSetupRestartsAutomatically(NO,preferences));
    [preferences setVolatileDomain:@{@"VIMDevelopmentAutoRestartGraphicsSupport":@NO} forName:NSArgumentDomain];
    assert(!VIMGraphicsSetupRestartsAutomatically(NO,preferences));
    NSString *old=@"CFAD1CCA-49A1-450C-8559-81FEDA907D98",*now=@"00112233-4455-6677-8899-AABBCCDDEEFF";
    NSMutableDictionary *initial=[@{@"MachineIdentifier":now,@"VolumeUUID":old,
        @"KernelMatchesSystem":@YES,@"Graphics":@{}} mutableCopy];
    // The older Leopard status tool omits the separate authorization bit.
    assert(VIMOpenGLInitialGraphicsCanResume(initial,now,old,YES,YES));
    assert(!VIMOpenGLInitialGraphicsCanResume(initial,old,old,YES,YES));
    assert(!VIMOpenGLInitialGraphicsCanResume(initial,now,now,YES,YES));
    assert(!VIMOpenGLInitialGraphicsCanResume(initial,now,old,NO,YES));
    assert(!VIMOpenGLInitialGraphicsCanResume(initial,now,old,YES,NO));
    initial[@"InstallationOwnershipVerified"]=@NO;
    assert(!VIMOpenGLInitialGraphicsCanResume(initial,now,old,YES,YES));
    initial[@"InstallationOwnershipVerified"]=@YES;
    assert(VIMOpenGLInitialGraphicsCanResume(initial,now,old,YES,YES));
    initial[@"KernelMatchesSystem"]=@NO;
    assert(!VIMOpenGLInitialGraphicsCanResume(initial,now,old,YES,YES));
    initial[@"KernelMatchesSystem"]=@YES;
    initial[@"Graphics"]=@{@"Phase":@"installed"};
    assert(!VIMOpenGLInitialGraphicsCanResume(initial,now,old,YES,YES));
    NSMutableDictionary *graphics=[@{@"ChangedThisBoot":@YES,@"EngineSHA256":@"engine",@"BackendSHA256":@"backend"} mutableCopy];
    NSMutableDictionary *report=[@{@"MachineIdentifier":@"machine",@"VolumeUUID":@"volume",@"ProductBuildVersion":@"9L31a",@"Graphics":graphics} mutableCopy];
    NSDictionary *before=@{@"MachineIdentifier":@"machine",@"VolumeUUID":@"volume",@"ProductBuildVersion":@"9L31a",@"Graphics":[graphics copy]};
    NSDictionary *expected=@{@"EngineSHA256":@"engine",@"BackendSHA256":@"backend"};
    assert(!VIMOpenGLBundledSupportMatches(@"leopard",report,expected));
    report[@"GraphicsRestartEvidenceVersion"]=@1;
    assert(VIMOpenGLBundledSupportMatches(@"leopard",report,expected));
    [report removeObjectForKey:@"GraphicsRestartEvidenceVersion"];
    assert(VIMOpenGLBundledSupportMatches(@"tiger",report,expected));
    NSDictionary *versioned=@{@"GraphicsSupportRevision":@11,@"EngineSHA256":@"different rebuild",@"BackendSHA256":@"different rebuild"};
    assert(!VIMOpenGLBundledSupportMatches(@"lion",report,versioned)); // unversioned -> first versioned upgrade
    graphics[@"GraphicsSupportRevision"]=@10;
    assert(!VIMOpenGLBundledSupportMatches(@"lion",report,versioned));
    graphics[@"GraphicsSupportRevision"]=@11;
    assert(VIMOpenGLBundledSupportMatches(@"lion",report,versioned)); // no rebuild churn
    graphics[@"GraphicsSupportRevision"]=@12;
    assert(VIMOpenGLBundledSupportMatches(@"lion",report,versioned)); // no downgrade
    for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mavericks",@"highsierra"]){
        NSDictionary *bundled=@{@"GraphicsSupportRevision":@(VIMOpenGLSupportRevision(guest))};
        BOOL affected=[@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest];
        assert(VIMOpenGLBundledSupportMatches(guest,report,bundled)==!affected);
    }
    [graphics removeObjectForKey:@"GraphicsSupportRevision"];
    NSDictionary *ack=@{@"MachineIdentifier":@"machine",@"RequestState":@"acknowledged",@"FromBootSessionUUID":old,@"GuestReport":before};
    assert(VIMOpenGLGraphicsNeedsRestart(report,nil,now));
    assert(VIMOpenGLGraphicsNeedsRestart(report,ack,old));
    assert(!VIMOpenGLGraphicsNeedsRestart(report,ack,now));
    graphics[@"BackendSHA256"]=@"new backend";
    assert(VIMOpenGLGraphicsNeedsRestart(report,ack,now));
    graphics[@"BackendSHA256"]=@"backend";
    graphics[@"InstalledBootSessionUUID"]=now;
    assert(VIMOpenGLGraphicsNeedsRestart(report,ack,now));
    graphics[@"InstalledBootSessionUUID"]=old;
    assert(!VIMOpenGLGraphicsNeedsRestart(report,nil,now));
    puts("PASS development restart opt-in/default-off, owned first-install resume, same-boot pending change, acknowledged new boot, changed payload and stamped receipts");
    return 0;
}}
