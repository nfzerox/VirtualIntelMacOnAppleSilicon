#import <Foundation/Foundation.h>
#import "VIMGuestBootPolicy.h"
@interface PolicyAgent:NSObject
@property NSDictionary *report;
@property NSMutableArray *commands;
@property BOOL loseReply;
@property BOOL ready;
@property BOOL restoreOnConfigure;
@property NSString *transactionPhase;
@end
@implementation PolicyAgent
- (void)stageInstallationFiles:(NSDictionary *)files progress:(id)progress completion:(void (^)(NSDictionary *,NSError *))completion {
    [self.commands addObject:@"stage"];
    NSCAssert(files[@"guest-macho"]&&files[@"guest-install"]&&files[@"BootPolicy.json"],@"Authenticated native tools staged together");
    completion(@{@"Directory":@"/private/var/db/VirtualIntelMac.policy-test"},nil);
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)args timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    [self.commands addObject:args.firstObject];
    if([path isEqual:@"/bin/chmod"]){completion(@{@"exitcode":@0},nil);return;}
    if([args.firstObject isEqual:@"--configure-boot-policy"]&&self.loseReply) {
        completion(nil,[NSError errorWithDomain:@"fixture" code:1 userInfo:@{NSLocalizedDescriptionKey:@"Lost reply"}]);return;
    }
    if([args.firstObject isEqual:@"--configure-boot-policy"]&&self.restoreOnConfigure)self.ready=YES;
    NSMutableDictionary *value=[self.report mutableCopy];value[@"ProtocolVersion"]=@1;value[@"PreferenceVerified"]=@(self.ready);
    if(self.transactionPhase)value[@"TransactionPhase"]=self.transactionPhase;
    value[@"TaskForPIDRestrictionDisabled"]=@YES;value[@"Operation"]=@"guest-library-validation-preference";
    value[@"Changed"]=@([args.firstObject isEqual:@"--configure-boot-policy"]&&self.restoreOnConfigure);
    completion(@{@"exitcode":@0,@"stdout":[NSJSONSerialization dataWithJSONObject:value options:0 error:nil]},nil);
}
@end
int main(void){@autoreleasepool {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *base=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString]];
    [fm createDirectoryAtURL:base withIntermediateDirectories:NO attributes:nil error:nil];
    NSDictionary *report=@{@"Guest":@"mojave",@"InstalledSystemCandidate":@YES,@"LiveRoot":@YES,@"KernelMatchesSystem":@YES,
        @"VolumeUUID":@"volume",@"ProductBuildVersion":@"18G-new",@"BootSessionUUID":@"boot-one"};
    [@{@"Report":report} writeToURL:[base URLByAppendingPathComponent:@"GuestStatus.plist"] atomically:YES];
    NSURL *journal=[base URLByAppendingPathComponent:@"BootPolicy.plist"];
    PolicyAgent *agent=[PolicyAgent new];agent.commands=[NSMutableArray new];agent.report=report;agent.ready=YES;
    __block BOOL finished=NO;
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:report identifier:@"machine" journal:journal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(ready&&!error,@"First migration: %@",error);finished=YES;}];
    NSCAssert(finished&&[VIMGuestBootPolicy readyAtURL:journal guest:@"mojave" identifier:@"machine"],@"Only verified matching migration selects narrower firmware");
    NSCAssert(([[agent.commands filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF == %@",@"--configure-boot-policy"]] count]==1),@"Single native write dispatch");
    [agent.commands removeAllObjects];
    NSMutableDictionary *updated=[report mutableCopy];updated[@"BootSessionUUID"]=@"boot-two";updated[@"ProductBuildVersion"]=@"18G-later";agent.report=updated;
    [@{@"Report":updated} writeToURL:[base URLByAppendingPathComponent:@"GuestStatus.plist"] atomically:YES];
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:updated identifier:@"machine" journal:journal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(ready&&!error,@"New minor build checked read-only");}];
    NSCAssert([agent.commands isEqual:@[@"--boot-policy-status"]],@"No reapplication/re-staging on next boot or minor update");
    NSDictionary *checked=[NSDictionary dictionaryWithContentsOfURL:journal];
    NSCAssert([checked[@"InitialResult"][@"BootSessionUUID"] isEqual:@"boot-one"]&&[checked[@"Result"][@"BootSessionUUID"] isEqual:@"boot-two"],@"Original proof retained separately from the fresh observation");
    NSMutableDictionary *state=[[NSDictionary dictionaryWithContentsOfURL:journal] mutableCopy];state[@"Phase"]=@"unknown";[state writeToURL:journal atomically:YES];
    [agent.commands removeAllObjects];agent.ready=NO;
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:updated identifier:@"machine" journal:journal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(!ready&&error,@"Unknown outcome remains blocked if preference is absent");}];
    NSCAssert([agent.commands isEqual:@[@"--boot-policy-status"]]&&![VIMGuestBootPolicy readyAtURL:journal guest:@"mojave" identifier:@"machine"],@"Unknown write not repeated and legacy firmware retained");
    [agent.commands removeAllObjects];agent.ready=YES;
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:updated identifier:@"machine" journal:journal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(ready&&!error,@"Actual desired preference can recover readiness read-only");}];
    NSCAssert([agent.commands isEqual:@[@"--boot-policy-status"]],@"Recovery never reapplies");
    [agent.commands removeAllObjects];agent.ready=NO;agent.restoreOnConfigure=YES;agent.transactionPhase=nil;
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:updated identifier:@"machine" journal:journal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(ready&&!error,@"Known completed write followed by changed preference is repaired: %@",error);}];
    NSCAssert(([agent.commands isEqual:@[@"--boot-policy-status",@"stage",@"0700",@"0600",@"--configure-boot-policy"]]),@"Exactly one fresh repair, preserving the old transaction");
    NSDictionary *repaired=[NSDictionary dictionaryWithContentsOfURL:journal];
    NSCAssert([repaired[@"RecoveryHistory"] count]==1&&[repaired[@"RecoveryBootSessionUUID"] isEqual:@"boot-two"],@"Repair history and per-boot bound persisted");
    [agent.commands removeAllObjects];agent.ready=NO;
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:updated identifier:@"machine" journal:journal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(!ready&&error,@"Repeated tampering in one boot does not loop");}];
    NSCAssert([agent.commands isEqual:@[@"--boot-policy-status"]],@"No repeated writes in the same boot");
    agent.ready=YES;agent.restoreOnConfigure=NO;agent.transactionPhase=nil;
    NSURL *lostJournal=[base URLByAppendingPathComponent:@"Lost.plist"];
    agent.loseReply=YES;[agent.commands removeAllObjects];
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:updated identifier:@"machine" journal:lostJournal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(!ready&&error,@"Lost native reply remains unknown");}];
    NSDictionary *lost=[NSDictionary dictionaryWithContentsOfURL:lostJournal];
    NSCAssert([lost[@"DispatchAttempted"] isEqual:@YES]&&[lost[@"Phase"] isEqual:@"unknown"],@"Claim persisted before the uncertain command");
    agent.loseReply=NO;[agent.commands removeAllObjects];
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:updated identifier:@"machine" journal:lostJournal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(ready&&!error,@"Lost reply recovered through observed guest preference");}];
    lost=[NSDictionary dictionaryWithContentsOfURL:lostJournal];
    NSCAssert([agent.commands isEqual:@[@"--boot-policy-status"]]&&[lost[@"PreviousError"] isEqual:@"Lost reply"]&&!lost[@"InitialResult"],@"No second write or invented initial success; original error retained");
    NSCAssert(![VIMGuestBootPolicy readyAtURL:journal guest:@"catalina" identifier:@"machine"]&&![VIMGuestBootPolicy readyAtURL:journal guest:@"mojave" identifier:@"foreign"],@"Foreign guest/machine rejected");
    updated[@"VolumeUUID"]=@"another-volume";[@{@"Report":updated} writeToURL:[base URLByAppendingPathComponent:@"GuestStatus.plist"] atomically:YES];
    [agent.commands removeAllObjects];
    [VIMGuestBootPolicy prepareWithAgent:(id)agent report:updated identifier:@"machine" journal:journal tools:base completion:^(BOOL ready,NSError *error){NSCAssert(!ready&&error,@"Changed volume refused");}];
    NSCAssert(!agent.commands.count&&![VIMGuestBootPolicy readyAtURL:journal guest:@"mojave" identifier:@"machine"],@"No command or cached permission for replacement disk");
    [fm removeItemAtURL:base error:nil];
    puts("PASS: preference-first selection, known adopted-policy repair, per-boot repair bound, read-only minor-update/unknown recovery, changed-volume/machine/guest refusal");
}return 0;}
