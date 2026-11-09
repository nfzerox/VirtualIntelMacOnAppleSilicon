#import "VIMModernSetup.h"
#import "VIMMachine.h"
#import "VIMGuestAgent.h"
#import "VIMAutomaticSetup.h"
static NSError *SetupError(NSString *text){return [NSError errorWithDomain:@"VirtualIntelMac.AutomaticSetup" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];}
@interface VIMModernSetup ()
@property VIMMachine *machine;
@property VIMGuestAgent *agent;
@property NSDictionary *profile;
@property NSDictionary *target;
@property NSMutableDictionary *state;
@property NSURL *journal;
@property NSString *helperDirectory;
@property NSString *helperBoot;
@end
@implementation VIMModernSetup
- (instancetype)initWithMachine:(VIMMachine *)machine agent:(VIMGuestAgent *)agent {
    if((self=[super init])){
        self.machine=machine;self.agent=agent;
        NSURL *folder=[machine.URL URLByAppendingPathComponent:@"State"];
        self.profile=[NSDictionary dictionaryWithContentsOfURL:[folder URLByAppendingPathComponent:@"AutomaticSetup.plist"]];
        self.target=[NSDictionary dictionaryWithContentsOfURL:[folder URLByAppendingPathComponent:@"InstallationTarget.plist"]];
        self.journal=[folder URLByAppendingPathComponent:@"NativeSetup.plist"];
        self.state=[[NSDictionary dictionaryWithContentsOfURL:self.journal] mutableCopy]?:[NSMutableDictionary new];
    }return self;
}
- (BOOL)savePhase:(NSString *)phase error:(NSError **)error {
    self.state[@"Phase"]=phase;self.state[@"MachineIdentifier"]=self.machine.configuration[@"Identifier"];
    self.state[@"TargetVolumeUUID"]=self.target[@"VolumeUUID"];
    self.state[@"FormatVersion"]=@1;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:self.state format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
    return data&&[data writeToURL:self.journal options:NSDataWritingAtomic error:error];
}
- (void)withHelpersForBoot:(NSString *)boot completion:(void (^)(NSError *))completion {
    if([self.helperBoot isEqual:boot]&&self.helperDirectory){completion(nil);return;}
    NSURL *tools=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/GuestTools"];
    [self.agent stageFiles:@{@"guest-native-installer":[tools URLByAppendingPathComponent:@"guest-native-installer"],
        @"guest-create-account":[tools URLByAppendingPathComponent:@"guest-create-account"]} progress:nil
        completion:^(NSDictionary *receipt,NSError *error){
        if(error){completion(error);return;}
        NSString *directory=receipt[@"Directory"];
        [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",[directory stringByAppendingPathComponent:@"guest-native-installer"],
            [directory stringByAppendingPathComponent:@"guest-create-account"]] timeout:0 completion:^(NSDictionary *mode,NSError *failure){
            if(!failure&&[mode[@"exitcode"] isEqual:@0]){self.helperDirectory=directory;self.helperBoot=boot;completion(nil);}
            else completion(failure?:SetupError(@"The automatic setup tools could not be prepared."));
        }];
    }];
}
- (NSDictionary *)requestForReport:(NSDictionary *)report {
    NSMutableDictionary *request=[@{@"FormatVersion":@1,@"MachineIdentifier":self.machine.configuration[@"Identifier"],@"Guest":self.machine.guest,
        @"VolumeUUID":report[@"VolumeUUID"],@"TargetVolumeUUID":self.target[@"VolumeUUID"],@"Username":self.profile[@"Username"],
        @"FileSharing":self.profile[@"FileSharing"],@"RemoteLogin":self.profile[@"RemoteLogin"],
        @"AutomaticLogin":self.profile[@"AutomaticLogin"]?:@YES} mutableCopy];
    NSDictionary *plan=self.state[@"InstallerPlan"];
    if([self.state[@"InstallDispatched"] isEqual:@YES]&&
       [plan[@"MachineIdentifier"] isEqual:request[@"MachineIdentifier"]]&&
       [plan[@"VolumeUUID"] isEqual:request[@"TargetVolumeUUID"]]){
        request[@"NativeInstallDispatched"]=@YES;
        if([[NSUUID alloc] initWithUUIDString:plan[@"TargetDiskUUID"]?:@""])request[@"TargetDiskUUID"]=plan[@"TargetDiskUUID"];
    }
    return request;
}
- (void)run:(NSString *)tool mode:(NSString *)mode request:(NSDictionary *)request completion:(void (^)(NSDictionary *,NSError *))completion {
    NSError *error=nil;NSData *input=[NSJSONSerialization dataWithJSONObject:request options:0 error:&error];
    if(!input){completion(nil,error);return;}
    [self.agent runExecutable:[self.helperDirectory stringByAppendingPathComponent:tool] arguments:@[mode] standardInput:input timeout:0 completion:completion];
}
- (void)advanceReport:(NSDictionary *)report completion:(void (^)(NSDictionary *,NSError *))completion {
    if(![self.profile[@"Enabled"] isEqual:@YES]||self.machine.preMetalGuest||[self.profile[@"Phase"] isEqual:@"complete"]){completion(@{@"Handled":@NO},nil);return;}
    NSString *identifier=self.machine.configuration[@"Identifier"],*target=self.target[@"VolumeUUID"],*boot=report[@"BootSessionUUID"];
    if(![self.profile[@"MachineIdentifier"] isEqual:identifier]||![self.profile[@"VolumeUUID"] isEqual:target]||
       ![[NSUUID alloc] initWithUUIDString:target?:@""]||![report[@"Guest"] isEqual:self.machine.guest]||
       ![report[@"LiveRoot"] isEqual:@YES]||![report[@"KernelMatchesSystem"] isEqual:@YES]||![[NSUUID alloc] initWithUUIDString:boot?:@""]||
       (self.state.count&&(![self.state[@"MachineIdentifier"] isEqual:identifier]||![self.state[@"TargetVolumeUUID"] isEqual:target]))){
        completion(nil,SetupError(@"This Mac’s automatic setup identity does not match its saved installation target."));return;
    }
    // A "failed" journal is only authoritative while the guest is still in
    // Recovery. startosinstall can print "Helper tool crashed..." and exit
    // non-zero after it has already staged the install and rebooted the guest.
    // Once the installed system has booted (no Recovery tools, installed root
    // present), that stale failure must not block account setup forever.
    if([self.state[@"Phase"] isEqual:@"failed"]&&![report[@"RecoveryToolsPresent"] isEqual:@YES]&&
       [report[@"InstalledRootPresent"] isEqual:@YES]){
        NSError *saveError=nil;[self.state removeObjectForKey:@"Error"];
        if(![self savePhase:@"install-prepared" error:&saveError]){completion(nil,saveError);return;}
    }
    if([self.state[@"Phase"] isEqual:@"failed"]){completion(nil,SetupError(self.state[@"Error"]?:@"Automatic installation could not finish. Its diagnostic files were retained."));return;}
    if([report[@"RecoveryToolsPresent"] isEqual:@YES]){
        if(self.state[@"InstallDispatched"]){
            // startosinstall staged the install, but the guest's own reboot
            // can still come back up in the installer media's Recovery; a
            // fresh restart then boots the staged installer. Only a later boot
            // running from that same Recovery volume is not progress. Big
            // Sur's staged installer also reports Recovery tools, but from its
            // own staged volume, and must be left to run.
            NSString *dispatched=self.state[@"DispatchBootSessionUUID"];
            NSString *recoveryVolume=self.state[@"DispatchRecoveryVolumeUUID"];
            if([dispatched isKindOfClass:NSString.class]&&![dispatched isEqual:boot]&&
               [recoveryVolume isKindOfClass:NSString.class]&&[recoveryVolume isEqual:report[@"VolumeUUID"]]&&
               ![self.state[@"RecoveryRestartBootSessionUUID"] isEqual:boot]) {
                NSUInteger attempts=[self.state[@"RecoveryRestarts"] unsignedIntegerValue];
                if(attempts>=2){
                    completion(nil,SetupError(@"The macOS installer did not start after restarting. Its prepared installation was kept; restart this Mac to try again."));return;
                }
                self.state[@"RecoveryRestarts"]=@(attempts+1);self.state[@"RecoveryRestartBootSessionUUID"]=boot;
                NSError *issue=nil;if(![self savePhase:self.state[@"Phase"] error:&issue]){completion(nil,issue);return;}
                if(self.progress)self.progress(@"Restarting into the macOS installer…");
                [self.agent requestRestartWithCompletion:^(NSError *ignored){
                    completion(@{@"Handled":@YES,@"RecoveryInstall":@YES,@"Message":@"Restarting into the macOS installer…"},nil);
                }];return;
            }
            completion(@{@"Handled":@YES,@"RecoveryInstall":@YES,@"Message":@"Installing macOS…"},nil);return;
        }
        [self withHelpersForBoot:boot completion:^(NSError *error){
            if(error){completion(nil,error);return;}
            NSMutableDictionary *request=[[self requestForReport:report] mutableCopy];request[@"VolumeUUID"]=target;
            [self run:@"guest-native-installer" mode:@"--plan" request:request completion:^(NSDictionary *reply,NSError *failure){
                id plan=[reply[@"exitcode"] isEqual:@0]?[NSJSONSerialization JSONObjectWithData:reply[@"stdout"]?:NSData.data options:0 error:NULL]:nil;
                if(failure||![plan isKindOfClass:NSDictionary.class]||![plan[@"VolumeUUID"] isEqual:target]){
                    NSString *detail=[[NSString alloc] initWithData:reply[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                    completion(nil,failure?:SetupError(detail.length?detail:@"The native installer could not confirm Macintosh HD."));return;
                }
                self.state[@"InstallDispatched"]=@YES;self.state[@"InstallerPlan"]=plan;
                self.state[@"DispatchBootSessionUUID"]=boot;
                if([report[@"VolumeUUID"] isKindOfClass:NSString.class])self.state[@"DispatchRecoveryVolumeUUID"]=report[@"VolumeUUID"];
                NSError *issue=nil;if(![self savePhase:@"install-dispatched" error:&issue]){completion(nil,issue);return;}
                request[@"LicenseAccepted"]=@YES;
                if(self.progress)self.progress(@"Preparing macOS installation…");
                NSTimeInterval previousPoll=self.agent.processPollInterval;
                self.agent.processPollInterval=5; // cold long-running installer, not 4 status RPCs/second under TCG
                [self run:@"guest-native-installer" mode:@"--start" request:request completion:^(NSDictionary *result,NSError *problem){
                    self.agent.processPollInterval=previousPoll;
                    // A real guest reboot closes this observer. Its durable
                    // dispatch record is intentionally not permission to retry.
                    if(problem){completion(@{@"Handled":@YES,@"RecoveryInstall":@YES,@"Message":@"Waiting for macOS installation to restart…"},nil);return;}
                    if(![result[@"exitcode"] isEqual:@0]){
                        NSString *detail=[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                        self.state[@"Error"]=detail.length?detail:@"The native macOS installer reported a failure.";
                        NSError *saveError=nil;[self savePhase:@"failed" error:&saveError];completion(nil,saveError?:SetupError(self.state[@"Error"]));return;
                    }
                    NSError *saveError=nil;[self savePhase:@"install-prepared" error:&saveError];
                    completion(@{@"Handled":@YES,@"RecoveryInstall":@YES,@"Message":@"Installing macOS…"},saveError);
                }];
            }];
        }];return;
    }
    if(![report[@"InstalledRootPresent"] isEqual:@YES]){completion(@{@"Handled":@YES,@"Message":@"Finishing macOS installation…"},nil);return;}
    BOOL finishing=[self.state[@"Phase"] isEqual:@"awaiting-first-login"]||[self.state[@"Phase"] isEqual:@"complete"];
    if(self.showAccountProgress&&(!finishing||[self.state[@"AccountBootSessionUUID"] isEqual:boot]))
        self.showAccountProgress([(self.profile[@"AutomaticLogin"]?:@YES) boolValue]);
    if([self.state[@"Phase"] isEqual:@"awaiting-first-login"]&&[self.state[@"AccountBootSessionUUID"] isEqual:boot]){
        completion(@{@"Handled":@YES,@"Restart":@YES,@"Message":@"Restarting to sign in…"},nil);return;
    }
    [self withHelpersForBoot:boot completion:^(NSError *error){
        if(error){completion(nil,error);return;}
        if(finishing&&![report[@"DesktopRunning"] isEqual:@YES]){completion(@{@"Handled":@YES,@"Message":@"Signing in to your virtual Mac…"},nil);return;}
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            NSError *issue=nil;NSString *password=finishing?@"":[VIMAutomaticSetup passwordForIdentifier:self.profile[@"CredentialIdentifier"] error:&issue];
            dispatch_async(dispatch_get_main_queue(),^{
                if(!password){completion(nil,issue?:SetupError(@"The saved setup password is unavailable on this Mac."));return;}
                NSMutableDictionary *request=[[self requestForReport:report] mutableCopy];request[@"Password"]=password;
                [self run:@"guest-create-account" mode:finishing?@"--finish-first-login":@"--create" request:request completion:^(NSDictionary *reply,NSError *failure){
                    if(!failure&&[reply[@"exitcode"] isEqual:@75]){completion(@{@"Handled":@YES,@"Message":@"Preparing your account and sharing…"},nil);return;}
                    id result=[reply[@"exitcode"] isEqual:@0]?[NSJSONSerialization JSONObjectWithData:reply[@"stdout"]?:NSData.data options:0 error:NULL]:nil;
                    if(failure||![result isKindOfClass:NSDictionary.class]){
                        NSString *detail=[[NSString alloc] initWithData:reply[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                        // The guest tool refuses (without touching anything) when the user already created an
                        // account or finished Setup Assistant by hand. That is not a failure: there is nothing
                        // left for automatic setup to do. Retire it so the session continues to graphics setup
                        // instead of showing this error forever and never installing the accelerated drivers.
                        if(!failure&&!finishing&&[detail containsString:@"already has an account or completed Setup Assistant"]){
                            NSMutableDictionary *profile=[self.profile mutableCopy];profile[@"Phase"]=@"complete";
                            NSURL *file=[self.machine.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"];
                            NSError *skipError=nil;
                            if([profile writeToURL:file error:&skipError]){
                                self.profile=profile;
                                [VIMAutomaticSetup removePasswordForIdentifier:profile[@"CredentialIdentifier"] error:NULL];
                                completion(@{@"Handled":@NO},nil);return;
                            }
                        }
                        completion(nil,failure?:SetupError(detail.length?detail:@"The automatic account setup did not finish. Its records were retained."));return;
                    }
                    NSError *saveError=nil;
                    if(finishing){
                        if(![result[@"Phase"] isEqual:@"complete"]){completion(nil,SetupError(@"The first desktop login could not be verified."));return;}
                        NSMutableDictionary *profile=[self.profile mutableCopy];profile[@"Phase"]=@"complete";
                        NSURL *file=[self.machine.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"];
                        if(![profile writeToURL:file error:&saveError]){completion(nil,saveError);return;}self.profile=profile;
                        [VIMAutomaticSetup removePasswordForIdentifier:profile[@"CredentialIdentifier"] error:NULL];
                        [self savePhase:@"complete" error:&saveError];completion(@{@"Handled":@NO},saveError);
                    }else{
                        if(![result[@"SetupCompleted"] isEqual:@YES]){completion(nil,SetupError(@"The automatic account handoff could not be confirmed."));return;}
                        self.state[@"AccountBootSessionUUID"]=boot;
                        if(![self savePhase:@"awaiting-first-login" error:&saveError]){completion(nil,saveError);return;}
                        completion(@{@"Handled":@YES,@"Restart":@YES,@"Message":@"Restarting to sign in…"},nil);
                    }
                }];
            });
        });
    }];
}
@end
