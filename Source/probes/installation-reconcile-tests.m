#import <Foundation/Foundation.h>
#import "VIMGuestInstallation.h"
@interface FakeInstallationAgent:NSObject
@property NSMutableArray *calls;
@property NSArray *replies;
@end
@implementation FakeInstallationAgent
- (void)runExecutable:(NSString *)path arguments:(NSArray *)args timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    NSUInteger index=self.calls.count;[self.calls addObject:args.firstObject];
    NSCAssert(![args.firstObject isEqual:@"--apply"],@"Reconciliation must never reinstall");
    NSDictionary *value=self.replies[index];
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
    for(NSUInteger test=0;test<6;test++) {
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
        agent.replies=@[test==4?@{@"TransactionID":@"wrong"}:status,response];
        __block BOOL finished=NO;
        [VIMGuestInstallation reconcileWithAgent:(id)agent journal:journal verifyAfterReboot:test!=0 completion:^(NSDictionary *result,NSError *error){
            NSCAssert((error!=nil)==(test>=2&&test<5),@"Unexpected verification result");finished=YES;
        }];
        NSCAssert(finished,@"Missing callback");
        NSCAssert(agent.calls.count==(test==3?0:(test==0||test==4?1:2)),@"Unexpected commands");
        NSDictionary *saved=[NSDictionary dictionaryWithContentsOfURL:journal];
        NSCAssert(![saved[@"Ready"] boolValue],@"Cannot infer installation readiness from GPU test alone");
        if(test==1)NSCAssert([saved[@"Phase"] isEqual:@"graphics-verified"],@"Missing verification state");
    }
    NSURL *repairRoot=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString] isDirectory:YES];
    [NSFileManager.defaultManager createDirectoryAtURL:repairRoot withIntermediateDirectories:YES attributes:nil error:NULL];
    NSURL *repairJournal=[repairRoot URLByAppendingPathComponent:@"GuestInstallation.plist"];
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
