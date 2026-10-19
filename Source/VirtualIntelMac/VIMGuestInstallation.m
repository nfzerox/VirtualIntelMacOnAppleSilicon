#import "VIMGuestInstallation.h"
#import "VIMGuestAgent.h"
#import "VIMGuestTransactionEvidence.h"
#import "VIMGuestBootPolicy.h"
static BOOL InstallJournalPath(NSString *path) {
    return [path isKindOfClass:NSString.class]&&
        ([path hasPrefix:@"/private/var/tmp/VirtualIntelMac."]||[path hasPrefix:@"/private/var/db/VirtualIntelMac."])&&
        path.pathComponents.count==6&&![path.pathComponents containsObject:@".."]&&![path.pathComponents containsObject:@"."]&&
        [path.lastPathComponent isEqual:@"InstallTransaction.plist"];
}
static NSError *InstallError(NSString *text){return [NSError errorWithDomain:@"VirtualIntelMac.Installation" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];}
static NSDictionary *InstallJSON(NSDictionary *command) {
    if(![command[@"exitcode"] isEqual:@0]||command[@"signal"])return nil;
    id value=[NSJSONSerialization JSONObjectWithData:command[@"stdout"]?:NSData.data options:0 error:NULL];
    return [value isKindOfClass:NSDictionary.class]?value:nil;
}
@implementation VIMGuestInstallation
+ (void)checkForRepairWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)journal completion:(void (^)(NSError *))completion {
    [self checkForRepairWithAgent:agent journal:journal updatedSystem:nil completion:completion];
}
+ (void)checkForRepairWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)journal updatedSystem:(NSDictionary *)report completion:(void (^)(NSError *))completion {
    if(VIMVerifiedInstallationSuperseded([NSDictionary dictionaryWithContentsOfURL:journal],report)){completion(nil);return;}
    [self reconcileWithAgent:agent journal:journal verifyAfterReboot:NO completion:^(NSDictionary *result,NSError *failure){
        if(failure){completion(failure);return;}
        BOOL installed=[result[@"Phase"] isEqual:@"awaiting-reboot"]&&[result[@"FilesPublished"] isEqual:@YES]&&[result[@"RebootRequired"] isEqual:@YES];
        completion(installed||[result[@"Phase"] isEqual:@"rolled-back"]?nil:
            InstallError(@"The previous installation has not reached a recoverable stopping point. No repair was started."));
    }];
}
+ (void)archiveForRepairWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)journal completion:(void (^)(NSError *))completion {
    [self archiveForRepairWithAgent:agent journal:journal updatedSystem:nil completion:completion];
}
+ (void)archiveForRepairWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)journal updatedSystem:(NSDictionary *)report completion:(void (^)(NSError *))completion {
    NSDictionary *prior=[NSDictionary dictionaryWithContentsOfURL:journal];
    BOOL superseded=VIMVerifiedInstallationSuperseded(prior,report);
    void (^archive)(NSDictionary *,NSError *)=^(NSDictionary *result,NSError *failure){
        if(failure){completion(failure);return;}
        BOOL installed=[result[@"Phase"] isEqual:@"awaiting-reboot"]&&[result[@"FilesPublished"] isEqual:@YES]&&[result[@"RebootRequired"] isEqual:@YES];
        if(!installed&&![result[@"Phase"] isEqual:@"rolled-back"]) {
            completion(InstallError(@"The previous installation has not reached a recoverable stopping point. Its records were kept; no repair was started."));return;
        }
        NSDictionary *record=[NSDictionary dictionaryWithContentsOfURL:journal];
        NSString *identity=result[@"TransactionID"];
        if(![record[@"TransactionID"] isEqual:identity]||(superseded&&!VIMVerifiedInstallationSuperseded(record,report))) {
            completion(InstallError(@"The installation record changed while checking it. No repair was started."));return;
        }
        NSURL *history=[journal.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"InstallationHistory" isDirectory:YES];
        NSError *error=nil;
        NSFileManager *fm=NSFileManager.defaultManager;
        NSURL *destination=[history URLByAppendingPathComponent:[identity stringByAppendingPathExtension:@"plist"]];
        // Recovered host records may be checked again after staging failed.
        // Retain each observation, never overwrite the earlier provenance.
        if([fm fileExistsAtPath:destination.path])destination=[history URLByAppendingPathComponent:
            [[NSString stringWithFormat:@"%@-%@",identity,NSUUID.UUID.UUIDString] stringByAppendingPathExtension:@"plist"]];
        if(![fm createDirectoryAtURL:history withIntermediateDirectories:YES attributes:nil error:&error]||
           ![fm moveItemAtURL:journal toURL:destination error:&error]) {
            completion(error);return;
        }
        completion(nil);
    };
    // Preserve all original proof and errors in history. Do not invent a guest
    // status, re-run a missing old tool, or infer completion for a new attempt.
    if(superseded)archive(prior[@"GuestResult"],nil);
    else [self reconcileWithAgent:agent journal:journal verifyAfterReboot:NO completion:archive];
}
+ (void)reconcileWithAgent:(VIMGuestAgent *)agent journal:(NSURL *)journal verifyAfterReboot:(BOOL)verify completion:(void (^)(NSDictionary *,NSError *))completion {
    NSMutableDictionary *state=[[NSDictionary dictionaryWithContentsOfURL:journal] mutableCopy];
    NSString *path=state[@"GuestJournal"],*identity=state[@"TransactionID"];
    if(![state[@"FormatVersion"] isEqual:@1]||![identity isKindOfClass:NSString.class]||
       ![[NSUUID alloc] initWithUUIDString:identity]||
       !InstallJournalPath(path)||
       ![state[@"Preparation"] isKindOfClass:NSDictionary.class]||
       ![state[@"Preparation"][@"TransactionID"] isEqual:identity]||
       ![state[@"Preparation"][@"TransactionJournal"] isEqual:path]) {
        completion(nil,InstallError(@"The saved installation record is invalid. No guest command was sent."));return;
    }
    NSString *binary=[path.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"guest-install"];
    // Retain historical evidence separately, never present it as a fresh check
    // when the current guest cannot be reached or its state has changed.
    if(state[@"PostBootVerification"])state[@"PreviousPostBootVerification"]=state[@"PostBootVerification"];
    [state removeObjectForKey:@"PostBootVerification"];
    state[@"Phase"]=@"reconciling";
    void (^save)(NSDictionary *,NSError *)=^(NSDictionary *result,NSError *failure) {
        state[@"Ready"]=@NO;state[@"UpdatedAt"]=NSDate.date;
        if(failure)state[@"ReconciliationError"]=failure.localizedDescription;
        else [state removeObjectForKey:@"ReconciliationError"];
        NSError *error=nil;
        NSData *data=[NSPropertyListSerialization dataWithPropertyList:state format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
        if(!data||![data writeToURL:journal options:NSDataWritingAtomic error:&error]){completion(nil,error);return;}
        completion(result,failure);
    };
    [agent runExecutable:binary arguments:@[@"--status",path] timeout:30 completion:^(NSDictionary *command,NSError *failure) {
        NSDictionary *result=InstallJSON(command);
        if(![result[@"TransactionID"] isEqual:identity]) {
            state[@"Phase"]=@"outcome-unknown";
            if(command[@"exitcode"])state[@"ReconciliationExitCode"]=command[@"exitcode"];
            if([command[@"stderr"] length])state[@"ReconciliationLog"]=[[NSString alloc] initWithData:command[@"stderr"] encoding:NSUTF8StringEncoding]?:@"Non-text guest output";
            save(nil,failure?:InstallError(@"Cannot confirm the saved guest transaction. Installation was not retried."));return;
        }
        state[@"GuestResult"]=result;state[@"Phase"]=result[@"Phase"]?:@"outcome-unknown";
        if(!verify){save(result,nil);return;}
        if(![result[@"Phase"] isEqual:@"awaiting-reboot"]||![result[@"RebootRequired"] isEqual:@YES]) {
            save(result,InstallError([NSString stringWithFormat:@"Graphics installation is recorded as “%@”. Its recovery information has been kept; installation was not repeated.",result[@"Phase"]?:@"unknown"]));return;
        }
        // Early retained verifiers only recognize the blanket AMFI bypass.
        // A verified preference-first migration supplies a current read-only
        // verifier, without replacing the old journal/tools or reapplying any
        // graphics files. All original target/hash/build/volume checks remain.
        NSURL *package=journal.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent;
        NSDictionary *configuration=[NSDictionary dictionaryWithContentsOfURL:[package URLByAppendingPathComponent:@"Machine.plist"]];
        NSString *current=[VIMGuestBootPolicy verifierAtURL:[package URLByAppendingPathComponent:@"State/BootPolicy.plist"]
            guest:configuration[@"Guest"] identifier:configuration[@"Identifier"] volume:result[@"VolumeUUID"]];
        [agent runExecutable:current?:binary arguments:@[@"--verify-after-reboot",path] timeout:120 completion:^(NSDictionary *reply,NSError *error) {
            NSString *diagnostic=[[NSString alloc] initWithData:reply[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
            if(diagnostic.length)state[@"VerificationLog"]=diagnostic;
            if(reply[@"exitcode"])state[@"VerificationExitCode"]=reply[@"exitcode"];
            NSDictionary *evidence=InstallJSON(reply);
            BOOL valid=[evidence[@"TransactionID"] isEqual:identity]&&
                [evidence[@"VerifiedAfterReboot"] isEqual:@YES]&&[evidence[@"MetalCommandVerified"] isEqual:@YES]&&
                [evidence[@"ProductBuildVersion"] isEqual:result[@"ProductBuildVersion"]]&&
                [evidence[@"VolumeUUID"] isEqual:result[@"VolumeUUID"]]&&
                [evidence[@"BootSessionUUID"] isKindOfClass:NSString.class]&&[evidence[@"BootSessionUUID"] length]>0&&
                ![evidence[@"BootSessionUUID"] isEqual:result[@"BootSessionUUID"]];
            if(!valid){save(result,error?:InstallError(diagnostic.length?[NSString stringWithFormat:@"Graphics verification couldn’t finish: %@. The installer has been kept.",[diagnostic stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet]]:@"Graphics verification couldn’t finish. The installer has been kept; you can check again after the desktop is ready."));return;}
            state[@"PostBootVerification"]=evidence;state[@"Phase"]=@"graphics-verified";
            save(evidence,nil);
        }];
    }];
}
+ (void)applyWithAgent:(VIMGuestAgent *)agent preparation:(NSDictionary *)prepared journal:(NSURL *)journal completion:(void (^)(NSDictionary *,NSError *))completion {
    NSString *guestJournal=prepared[@"TransactionJournal"],*identity=prepared[@"TransactionID"];
    if(![prepared[@"Prepared"] isEqual:@YES]||![prepared[@"Planned"] isEqual:@YES]||![prepared[@"Installed"] isEqual:@NO]||
       ![identity isKindOfClass:NSString.class]||![[NSUUID alloc] initWithUUIDString:identity]||
       !InstallJournalPath(guestJournal)) {
        completion(nil,InstallError(@"No valid prepared installation transaction is available."));return;
    }
    NSMutableDictionary *state=[@{@"FormatVersion":@1,@"TransactionID":identity,@"GuestJournal":guestJournal,
        @"Preparation":prepared,@"Phase":@"dispatching",@"Ready":@NO,@"StartedAt":NSDate.date} mutableCopy];
    NSError *error=nil;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:state format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
    // Exclusive creation prevents a second invocation from silently retrying
    // after a lost reply or app crash. The caller must reconcile that journal.
    if(!data||![data writeToURL:journal options:NSDataWritingWithoutOverwriting error:&error]){completion(nil,error);return;}
    NSString *binary=[guestJournal.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"guest-install"];
    NSMutableArray *arguments=[@[@"--apply",guestJournal] mutableCopy];
    NSString *progressDirectory=prepared[@"GuestProgress"][@"Directory"];
    if(progressDirectory)[arguments addObjectsFromArray:@[@"--progress",progressDirectory]];
    // Do not abandon a healthy install on slow TCG hosts. The guest records
    // progress durably; real process/transport failures still reconcile it.
    [agent runExecutable:binary arguments:arguments timeout:0 completion:^(NSDictionary *command,NSError *failure) {
        if(failure)state[@"CommandError"]=failure.localizedDescription;
        if([command[@"stderr"] length])state[@"CommandLog"]=[[NSString alloc] initWithData:command[@"stderr"] encoding:NSUTF8StringEncoding]?:@"Non-text guest output";
        // Even nonzero exit/transport timeout can have a completed rollback or
        // a still-running operation. The durable guest result is authoritative.
        [agent runExecutable:binary arguments:@[@"--status",guestJournal] timeout:30 completion:^(NSDictionary *status,NSError *statusError) {
            NSDictionary *result=InstallJSON(status);
            if(![result[@"TransactionID"] isEqual:identity])result=nil;
            state[@"Phase"]=result[@"Phase"]?:@"outcome-unknown";
            if(result)state[@"GuestResult"]=result;
            state[@"UpdatedAt"]=NSDate.date;
            NSError *saveError=nil;NSData *saved=[NSPropertyListSerialization dataWithPropertyList:state format:NSPropertyListBinaryFormat_v1_0 options:0 error:&saveError];
            if(saved&&![saved writeToURL:journal options:NSDataWritingAtomic error:&saveError])saved=nil;
            if(!saved){completion(nil,saveError);return;}
            BOOL success=[result[@"Phase"] isEqual:@"awaiting-reboot"]&&[result[@"RebootRequired"] isEqual:@YES];
            completion(result,success?nil:(statusError?:failure?:InstallError(result[@"Failure"]?:@"Installation is not complete. Its saved state must be checked before retrying.")));
        }];
    }];
}
@end
