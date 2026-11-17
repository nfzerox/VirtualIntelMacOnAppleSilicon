#import <Foundation/Foundation.h>
#import "VIMGuestInstallation.h"
@interface FakeInstallationAgent:NSObject
@property NSMutableArray *calls;
@property NSMutableArray *paths;
@property NSArray *replies;
@end
@implementation FakeInstallationAgent
- (void)runTemporaryTool:(NSURL *)tool arguments:(NSArray *)args completion:(void (^)(NSDictionary *,NSError *))completion {
    NSCAssert([tool.lastPathComponent isEqual:@"guest-install"],@"Use the current bundled native verifier");
    [self runExecutable:@"current-bundled-guest-install" arguments:args timeout:0 completion:completion];
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)args timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    NSUInteger index=self.calls.count;[self.calls addObject:args.firstObject];
    [self.paths addObject:path];
    NSCAssert(![args.firstObject isEqual:@"--apply"],@"Reconciliation must never reinstall");
    NSDictionary *value=self.replies[index];
    if(value[@"exitcode"]){completion(value,nil);return;}
    completion(@{@"exitcode":@0,@"stdout":[NSJSONSerialization dataWithJSONObject:value options:0 error:NULL]},nil);
}
@end
int main(void){@autoreleasepool {
    NSString *identity=NSUUID.UUID.UUIDString;
    NSString *path=@"/private/var/tmp/VirtualIntelMac.test/InstallTransaction.plist";
    NSURL *journal=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".plist"]]];
    NSDictionary *initial=@{@"FormatVersion":@1,@"TransactionID":identity,@"GuestJournal":path,
        @"Preparation":@{@"TransactionID":identity,@"TransactionJournal":path}};
    NSDictionary *status=@{@"TransactionID":identity,@"Phase":@"awaiting-reboot",@"RebootRequired":@YES,
        @"ProductBuildVersion":@"19H15",@"VolumeUUID":@"volume",@"BootSessionUUID":@"before"};
    NSDictionary *verified=@{@"TransactionID":identity,@"VerifiedAfterReboot":@YES,@"MetalCommandVerified":@YES,
        @"ProductBuildVersion":@"19H15",@"VolumeUUID":@"volume",@"BootSessionUUID":@"after",@"Ready":@NO};
    for(NSUInteger test=0;test<7;test++) {
        NSMutableDictionary *record=[initial mutableCopy];
        if(test==5) {
            NSString *durable=@"/private/var/db/VirtualIntelMac.test/InstallTransaction.plist";
            record[@"GuestJournal"]=durable;record[@"Preparation"]=@{@"TransactionID":identity,@"TransactionJournal":durable};
        }
        NSMutableDictionary *response=[verified mutableCopy];
        if(test==2)response[@"BootSessionUUID"]=@"before";
        if(test==3)record[@"GuestJournal"]=@"/tmp/untrusted/InstallTransaction.plist";
        [record writeToURL:journal atomically:YES];
        FakeInstallationAgent *agent=[FakeInstallationAgent new];agent.calls=[NSMutableArray new];
        NSDictionary *rolledBack=@{@"TransactionID":identity,@"Phase":@"rolled-back",@"Installed":@NO};
        agent.replies=@[test==4?@{@"TransactionID":@"wrong"}:test==6?rolledBack:status,response];
        __block BOOL finished=NO;
        [VIMGuestInstallation reconcileWithAgent:(id)agent journal:journal verifyAfterReboot:test!=0 completion:^(NSDictionary *result,NSError *error){
            NSCAssert((error!=nil)==(test>=2&&test<5),@"Unexpected verification result");finished=YES;
        }];
        NSCAssert(finished,@"Missing callback");
        NSCAssert(agent.calls.count==(test==3?0:(test==0||test==4||test==6?1:2)),@"Unexpected commands");
        NSDictionary *saved=[NSDictionary dictionaryWithContentsOfURL:journal];
        NSCAssert(![saved[@"Ready"] boolValue],@"Cannot infer installation readiness from GPU test alone");
        if(test==1)NSCAssert([saved[@"Phase"] isEqual:@"graphics-verified"],@"Missing verification state");
        if(test==6)NSCAssert([saved[@"Phase"] isEqual:@"rolled-back"]&&!saved[@"PostBootVerification"]&&!saved[@"ReconciliationError"],
            @"A confirmed rollback is not GPU verification or an unknown write");
    }
    NSURL *repairRoot=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:repairRoot withIntermediateDirectories:YES attributes:nil error:NULL];
    NSURL *repairJournal=[repairRoot URLByAppendingPathComponent:@"GuestInstallation.plist"];
    // An existing VM can retain a boot-policy verifier older than its newly
    // installed graphics transaction. It must not override the matching tool.
    NSURL *package=[repairRoot URLByAppendingPathComponent:@"Machine.vintelmac"];
    NSURL *stateDirectory=[package URLByAppendingPathComponent:@"State"];
    [NSFileManager.defaultManager createDirectoryAtURL:stateDirectory withIntermediateDirectories:YES attributes:nil error:NULL];
    [@{@"Guest":@"catalina",@"Identifier":@"machine"} writeToURL:[package URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];
    [@{@"Report":@{@"Guest":@"catalina",@"VolumeUUID":@"volume"}} writeToURL:[stateDirectory URLByAppendingPathComponent:@"GuestStatus.plist"] atomically:YES];
    NSString *compatibility=@"/private/var/db/VirtualIntelMac.old-policy/guest-install";
    [@{@"FormatVersion":@1,@"Guest":@"catalina",@"MachineIdentifier":@"machine",@"VolumeUUID":@"volume",@"Phase":@"ready",
        @"DispatchAttempted":@YES,@"GuestDirectory":compatibility.stringByDeletingLastPathComponent,
        @"Result":@{@"ProtocolVersion":@1,@"Guest":@"catalina",@"VolumeUUID":@"volume",@"PreferenceVerified":@YES,@"TaskForPIDRestrictionDisabled":@YES}}
        writeToURL:[stateDirectory URLByAppendingPathComponent:@"BootPolicy.plist"] atomically:YES];
    NSURL *migrationJournal=[stateDirectory URLByAppendingPathComponent:@"GuestInstallation.plist"];
    for(NSUInteger test=0;test<3;test++) {
        NSMutableDictionary *record=[initial mutableCopy];record[@"VerificationLog"]=@"Previous failure";record[@"VerificationExitCode"]=@65;
        [record writeToURL:migrationJournal atomically:YES];
        NSString *failure=test==1?@"Required guest AMFI boot policy is missing; restart using the app-provided EFI configuration\n":@"Installed targets differ from release policy\n";
        NSDictionary *rejected=@{@"exitcode":@65,@"stderr":[failure dataUsingEncoding:NSUTF8StringEncoding]};
        FakeInstallationAgent *agent=[FakeInstallationAgent new];agent.calls=[NSMutableArray new];agent.paths=[NSMutableArray new];
        agent.replies=test==0?@[status,verified]:@[status,rejected,verified];
        __block BOOL finished=NO;
        [VIMGuestInstallation reconcileWithAgent:(id)agent journal:migrationJournal verifyAfterReboot:YES completion:^(NSDictionary *result,NSError *error){
            NSCAssert((error!=nil)==(test==2),@"Only old AMFI admission may use compatibility verifier");finished=YES;
        }];
        NSCAssert(finished&&[agent.paths[1] isEqual:[path.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"guest-install"]],@"Transaction verifier runs first");
        NSCAssert(agent.calls.count==(test==1?3:2),@"No fallback on real graphics verification failure");
        if(test==1)NSCAssert([agent.paths[2] isEqual:compatibility],@"Use proven boot-policy migration helper only for old admission");
        NSDictionary *saved=[NSDictionary dictionaryWithContentsOfURL:migrationJournal];
        if(test<2)NSCAssert([saved[@"Phase"] isEqual:@"graphics-verified"]&&!saved[@"VerificationLog"]&&!saved[@"VerificationExitCode"],@"Successful observation clears obsolete failure labels");
        else NSCAssert([saved[@"VerificationLog"] isEqual:failure],@"Actual failure is retained");
    }
    [@{@"Guest":@"mojave",@"Identifier":@"machine"} writeToURL:[package URLByAppendingPathComponent:@"Machine.plist"] atomically:YES];
    for(NSUInteger test=0;test<2;test++) {
        [initial writeToURL:migrationJournal atomically:YES];
        FakeInstallationAgent *agent=[FakeInstallationAgent new];agent.calls=[NSMutableArray new];agent.paths=[NSMutableArray new];
        agent.replies=@[status,test?@{@"exitcode":@65,@"stderr":[@"Actual graphics failure" dataUsingEncoding:NSUTF8StringEncoding]}:verified];
        __block BOOL finished=NO;
        [VIMGuestInstallation reconcileWithAgent:(id)agent journal:migrationJournal verifyAfterReboot:YES completion:^(NSDictionary *result,NSError *error){
            finished=YES;NSCAssert((error!=nil)==(test!=0),@"Current Mojave verifier must preserve real failures");
        }];
        NSCAssert(finished&&agent.calls.count==2&&[agent.paths[1] isEqual:@"current-bundled-guest-install"],
            @"Mojave must not reuse an old verifier that hashes retired app preferences");
    }
    [initial writeToURL:repairJournal atomically:YES];
    NSMutableDictionary *terminal=[status mutableCopy];terminal[@"FilesPublished"]=@YES;
    FakeInstallationAgent *checking=[FakeInstallationAgent new];checking.calls=[NSMutableArray new];checking.replies=@[terminal];
    __block BOOL checked=NO;
    [VIMGuestInstallation checkForRepairWithAgent:(id)checking journal:repairJournal completion:^(NSError *error){NSCAssert(!error,@"Terminal repair check failed");checked=YES;}];
    NSCAssert(checked&&[NSFileManager.defaultManager fileExistsAtPath:repairJournal.path],@"Preparation check must retain display handoff evidence");
    NSCAssert(![NSFileManager.defaultManager fileExistsAtPath:[repairRoot URLByAppendingPathComponent:@"InstallationHistory"].path],@"Preparation check must not archive");
    for(NSUInteger test=0;test<3;test++) {
        [initial writeToURL:repairJournal atomically:YES];
        NSMutableDictionary *reply=[status mutableCopy];
        reply[@"FilesPublished"]=@YES;
        if(test==0)reply[@"Phase"]=@"publishing";
        if(test==1)reply[@"TransactionID"]=@"wrong";
        FakeInstallationAgent *agent=[FakeInstallationAgent new];agent.calls=[NSMutableArray new];agent.replies=@[reply];
        __block BOOL finished=NO;
        [VIMGuestInstallation archiveForRepairWithAgent:(id)agent journal:repairJournal completion:^(NSError *error){
            NSCAssert((error!=nil)==(test<2),@"Repair must reject active or mismatched transactions");finished=YES;
        }];
        NSCAssert(finished&&agent.calls.count==1,@"Repair must inspect exactly one status, never apply");
        NSCAssert([NSFileManager.defaultManager fileExistsAtPath:repairJournal.path]==(test<2),@"Only a confirmed terminal record may move");
    }
    NSDictionary *retained=[NSDictionary dictionaryWithContentsOfURL:[repairRoot URLByAppendingPathComponent:[NSString stringWithFormat:@"InstallationHistory/%@.plist",identity]]];
    NSCAssert([retained[@"Preparation"] isEqual:initial[@"Preparation"]],@"Retain full recovery provenance");
    [initial writeToURL:repairJournal atomically:YES];
    FakeInstallationAgent *repeat=[FakeInstallationAgent new];repeat.calls=[NSMutableArray new];repeat.replies=@[terminal];
    [VIMGuestInstallation archiveForRepairWithAgent:(id)repeat journal:repairJournal completion:^(NSError *error){NSCAssert(!error,@"Recovered history must not prevent repair");}];
    NSArray *history=[NSFileManager.defaultManager contentsOfDirectoryAtPath:[repairRoot URLByAppendingPathComponent:@"InstallationHistory"].path error:NULL];
    NSCAssert(history.count==2,@"Repeated archival must retain both records");
    NSMutableDictionary *oldVerified=[initial mutableCopy];
    oldVerified[@"GuestResult"]=terminal;oldVerified[@"PreviousPostBootVerification"]=verified;
    oldVerified[@"Preparation"]=@{@"TransactionID":identity,@"TransactionJournal":path,
        @"GuestReport":@{@"Guest":@"catalina",@"ProductBuildVersion":@"19H15",@"VolumeUUID":@"volume"}};
    oldVerified[@"Phase"]=@"outcome-unknown";oldVerified[@"ReconciliationError"]=@"Temporary verifier missing";
    NSDictionary *updated=@{@"Guest":@"catalina",@"InstalledSystemCandidate":@YES,@"LiveRoot":@YES,@"KernelMatchesSystem":@YES,
        @"VolumeUUID":@"volume",@"ProductBuildVersion":@"19H2036",@"BootSessionUUID":@"updated-boot"};
    FakeInstallationAgent *absent=[FakeInstallationAgent new];absent.calls=[NSMutableArray new];absent.replies=@[];
    [oldVerified writeToURL:repairJournal atomically:YES];
    [VIMGuestInstallation checkForRepairWithAgent:(id)absent journal:repairJournal updatedSystem:updated
        completion:^(NSError *error){NSCAssert(!error,@"Completed old-build transaction may be superseded");}];
    NSCAssert([NSFileManager.defaultManager fileExistsAtPath:repairJournal.path],@"Preparation must keep old proof");
    [VIMGuestInstallation archiveForRepairWithAgent:(id)absent journal:repairJournal updatedSystem:updated
        completion:^(NSError *error){NSCAssert(!error,@"Superseded verified transaction is archived intact");}];
    NSCAssert(absent.calls.count==0,@"No old guest verifier execution or repeated installation");
    NSCAssert(![NSFileManager.defaultManager fileExistsAtPath:repairJournal.path],@"Retire only the verified old-build record");
    [NSFileManager.defaultManager removeItemAtURL:repairRoot error:NULL];
    [NSFileManager.defaultManager removeItemAtURL:journal error:NULL];
    puts("PASS: status, postboot success, same-boot rejection, invalid journal, wrong transaction; no reapply or ready assertion");
}return 0;}
