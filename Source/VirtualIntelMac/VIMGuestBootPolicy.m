#import "VIMGuestBootPolicy.h"
#import "VIMGuestAgent.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

static NSError *PolicyError(NSString *text){return [NSError errorWithDomain:@"VirtualIntelMac.BootPolicy" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];}
static BOOL GuestDirectory(NSString *path) {
    return [path isKindOfClass:NSString.class]&&[path hasPrefix:@"/private/var/db/VirtualIntelMac."]&&
        path.pathComponents.count==5&&[path isEqual:path.stringByStandardizingPath]&&path.lastPathComponent.length>16;
}
static BOOL SavePolicy(NSDictionary *record,NSURL *file,NSError **error) {
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:record format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
    if(!data||![data writeToURL:file options:NSDataWritingAtomic error:error])return NO;
    int fd=open(file.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    BOOL ok=fd>=0&&!fchmod(fd,0600)&&!fsync(fd)&&!fcntl(fd,F_FULLFSYNC);if(fd>=0)close(fd);
    if(ok) {
        int parent=open(file.URLByDeletingLastPathComponent.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        ok=parent>=0&&!fsync(parent);if(parent>=0)close(parent);
    }
    if(!ok&&error)*error=PolicyError(@"The guest boot-policy record could not be saved durably.");return ok;
}
@interface VIMGuestBootPolicy ()
@property VIMGuestAgent *agent;
@property NSDictionary *report;
@property NSURL *journal;
@property NSURL *tools;
@property NSMutableDictionary *state;
@property(copy) void (^completion)(BOOL,NSError *);
@end
@implementation VIMGuestBootPolicy
+ (NSString *)verifierAtURL:(NSURL *)journal guest:(NSString *)guest identifier:(NSString *)identifier volume:(NSString *)volume {
    NSDictionary *state=[NSDictionary dictionaryWithContentsOfURL:journal];
    if(![self readyAtURL:journal guest:guest identifier:identifier]||![state[@"VolumeUUID"] isEqual:volume])return nil;
    return [state[@"GuestDirectory"] stringByAppendingPathComponent:@"guest-install"];
}
+ (BOOL)readyAtURL:(NSURL *)journal guest:(NSString *)guest identifier:(NSString *)identifier {
    NSDictionary *state=[NSDictionary dictionaryWithContentsOfURL:journal],*result=state[@"Result"];
    NSDictionary *observed=[NSDictionary dictionaryWithContentsOfURL:[journal.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"GuestStatus.plist"]][@"Report"];
    if(![state isKindOfClass:NSDictionary.class]||![result isKindOfClass:NSDictionary.class]||![observed isKindOfClass:NSDictionary.class])return NO;
    return [state[@"FormatVersion"] isEqual:@1]&&[state[@"MachineIdentifier"] isEqual:identifier]&&
        [state[@"Guest"] isEqual:guest]&&[state[@"Phase"] isEqual:@"ready"]&&
        [state[@"DispatchAttempted"] isEqual:@YES]&&GuestDirectory(state[@"GuestDirectory"])&&
        [observed[@"Guest"] isEqual:guest]&&[observed[@"VolumeUUID"] isEqual:state[@"VolumeUUID"]]&&
        [result[@"PreferenceVerified"] isEqual:@YES]&&[result[@"TaskForPIDRestrictionDisabled"] isEqual:@YES]&&
        [result[@"VolumeUUID"] isEqual:state[@"VolumeUUID"]]&&[state[@"VolumeUUID"] isKindOfClass:NSString.class]&&
        [state[@"VolumeUUID"] length]>0&&[result[@"Guest"] isEqual:guest]&&[result[@"ProtocolVersion"] isEqual:@1];
}
+ (void)prepareWithAgent:(VIMGuestAgent *)agent report:(NSDictionary *)report identifier:(NSString *)identifier
                journal:(NSURL *)journal tools:(NSURL *)tools completion:(void (^)(BOOL,NSError *))completion {
    VIMGuestBootPolicy *operation=[self new];operation.agent=agent;operation.report=[report copy];operation.journal=journal;operation.tools=tools;operation.completion=completion;
    if(![@[@"mojave",@"catalina",@"bigsur"] containsObject:report[@"Guest"]]||
       ![report[@"InstalledSystemCandidate"] isEqual:@YES]||![report[@"LiveRoot"] isEqual:@YES]||
       ![report[@"KernelMatchesSystem"] isEqual:@YES]||!identifier.length) {
        completion(NO,PolicyError(@"Guest boot-policy preparation requires a matching installed system."));return;
    }
    for(NSString *key in @[@"VolumeUUID",@"ProductBuildVersion",@"BootSessionUUID"])
        if(![report[key] isKindOfClass:NSString.class]||![report[key] length]){completion(NO,PolicyError(@"The guest identity is incomplete."));return;}
    NSDictionary *prior=[NSDictionary dictionaryWithContentsOfURL:journal];
    if(prior) {
        // A different system disk needs its own explicit installation/repair;
        // never reuse an old volume's permission to write its replacement.
        if(![prior[@"FormatVersion"] isEqual:@1]||![prior[@"MachineIdentifier"] isEqual:identifier]||
           ![prior[@"Guest"] isEqual:report[@"Guest"]]||![prior[@"VolumeUUID"] isEqual:report[@"VolumeUUID"]]) {
            completion(NO,PolicyError(@"This Mac’s saved boot-policy record belongs to a different system. Its boot policy was not changed."));return;
        }
        operation.state=[prior mutableCopy];
        if(GuestDirectory(prior[@"GuestDirectory"])&&[prior[@"DispatchAttempted"] isEqual:@YES]) {
            operation.state[@"PreviousPhase"]=prior[@"Phase"]?:@"unknown";
            if(prior[@"Error"])operation.state[@"PreviousError"]=prior[@"Error"];
            if(prior[@"Result"])operation.state[@"PreviousResult"]=prior[@"Result"];
            operation.state[@"Phase"]=@"checking";
            NSError *error=nil;if(!SavePolicy(operation.state,journal,&error)){completion(NO,error);return;}
            [operation run:NO];return;
        }
        if([prior[@"DispatchAttempted"] isEqual:@YES]) {
            completion(NO,PolicyError(@"The previous compatibility change has an unknown outcome. It was not repeated."));return;
        }
    } else operation.state=[@{@"FormatVersion":@1,@"Attempt":NSUUID.UUID.UUIDString,@"MachineIdentifier":identifier,
        @"Guest":report[@"Guest"],@"VolumeUUID":report[@"VolumeUUID"],@"Phase":@"preparing",
        @"InitialIdentity":@{@"ProductBuildVersion":report[@"ProductBuildVersion"],@"BootSessionUUID":report[@"BootSessionUUID"]}} mutableCopy];
    [operation stage];
}
- (void)finish:(BOOL)ready error:(NSError *)error {
    if(error)self.state[@"Error"]=error.localizedDescription;else [self.state removeObjectForKey:@"Error"];
    self.state[@"Phase"]=ready?@"ready":([self.state[@"DispatchAttempted"] isEqual:@YES]?@"unknown":@"preflight-failed");
    self.state[@"UpdatedAt"]=NSDate.date;NSError *saveError=nil;
    if(!SavePolicy(self.state,self.journal,&saveError)){self.completion(NO,saveError);return;}
    self.completion(ready,error);
}
- (void)stage {
    self.state[@"Phase"]=@"preparing";NSError *error=nil;
    if(![NSFileManager.defaultManager createDirectoryAtURL:self.journal.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error]||
       !SavePolicy(self.state,self.journal,&error)){self.completion(NO,error);return;}
    NSDictionary *request=@{@"ProtocolVersion":@1,@"Guest":self.report[@"Guest"],@"VolumeUUID":self.report[@"VolumeUUID"],
        @"ProductBuildVersion":self.report[@"ProductBuildVersion"],@"BootSessionUUID":self.report[@"BootSessionUUID"]};
    NSURL *file=[self.journal.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"BootPolicyRequest.json"];
    NSData *data=[NSJSONSerialization dataWithJSONObject:request options:0 error:&error];
    if(!data||![data writeToURL:file options:NSDataWritingAtomic error:&error]){[self finish:NO error:error];return;}
    [self.agent stageInstallationFiles:@{@"guest-macho":[self.tools URLByAppendingPathComponent:@"guest-macho"],
        @"guest-install":[self.tools URLByAppendingPathComponent:@"guest-install"],@"BootPolicy.json":file}
        progress:nil completion:^(NSDictionary *receipt,NSError *failure){
        NSString *directory=receipt[@"Directory"];
        if(failure||!GuestDirectory(directory)){[self finish:NO error:failure?:PolicyError(@"The guest compatibility files could not be prepared.")];return;}
        self.state[@"GuestDirectory"]=directory;self.state[@"Transfer"]=receipt;
        [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",[directory stringByAppendingPathComponent:@"guest-install"]] timeout:10 completion:^(NSDictionary *reply,NSError *issue){
            if(issue||![reply[@"exitcode"] isEqual:@0]){[self finish:NO error:issue?:PolicyError(@"The compatibility helper could not be prepared.")];return;}
            [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0600",[directory stringByAppendingPathComponent:@"BootPolicy.json"]] timeout:10 completion:^(NSDictionary *privacy,NSError *privacyError){
                if(privacyError||![privacy[@"exitcode"] isEqual:@0]){[self finish:NO error:privacyError?:PolicyError(@"The guest compatibility request could not be secured.")];return;}
                [self run:YES];
            }];
        }];
    }];
}
- (void)run:(BOOL)configure {
    NSString *directory=self.state[@"GuestDirectory"];
    if(!GuestDirectory(directory)){[self finish:NO error:PolicyError(@"The guest compatibility record has an invalid path.")];return;}
    if(configure) {
        if([self.state[@"DispatchAttempted"] isEqual:@YES]){[self finish:NO error:PolicyError(@"A compatibility change was already dispatched. It was not repeated.")];return;}
        self.state[@"DispatchAttempted"]=@YES;self.state[@"Phase"]=@"dispatched";NSError *error=nil;
        if(!SavePolicy(self.state,self.journal,&error)){self.completion(NO,error);return;}
    }
    [self.agent runExecutable:[directory stringByAppendingPathComponent:@"guest-install"]
        arguments:@[configure?@"--configure-boot-policy":@"--boot-policy-status",[directory stringByAppendingPathComponent:@"BootPolicy.json"]]
        timeout:30 completion:^(NSDictionary *reply,NSError *failure){
        id result=[reply[@"exitcode"] isEqual:@0]&&!reply[@"signal"]?
            [NSJSONSerialization JSONObjectWithData:reply[@"stdout"]?:NSData.data options:0 error:NULL]:nil;
        BOOL valid=[result isKindOfClass:NSDictionary.class]&&[result[@"ProtocolVersion"] isEqual:@1]&&
            [result[@"Operation"] isEqual:@"guest-library-validation-preference"]&&
            [result[@"TaskForPIDRestrictionDisabled"] isEqual:@YES];
        for(NSString *key in @[@"Guest",@"VolumeUUID",@"ProductBuildVersion",@"BootSessionUUID"])
            valid=valid&&[result[key] isEqual:self.report[key]];
        if(failure||!valid){[self finish:NO error:failure?:PolicyError(@"Guest privacy compatibility could not be verified. The previous boot policy was retained.")];return;}
        if(!configure&&[result[@"PreferenceVerified"] isEqual:@NO]&&
           ([result[@"TransactionPhase"] isEqual:@"files-published"]||
            (!result[@"TransactionPhase"]&&[self.state[@"InitialResult"][@"Changed"] isEqual:@NO]))&&
           [self.state[@"PreviousPhase"] isEqual:@"ready"]&&
           [self.state[@"InitialResult"][@"PreferenceVerified"] isEqual:@YES]&&
           ![self.state[@"RecoveryBootSessionUUID"] isEqual:self.report[@"BootSessionUUID"]]) {
            // The old write conclusively completed (or adopted an already
            // correct preference without writing). This is a later change,
            // not permission to repeat an uncertain publication. Keep
            // its guest transaction and start one fresh, identity-bound repair.
            NSMutableArray *history=[self.state[@"RecoveryHistory"] mutableCopy]?:[NSMutableArray new];
            [history addObject:@{@"GuestDirectory":self.state[@"GuestDirectory"],
                @"ObservedChange":result,@"PriorAttempt":self.state[@"Attempt"]?:@""}];
            self.state[@"RecoveryHistory"]=history;
            self.state[@"RecoveryBootSessionUUID"]=self.report[@"BootSessionUUID"];
            self.state[@"Attempt"]=NSUUID.UUID.UUIDString;
            [self.state removeObjectForKey:@"DispatchAttempted"];
            [self.state removeObjectForKey:@"GuestDirectory"];
            [self.state removeObjectForKey:@"Result"];
            [self stage];return;
        }
        self.state[@"Result"]=result;
        if(configure&&!self.state[@"InitialResult"])self.state[@"InitialResult"]=result;
        [self finish:[result[@"PreferenceVerified"] isEqual:@YES] error:[result[@"PreferenceVerified"] isEqual:@YES]?nil:
            PolicyError(@"The guest compatibility preference is missing or changed. No previous write was repeated.")];
    }];
}
@end
