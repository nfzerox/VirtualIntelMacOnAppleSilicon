#import "VIMOpenGLSetup.h"
#import "VIMMachine.h"
#import "VIMGuestAgent.h"
#import "VIMAutomaticSetup.h"
#import "VIMQMPConnection.h"
#import "VIMOpenGLSupportDisks.h"
#import "VIMOpenGLRestartEvidence.h"
#import <CommonCrypto/CommonDigest.h>
#include "../tools/opengl-installer-paths.h"
#include "../tools/opengl-system-version.h"
static NSString *const GuestTools=@"/Library/Application Support/Virtual Intel Mac/GuestTools";
static NSError *SetupError(NSString *message){return [NSError errorWithDomain:@"VirtualIntelMac.OpenGLSetup" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];}
static NSDictionary *BundledGraphics(NSURL *support,NSError **error) {
    NSMutableDictionary *digests=[NSMutableDictionary new];
    NSDictionary *names=@{@"EngineSHA256":@"GLEngine",@"BackendSHA256":@"libVIMPassthroughOpenGL.dylib"};
    for(NSString *key in names) {
        NSData *data=[NSData dataWithContentsOfURL:[support URLByAppendingPathComponent:names[key]] options:NSDataReadingMappedIfSafe error:error];
        if(!data.length||data.length>64*1024*1024){if(error&&!*error)*error=SetupError(@"This app’s graphics support is incomplete. Reinstall the app and try again.");return nil;}
        unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,(CC_LONG)data.length,digest);
        NSMutableString *text=[NSMutableString new];for(NSUInteger i=0;i<sizeof(digest);i++)[text appendFormat:@"%02x",digest[i]];
        digests[key]=text;
    }return digests;
}
@interface VIMOpenGLSetup ()
@property VIMMachine *machine;
@property VIMGuestAgent *agent;
@property VIMQMPConnection *control;
@property NSDictionary *profile;
@property NSString *bootUUID;
@property BOOL testedGraphics;
@property BOOL audioInitialized;
@property NSUInteger audioDeviceWaits;
@property BOOL audioDefaultsSkipped;
@property BOOL fileSharingChecked;
@property BOOL tigerGraphicsPrepared;
@property BOOL tigerAudioPrepared;
@property BOOL installerResumeRequested;
@property NSDictionary *bundledGraphics;
@property BOOL bundledGraphicsChecked;
@property BOOL initialGraphicsInstallAttempted;
@property BOOL payloadRestartRequired;
@property BOOL refreshingFirstLoginSupport;
@property BOOL updatingCompletedGuest;
@property NSURL *payloadMediaDirectory;
@property NSURL *payloadMedia;
@property NSString *payloadVolumeName;
@property BOOL payloadMediaInserted;
@property BOOL payloadMountRequested;
@property NSString *payloadDiskInfoTool;
@property(copy) NSString *resumedPassword;
- (void)stageTigerAudio:(void (^)(NSDictionary *,NSError *))completion;
- (void)loadTigerAudioAfterStaging:(BOOL)staged completion:(void (^)(NSDictionary *,NSError *))completion;
- (void)stageFileSharingReusingServer:(BOOL)reuseServer completion:(void (^)(NSDictionary *,NSError *))completion;
@end
@implementation VIMOpenGLSetup
- (void)requestInstallerResume {
    // Recovery retry is currently qualified against Snow Leopard's native
    // package installer. Normal installed/account-setup resumes are unchanged.
    self.installerResumeRequested=[self.machine.guest isEqual:@"snowleopard"]&&
        [self.machine.configuration[@"InstallState"] isEqual:@"installing"]&&[self.profile[@"Enabled"] boolValue];
}
- (void)resumeRecovery:(NSDictionary *)target completion:(void (^)(NSDictionary *,NSError *))completion {
    self.installerResumeRequested=NO;
    NSDictionary *journal=[NSDictionary dictionaryWithContentsOfURL:[self.machine.URL URLByAppendingPathComponent:@"State/Install.plist"]];
    NSString *machine=self.machine.configuration[@"Identifier"],*volume=target[@"VolumeUUID"];
    if(![self.machine.configuration[@"InstallState"] isEqual:@"installing"]||![self.profile[@"Enabled"] boolValue]||
       ![journal[@"Phase"] isEqual:@"awaiting-os-install"]||![journal[@"Identifier"] isEqual:machine]||
       ![[NSUUID alloc] initWithUUIDString:machine]||![[NSUUID alloc] initWithUUIDString:volume]){
        completion(nil,SetupError(@"This Mac does not have an interrupted automatic installation to resume. Its existing system was not changed."));return;
    }
    NSURL *support=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/OpenGLGuests/snowleopard/opengl-install-bootstrap"];
    [self.agent stageFiles:@{@"opengl-install-bootstrap":support} progress:nil completion:^(NSDictionary *receipt,NSError *failure){
        if(failure){completion(nil,failure);return;}
        NSString *tool=[receipt[@"Directory"] stringByAppendingPathComponent:@"opengl-install-bootstrap"];
        [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",tool] timeout:0 completion:^(NSDictionary *mode,NSError *issue){
            if(issue||![mode[@"exitcode"] isEqual:@0]){completion(nil,issue?:SetupError(@"The saved installation could not be prepared to resume."));return;}
            [self.agent runExecutable:tool arguments:@[@"--resume-recovery",machine,volume] timeout:0 completion:^(NSDictionary *result,NSError *error){
                if(error||![result[@"exitcode"] isEqual:@0]){
                    NSString *detail=[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                    completion(nil,error?:SetupError(detail.length?detail:@"The saved installation could not safely resume. Its existing files were retained."));return;
                }
                completion(@{@"Phase":@"recovery"},nil);
            }];
        }];
    }];
}
- (NSURL *)bundledSupport {
    return [NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:[@"Runtime/OpenGLGuests/" stringByAppendingString:self.machine.guest]];
}
- (void)checkBundledGraphics:(NSDictionary *)report completion:(void (^)(NSDictionary *,NSError *))completion {
    if(!self.bundledGraphics) {
        // Two small, owned libraries once per cold coordinator, never on the
        // desktop/frame/clipboard polling path. Reuse the guest's existing
        // recovery hashes; there is no new payload seal or generated manifest.
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
            NSError *error=nil;NSDictionary *expected=BundledGraphics(self.bundledSupport,&error);
            dispatch_async(dispatch_get_main_queue(),^{
                if(!expected){completion(nil,error);return;}
                self.bundledGraphics=expected;[self checkBundledGraphics:report completion:completion];
            });
        });return;
    }
    NSDictionary *graphics=report[@"Graphics"];
    BOOL installed=[graphics[@"Phase"] isEqual:@"installed"]&&[graphics[@"ProductBuildVersion"] isEqual:report[@"ProductBuildVersion"]];
    BOOL matches=VIMOpenGLBundledSupportMatches(self.machine.guest,report,self.bundledGraphics);
    self.updatingCompletedGuest=[self.machine.configuration[@"InstallState"] isEqual:@"installed"];
    // Let an interrupted El Capitan or Sierra install recover with an updated
    // app. Refresh the owned helper once through the existing support disc.
    self.refreshingFirstLoginSupport=matches&&[@[@"elcapitan",@"sierra"] containsObject:self.machine.guest]&&
        [self.machine.configuration[@"InstallState"] isEqual:@"patching"]&&[self.profile[@"AutomaticLogin"] boolValue]&&
        [report[@"Account"][@"Phase"] isEqual:@"autologin-armed"]&&[report[@"ConsoleUID"] isEqual:@0]&&
        ![report[@"LoginHandoffRecoveryVersion"] isEqual:@2];
    if(!installed||(matches&&!self.refreshingFirstLoginSupport)) {
        self.bundledGraphicsChecked=YES;[self advance:completion];return;
    }
    if(self.showProgress&&!self.updatingCompletedGuest&&![self.profile[@"Enabled"] boolValue]&&[report[@"DesktopRunning"] boolValue])
        self.showProgress(NO,NO);
    if([self.machine.guest isEqual:@"tiger"]){[self prepareTigerGraphics:completion];return;}
    [self refreshBundledSupport:completion];
}
- (void)refreshBundledSupport:(void (^)(NSDictionary *,NSError *))completion {
    if(!self.payloadMedia) {
        self.payloadVolumeName=[@"VIM GL " stringByAppendingString:[NSUUID.UUID.UUIDString substringToIndex:8]];
        NSURL *directory=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:[@"vim-opengl-update-" stringByAppendingString:NSUUID.UUID.UUIDString]] isDirectory:YES];
        self.payloadMediaDirectory=directory;
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
            NSError *error=nil;NSFileManager *fm=NSFileManager.defaultManager;
            NSURL *media=[directory URLByAppendingPathComponent:@"Support.dmg"],*log=[directory URLByAppendingPathComponent:@"Preparation.log"];
            BOOL ok=[fm createDirectoryAtURL:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:&error];
            NSFileHandle *handle=nil;
            if(ok)ok=[fm createFileAtPath:log.path contents:nil attributes:@{NSFilePosixPermissions:@0600}];
            if(ok)handle=[NSFileHandle fileHandleForWritingToURL:log error:&error];
            if(ok&&handle) {
                NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/usr/bin/hdiutil"];
                task.arguments=@[@"makehybrid",@"-hfs",@"-hfs-volume-name",self.payloadVolumeName,@"-o",media.path,self.bundledSupport.path];
                task.standardInput=NSFileHandle.fileHandleWithNullDevice;task.standardOutput=handle;task.standardError=handle;
                ok=[task launchAndReturnError:&error];if(ok){[task waitUntilExit];ok=task.terminationStatus==0;}[handle closeFile];
            }else ok=NO;
            dispatch_async(dispatch_get_main_queue(),^{
                if(!ok){completion(nil,error?:SetupError(@"The graphics update disc couldn’t be prepared. Its diagnostic files were retained."));return;}
                self.payloadMedia=media;[self refreshBundledSupport:completion];
            });
        });return;
    }
    if(!self.payloadMediaInserted) {
        [self.control execute:@"blockdev-change-medium" arguments:@{@"id":@"setup-support-device",@"filename":self.payloadMedia.path,@"format":@"raw",@"read-only-mode":@"read-only"}
            completion:^(id result,NSError *error){
                if(error){completion(nil,error);return;}
                self.payloadMediaInserted=YES;[self refreshBundledSupport:completion];
            }];return;
    }
    NSString *mount=[@"/Volumes" stringByAppendingPathComponent:self.payloadVolumeName];
    [self.agent runExecutable:@"/bin/test" arguments:@[@"-d",mount] timeout:0 completion:^(NSDictionary *result,NSError *error){
        if(error){completion(nil,error);return;}
        if(![result[@"exitcode"] isEqual:@0]) {
            [self mountPayloadSupport:completion];return;
        }
        NSString *bootstrap=[GuestTools stringByAppendingPathComponent:@"opengl-install-bootstrap"];
        // The installed helper may predate --refresh-support. Run the current
        // matching helper from our read-only disc, then use its freshly
        // installed copy for the real graphics transaction below.
        NSString *mediaBootstrap=[mount stringByAppendingPathComponent:@"opengl-install-bootstrap"];
        [self.agent runExecutable:mediaBootstrap arguments:@[@"--refresh-support",mount] timeout:0 completion:^(NSDictionary *copied,NSError *copyError){
            if(copyError||![copied[@"exitcode"] isEqual:@0]){completion(nil,copyError?:SetupError(@"The graphics update couldn’t be copied."));return;}
            // --refresh-support publishes files atomically but deliberately
            // does not install graphics or launch a second control agent.
            [self.agent runExecutable:bootstrap arguments:@[@"--install-support"] timeout:0 completion:^(NSDictionary *installed,NSError *installError){
                if(installError||![installed[@"exitcode"] isEqual:@0]){completion(nil,installError?:SetupError(@"The graphics update couldn’t finish."));return;}
                [self readStatus:^(NSDictionary *report,NSError *failure){
                    NSDictionary *graphics=report[@"Graphics"];
                    if(failure||![graphics[@"Phase"] isEqual:@"installed"]||![report[@"GraphicsRestartEvidenceVersion"] isEqual:@1]||
                       (self.refreshingFirstLoginSupport&&![report[@"LoginHandoffRecoveryVersion"] isEqual:@2])||
                       ![graphics[@"EngineSHA256"] isEqual:self.bundledGraphics[@"EngineSHA256"]]||
                       ![graphics[@"BackendSHA256"] isEqual:self.bundledGraphics[@"BackendSHA256"]]){
                        completion(nil,failure?:SetupError(@"The updated graphics files couldn’t be confirmed."));return;
                    }
                    self.bundledGraphicsChecked=YES;self.payloadRestartRequired=!self.refreshingFirstLoginSupport;self.testedGraphics=NO;
                    [self.agent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"eject",mount] timeout:0 completion:^(NSDictionary *ejected,NSError *ejectError){
                        if(ejectError||![ejected[@"exitcode"] isEqual:@0]){completion(nil,ejectError?:SetupError(@"Graphics support was updated, but its disc is still in use. Eject it normally before restarting."));return;}
                        [self.control execute:@"eject" arguments:@{@"id":@"setup-support-device"} completion:^(id ignored,NSError *removeError){
                            if(removeError){completion(nil,removeError);return;}
                            self.payloadMediaInserted=NO;
                            [NSFileManager.defaultManager removeItemAtURL:self.payloadMediaDirectory error:NULL];
                            [self advance:completion];
                        }];
                    }];
                }];
            }];
        }];
    }];
}
- (void)mountPayloadSupport:(void (^)(NSDictionary *,NSError *))completion {
    if(self.payloadMountRequested){completion(@{@"Phase":@"payload-media-wait"},nil);return;}
    [self.agent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"list",@"-plist"] timeout:0 completion:^(NSDictionary *reply,NSError *error){
        NSDictionary *list=[reply[@"exitcode"] isEqual:@0]?[NSPropertyListSerialization propertyListWithData:reply[@"stdout"]?:NSData.data options:NSPropertyListImmutable format:NULL error:NULL]:nil;
        if(error||![list isKindOfClass:NSDictionary.class]){completion(nil,error?:SetupError(@"The graphics update disc couldn’t be located. Nothing was installed."));return;}
        BOOL needsInfo=NO;NSArray *devices=VIMOpenGLSupportDiskCandidates(list,self.payloadVolumeName,&needsInfo);
        if(!devices){completion(nil,SetupError(@"The guest disk list was not recognized. Nothing was installed."));return;}
        if(needsInfo)[self inspectPayloadDevices:devices index:0 matches:[NSMutableArray new] completion:completion];
        else [self mountPayloadDevices:devices completion:completion];
    }];
}
- (void)inspectPayloadDevices:(NSArray<NSString *> *)devices index:(NSUInteger)index matches:(NSMutableArray<NSString *> *)matches
                  completion:(void (^)(NSDictionary *,NSError *))completion {
    if(index==devices.count){[self mountPayloadDevices:matches completion:completion];return;}
    if(!self.payloadDiskInfoTool) {
        // Native Leopard/Snow diskutil info can abort on optical media.
        // The current compiled helper uses their native Disk Arbitration API.
        // Stage it once before mounting, independent of old installed tools;
        // this cold metadata query is not a graphics/network transport path.
        NSURL *source=[self.bundledSupport URLByAppendingPathComponent:@"opengl-install-bootstrap"];
        [self.agent stageFiles:@{@"opengl-install-bootstrap":source} progress:nil completion:^(NSDictionary *receipt,NSError *error){
            if(error){completion(nil,error);return;}
            NSString *tool=[receipt[@"Directory"] stringByAppendingPathComponent:@"opengl-install-bootstrap"];
            [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",tool] timeout:0 completion:^(NSDictionary *mode,NSError *failure){
                if(failure||![mode[@"exitcode"] isEqual:@0]){completion(nil,failure?:SetupError(@"The graphics disc lookup couldn’t be prepared. Nothing was installed."));return;}
                self.payloadDiskInfoTool=tool;
                [self inspectPayloadDevices:devices index:index matches:matches completion:completion];
            }];
        }];return;
    }
    NSString *device=devices[index];
    [self.agent runExecutable:self.payloadDiskInfoTool arguments:@[@"--disk-info",device] timeout:0 completion:^(NSDictionary *reply,NSError *error){
        NSDictionary *info=[reply[@"exitcode"] isEqual:@0]?[NSPropertyListSerialization propertyListWithData:reply[@"stdout"]?:NSData.data options:NSPropertyListImmutable format:NULL error:NULL]:nil;
        if(error||![info isKindOfClass:NSDictionary.class]){completion(nil,error?:SetupError(@"The guest disk information couldn’t be read. Nothing was installed."));return;}
        if(VIMOpenGLSupportDiskInfoMatches(info,device,self.payloadVolumeName))[matches addObject:device];
        [self inspectPayloadDevices:devices index:index+1 matches:matches completion:completion];
    }];
}
- (void)mountPayloadDevices:(NSArray<NSString *> *)matches completion:(void (^)(NSDictionary *,NSError *))completion {
    if(!matches.count){completion(@{@"Phase":@"payload-media-wait"},nil);return;}
    NSString *disk=matches.firstObject;
    if(matches.count!=1||!VIMOpenGLSupportDiskIdentifier(disk)){completion(nil,SetupError(@"The graphics update disc could not be identified uniquely. Nothing was installed."));return;}
    self.payloadMountRequested=YES;
    [self.agent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"mount",disk] timeout:0 completion:^(NSDictionary *mounted,NSError *failure){
        if(failure||![mounted[@"exitcode"] isEqual:@0]){completion(nil,failure?:SetupError(@"The read-only graphics update disc couldn’t be mounted. Nothing was installed."));return;}
        [self refreshBundledSupport:completion];
    }];
}
- (BOOL)providePassword:(NSString *)password error:(NSError **)error {
    NSAssert(NSThread.isMainThread,@"Setup credentials are main-thread owned");
    if(![VIMAutomaticSetup validatePassword:password error:error])return NO;
    self.resumedPassword=password;return YES;
}
- (instancetype)initWithMachine:(VIMMachine *)machine agent:(VIMGuestAgent *)agent control:(VIMQMPConnection *)control {
    if((self=[super init])){
        self.machine=machine;self.agent=agent;self.control=control;
        self.profile=[NSDictionary dictionaryWithContentsOfURL:[machine.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"]];
    }return self;
}
- (void)prepareTigerGraphics:(void (^)(NSDictionary *,NSError *))completion {
    // Use the same private, root-owned control transfer as later
    // guests. Native10.4 executables only; no network relay or guest Xcode.
    NSURL *support=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/OpenGLGuests/tiger"];
    NSDictionary *relative=@{@"opengl-tiger-graphics":@"opengl-tiger-graphics",@"BuildReceipt.plist":@"Graphics.plist",
        @"GLEngine":@"GLEngine",@"libVIMPassthroughOpenGL.dylib":@"libVIMPassthroughOpenGL.dylib",
        @"GA-Info.plist":@"VMsvga2GA.plugin/Contents/Info.plist",
        @"VMsvga2GA":@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA"};
    NSMutableDictionary *files=[NSMutableDictionary new];
    for(NSString *name in relative)files[name]=[support URLByAppendingPathComponent:relative[name]];
    [self.agent stageInstallationFiles:files progress:nil completion:^(NSDictionary *receipt,NSError *failure){
        if(failure){completion(nil,failure);return;}
        NSString *directory=receipt[@"Directory"],*tool=[directory stringByAppendingPathComponent:@"opengl-tiger-graphics"];
        [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",tool] timeout:0 completion:^(NSDictionary *mode,NSError *issue){
            if(issue||![mode[@"exitcode"] isEqual:@0]){completion(nil,issue?:SetupError(@"Tiger’s native graphics setup couldn’t be prepared."));return;}
            [self.agent runExecutable:tool arguments:@[@"--install",directory] timeout:0 completion:^(NSDictionary *result,NSError *error){
                if(error||![result[@"exitcode"] isEqual:@0]){
                    NSString *detail=[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                    completion(nil,error?:SetupError(detail.length?detail:@"Tiger’s native graphics files couldn’t be verified. Its original files and setup records were retained."));return;
                }
                self.tigerGraphicsPrepared=YES;self.bundledGraphicsChecked=YES;
                self.payloadRestartRequired=YES;[self advance:completion];
            }];
        }];
    }];
}
- (void)prepareTigerAudio:(void (^)(NSDictionary *,NSError *))completion {
    [self.agent runExecutable:@"/bin/test" arguments:@[@"-x",[GuestTools stringByAppendingPathComponent:@"opengl-tiger-audio"]] timeout:0 completion:^(NSDictionary *reply,NSError *error){
        if(error){completion(nil,error);return;}
        if(![reply[@"exitcode"] isEqual:@0]){[self stageTigerAudio:completion];return;}
        [self loadTigerAudioAfterStaging:NO completion:completion];
    }];
}
- (void)loadTigerAudioAfterStaging:(BOOL)staged completion:(void (^)(NSDictionary *,NSError *))completion {
    [self tool:@"opengl-tiger-audio" arguments:@[@"--load"] input:nil completion:^(NSDictionary *result,NSError *error){
        if(!error&&[result[@"exitcode"] isEqual:@66]&&!staged){[self stageTigerAudio:completion];return;}
        if(!error&&[result[@"exitcode"] isEqual:@75]){completion(@{@"Phase":@"audio-busy"},nil);return;}
        if(!error&&[result[@"exitcode"] isEqual:@76]){completion(@{@"Phase":@"needs-restart"},nil);return;}
        if(error||![result[@"exitcode"] isEqual:@0]){
            NSString *detail=[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
            completion(nil,error?:SetupError(detail.length?detail:@"Tiger’s native sound driver couldn’t start. Its files and recovery records were retained."));return;
        }
        self.tigerAudioPrepared=YES;completion(@{@"Phase":@"audio-ready"},nil);
    }];
}
- (void)stageTigerAudio:(void (^)(NSDictionary *,NSError *))completion {
    NSURL *support=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/OpenGLGuests/tiger"];
    NSDictionary *files=@{@"opengl-tiger-audio":[support URLByAppendingPathComponent:@"opengl-tiger-audio"],
            @"opengl-audio-defaults":[support URLByAppendingPathComponent:@"opengl-audio-defaults"],
            @"ControlBuildReceipt.plist":[support URLByAppendingPathComponent:@"BuildReceipt.plist"],
            @"BuildReceipt.plist":[support URLByAppendingPathComponent:@"Audio/BuildReceipt.plist"],
            @"VoodooHDA":[support URLByAppendingPathComponent:@"Audio/VoodooHDA.kext/Contents/MacOS/VoodooHDA"],
            @"Info.plist":[support URLByAppendingPathComponent:@"Audio/VoodooHDA.kext/Contents/Info.plist"]};
        [self.agent stageInstallationFiles:files progress:nil completion:^(NSDictionary *receipt,NSError *failure){
            if(failure){completion(nil,failure);return;}
            NSString *directory=receipt[@"Directory"],*program=[directory stringByAppendingPathComponent:@"opengl-tiger-audio"];
            [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",program] timeout:0 completion:^(NSDictionary *mode,NSError *issue){
                if(issue||![mode[@"exitcode"] isEqual:@0]){completion(nil,issue?:SetupError(@"The native Tiger sound update couldn’t be prepared."));return;}
                [self.agent runExecutable:program arguments:@[@"--install",directory] timeout:0 completion:^(NSDictionary *result,NSError *error){
                    if(error||![result[@"exitcode"] isEqual:@0]){
                        NSString *detail=[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                        completion(nil,error?:SetupError(detail.length?detail:@"Tiger’s sound update couldn’t finish. Its recovery records were retained."));return;
                    }
                    [self loadTigerAudioAfterStaging:YES completion:completion];
                }];
            }];
        }];
}
- (void)tool:(NSString *)name arguments:(NSArray *)arguments input:(NSData *)input completion:(void (^)(NSDictionary *,NSError *))completion {
    [self.agent runExecutable:[GuestTools stringByAppendingPathComponent:name] arguments:arguments standardInput:input timeout:0 completion:completion];
}
- (void)logFailedVerification:(NSString *)name reply:(NSDictionary *)reply error:(NSError *)error {
    if(!self.logVerificationFailure)return;
    NSMutableString *message=[NSMutableString stringWithFormat:@"%@ exit=%@ error=%@",name,
        reply[@"exitcode"]?:@"unavailable",error?@(error.code):@0];
    for(NSString *stream in @[@"stdout",@"stderr"]){
        NSData *data=reply[stream];if(!data.length)continue;
        if(data.length>2048)data=[data subdataWithRange:NSMakeRange(0,2048)];
        NSString *detail=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if(detail.length)[message appendFormat:@"\n%@: %@",stream,detail];
    }
    self.logVerificationFailure(message);
}
- (void)runAccountToolForSharingRepair:(BOOL)repair input:(NSData *)input completion:(void (^)(NSDictionary *,NSError *))completion {
    if(!repair){[self tool:@"opengl-create-account" arguments:@[@"--create"] input:input completion:completion];return;}
    // An older installed helper predates this repair even when its graphics
    // hashes still match. Stage the app's verified, self-contained native
    // control instead of invoking a new argument on that stale executable.
    // Only the requested non-secret helper enters the private root-owned directory;
    // account credentials remain standard-input data, never staged files.
    NSURL *tools=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/OpenGLGuests/leopard"];
    [self.agent stageFiles:@{@"opengl-create-account":[tools URLByAppendingPathComponent:@"opengl-create-account"]} progress:nil
        completion:^(NSDictionary *receipt,NSError *failure){
        if(failure){completion(nil,failure);return;}
        NSString *program=[receipt[@"Directory"] stringByAppendingPathComponent:@"opengl-create-account"];
        [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",program] timeout:0 completion:^(NSDictionary *mode,NSError *issue){
            if(issue||![mode[@"exitcode"] isEqual:@0]){
                completion(nil,issue?:SetupError(@"The file-sharing update couldn’t be prepared. The existing account was not replaced."));return;
            }
            [self.agent runExecutable:program arguments:@[@"--prepare-file-sharing"] standardInput:input timeout:0 completion:completion];
        }];
    }];
}
- (void)resumeInitialGraphics:(void (^)(NSDictionary *,NSError *))completion {
    if(self.initialGraphicsInstallAttempted){
        completion(nil,SetupError(@"Graphics setup did not finish. Restart this Mac to try again."));return;
    }
    self.initialGraphicsInstallAttempted=YES;
    // Recovery already installed our helper on the authorized target. It
    // rechecks the root against its journal before replacing graphics files.
    [self tool:@"opengl-install-bootstrap" arguments:@[@"--install-support"] input:nil
        completion:^(NSDictionary *result,NSError *error){
        if(error||![result[@"exitcode"] isEqual:@0]){
            NSString *detail=[[NSString alloc] initWithData:result[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
            completion(nil,error?:SetupError(detail.length?detail:@"Graphics setup couldn’t finish. Restart this Mac to try again."));return;
        }
        [self advance:completion];
    }];
}
- (void)advance:(void (^)(NSDictionary *,NSError *))completion {
    NSAssert(NSThread.isMainThread,@"OpenGL setup shares the session's agent owner");
    if(!self.machine.preMetalGuest){completion(@{@"Phase":@"unsupported"},nil);return;}
    NSDictionary *target=[NSDictionary dictionaryWithContentsOfURL:[self.machine.URL URLByAppendingPathComponent:@"State/InstallationTarget.plist"]];
    if(!target){completion(@{@"Phase":@"unmanaged"},nil);return;}
    [self.agent connectWithCompletion:^(NSError *connection){
        if(connection){completion(@{@"Phase":@"booting"},nil);return;}
        NSString *installer=[self.machine.guest isEqual:@"tiger"]?@"/System/Installation/CDIS":
            [NSString stringWithUTF8String:VIMOpenGLRecoveryInstallerForGuest(self.machine.guest.UTF8String)];
        [self.agent runExecutable:@"/bin/test" arguments:@[[self.machine.guest isEqual:@"tiger"]?@"-d":@"-f",installer] timeout:0 completion:^(NSDictionary *result,NSError *error){
            if(error){completion(@{@"Phase":@"booting"},nil);return;}
            if([result[@"exitcode"] isEqual:@0]){
                if(self.installerResumeRequested){[self resumeRecovery:target completion:completion];return;}
                completion(@{@"Phase":@"recovery"},nil);return;
            }
            self.installerResumeRequested=NO;
            [self readStatus:^(NSDictionary *report,NSError *failure){
                if(failure){completion(nil,failure);return;}
                // The agent can answer before Disk Arbitration has published
                // its description of /. Absence is not a volume mismatch.
                NSString *rootVolume=[report[@"VolumeUUID"] isKindOfClass:NSString.class]?report[@"VolumeUUID"]:nil;
                if(!rootVolume.length){completion(@{@"Phase":@"root-volume-wait"},nil);return;}
                BOOL tiger=[self.machine.guest isEqual:@"tiger"];
                NSString *ownershipKey=tiger?@"InstallationOwnershipVerified":@"OwnershipVerified";
                NSString *identifier=self.machine.configuration[@"Identifier"];
                NSString *version=[report[@"ProductVersion"] isKindOfClass:NSString.class]?report[@"ProductVersion"]:nil;
                BOOL resumeGraphics=!tiger&&VIMOpenGLInitialGraphicsCanResume(report,identifier,target[@"VolumeUUID"],
                    [self.machine.configuration[@"InstallState"] isEqual:@"installing"],
                    [VIMOpenGLGuestForSystemVersion(version) isEqual:self.machine.guest]);
                if((![report[ownershipKey] isEqual:@YES]&&!resumeGraphics)||![report[@"MachineIdentifier"] isEqual:identifier]||
                   ![rootVolume isEqual:target[@"VolumeUUID"]]){
                    // This is a decision about automatic writes, not whether
                    // the user can boot and use the system. Keep the saved
                    // installer and let the ordinary desktop agent take over.
                    completion(@{@"Phase":@"setup-unavailable"},nil);return;
                }
                // Apple's real security updater publishes SystemVersion before
                // the running kernel changes. Never mistake that pending
                // restart for foreign-volume ownership or patch while it runs.
                if(![report[@"KernelMatchesSystem"] isEqual:@YES]){completion(@{@"Phase":@"update-restart-wait"},nil);return;}
                if(self.showProgress&&[self.machine.configuration[@"InstallState"] isEqual:@"installing"]){
                    BOOL automatic=[self.profile[@"Enabled"] boolValue];
                    NSString *accountPhase=report[@"Account"][@"Phase"];
                    if(automatic&&![@[@"autologin-armed",@"complete"] containsObject:accountPhase?:@""])
                        self.showProgress(YES,[(self.profile[@"AutomaticLogin"]?:@YES) boolValue]);
                    else if(!automatic&&[report[@"DesktopRunning"] boolValue]&&
                            (resumeGraphics||(!self.bundledGraphicsChecked&&[self.machine.guest isEqual:@"tiger"]&&
                            ![report[@"GraphicsFilesVerified"] boolValue])))
                        self.showProgress(NO,NO);
                }
                if(resumeGraphics){[self resumeInitialGraphics:completion];return;}
                if(!self.bundledGraphicsChecked){[self checkBundledGraphics:report completion:completion];return;}
                if([self.machine.guest isEqual:@"tiger"]&&!self.tigerGraphicsPrepared){
                    NSDictionary *native=report[@"Graphics"];
                    BOOL current=[report[@"GraphicsFilesVerified"] isEqual:@YES]&&[native[@"Phase"] isEqual:@"installed"]&&
                        [native[@"ProductBuildVersion"] isEqual:report[@"ProductBuildVersion"]];
                    // Only cold mismatches transfer code. Normal boots verify
                    // installed bytes locally, not a megabyte over the UART.
                    if(!current){[self prepareTigerGraphics:completion];return;}
                    self.tigerGraphicsPrepared=YES;
                }
                NSDictionary *graphics=report[@"Graphics"];
                if(![report[@"OwnershipVerified"] isEqual:@YES]||![graphics[@"Phase"] isEqual:@"installed"]||![graphics[@"ProductBuildVersion"] isEqual:report[@"ProductBuildVersion"]]){
                    completion(nil,SetupError(@"OS X graphics setup could not be verified. Its original graphics files and diagnostic log were retained."));return;
                }
                if(self.bootUUID){[self advanceReport:report completion:completion];return;}
                if(report[@"BootSessionUUID"]){
                    if(![report[@"BootSessionUUID"] isKindOfClass:NSString.class]||![[NSUUID alloc] initWithUUIDString:report[@"BootSessionUUID"]]){
                        completion(nil,SetupError(@"This Mac’s control session could not be identified safely."));return;
                    }
                    self.bootUUID=report[@"BootSessionUUID"];[self advanceReport:report completion:completion];return;
                }
                [self.agent runExecutable:@"/usr/sbin/sysctl" arguments:@[@"-n",@"kern.bootsessionuuid"] timeout:0 completion:^(NSDictionary *boot,NSError *issue){
                    NSString *value=[[NSString alloc] initWithData:boot[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
                    value=[value stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                    if(issue||![[NSUUID alloc] initWithUUIDString:value]){completion(nil,SetupError(@"This Mac’s boot session could not be identified safely."));return;}
                    self.bootUUID=value;[self advanceReport:report completion:completion];
                }];
            }];
        }];
    }];
}
- (void)readStatus:(void (^)(NSDictionary *,NSError *))completion {
    [self tool:@"opengl-setup-status" arguments:@[] input:nil completion:^(NSDictionary *reply,NSError *error){
        id report=reply[@"stdout"]?[NSJSONSerialization JSONObjectWithData:reply[@"stdout"] options:0 error:NULL]:nil;
        if(error||![reply[@"exitcode"] isEqual:@0]||![report isKindOfClass:NSDictionary.class]){
            completion(nil,error?:SetupError(@"The virtual Mac’s setup status is unavailable. The guest support log was retained for diagnosis."));return;
        }completion(report,nil);
    }];
}
- (NSNumber *)fileSharingPolicy {
    NSString *guest=self.machine.guest;
    NSString *metadata=@{@"tiger":@"TigerSMB.plist",@"leopard":@"LeopardSMB.plist",
                         @"snowleopard":@"SnowLeopardSMB.plist"}[guest];
    if(!metadata)return nil;
    NSString *path=[NSString stringWithFormat:@"Runtime/OpenGLGuests/%@/%@",guest,metadata];
    id value=[NSDictionary dictionaryWithContentsOfURL:[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:path]][@"PolicyVersion"];
    return [value isKindOfClass:NSNumber.class]&&[value integerValue]>0?value:nil;
}
- (void)finishFileSharing:(NSDictionary *)reply error:(NSError *)error completion:(void (^)(NSDictionary *,NSError *))completion {
        if(!error&&[reply[@"exitcode"] isEqual:@75]){completion(@{@"Phase":@"file-sharing-busy"},nil);return;}
        id proof=reply[@"stdout"]?[NSJSONSerialization JSONObjectWithData:reply[@"stdout"] options:0 error:NULL]:nil;
        NSDictionary *target=[NSDictionary dictionaryWithContentsOfURL:[self.machine.URL URLByAppendingPathComponent:@"State/InstallationTarget.plist"]];
        if(error||![reply[@"exitcode"] isEqual:@0]||![proof isKindOfClass:NSDictionary.class]||
           ![proof[@"Phase"] isEqual:@"installed"]||![proof[@"MachineIdentifier"] isEqual:self.machine.configuration[@"Identifier"]]||
           ![proof[@"VolumeUUID"] isEqual:target[@"VolumeUUID"]]){
            completion(nil,error?:SetupError(@"File Sharing’s compatibility update couldn’t finish. Your original system files and recovery records were retained."));return;
        }
        if(![proof[@"PolicyVersion"] isEqual:[self fileSharingPolicy]]){
            completion(nil,SetupError(@"File Sharing’s update has an unsupported version."));return;
        }
        if(![self.machine.guest isEqual:@"snowleopard"]&&![proof[@"ClientPolicyVersion"] isEqual:@2]){
            completion(nil,SetupError(@"The native File Sharing client update is missing."));return;
        }
        if([proof[@"RestartRequired"] boolValue]){completion(@{@"Phase":@"needs-restart"},nil);return;}
        self.fileSharingChecked=YES;completion(proof,nil);
}
- (void)stageFileSharing:(void (^)(NSDictionary *,NSError *))completion {
    [self stageFileSharingReusingServer:NO completion:completion];
}
- (void)stageFileSharingReusingServer:(BOOL)reuseServer completion:(void (^)(NSDictionary *,NSError *))completion {
        if(![self fileSharingPolicy]){completion(nil,SetupError(@"File Sharing’s bundled update is missing."));return;}
        BOOL tiger=[self.machine.guest isEqual:@"tiger"];
        BOOL snow=[self.machine.guest isEqual:@"snowleopard"];
        NSURL *tools=[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:
            [@"Runtime/OpenGLGuests" stringByAppendingPathComponent:self.machine.guest]];
        NSMutableDictionary *files=[NSMutableDictionary new];
        NSArray *names=tiger?@[@"opengl-file-sharing",@"tiger-smbd",@"tiger-opendirectory.so",@"TigerSMB.plist",@"Samba-COPYING.txt",
                              @"tiger-smbfs",@"TigerSMBClient-Info.plist",@"TigerSMBClient.plist",@"SMB-COPYRIGHT.txt",@"SMB-VIM-LICENSE.txt"]:
                            @[@"opengl-file-sharing",@"leopard-smbd",@"leopard-odsam.dylib",@"LeopardSMB.plist",@"Samba-COPYING.txt",
                              @"leopard-smbfs",@"LeopardSMBClient-Info.plist",@"LeopardSMBClient.plist",@"SMB-COPYRIGHT.txt",@"SMB-VIM-LICENSE.txt"];
        if(reuseServer)names=tiger?@[@"opengl-file-sharing",@"tiger-smbfs",@"TigerSMBClient-Info.plist",@"TigerSMBClient.plist",@"SMB-COPYRIGHT.txt",@"SMB-VIM-LICENSE.txt"]:
                                   @[@"opengl-file-sharing",@"leopard-smbfs",@"LeopardSMBClient-Info.plist",@"LeopardSMBClient.plist",@"SMB-COPYRIGHT.txt",@"SMB-VIM-LICENSE.txt"];
        if(snow)names=@[@"opengl-file-sharing",@"snowleopard-smbd",@"snowleopard-odsam.dylib",@"SnowLeopardSMB.plist",@"Samba-COPYING.txt"];
        for(NSString *name in names)
            files[name]=[tools URLByAppendingPathComponent:name];
        [self.agent stageFiles:files progress:nil completion:^(NSDictionary *receipt,NSError *failure){
            if(failure){completion(nil,failure);return;}
            NSString *directory=receipt[@"Directory"],*staged=[directory stringByAppendingPathComponent:@"opengl-file-sharing"];
            [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",staged] timeout:0 completion:^(NSDictionary *mode,NSError *issue){
                if(issue||![mode[@"exitcode"] isEqual:@0]){completion(nil,issue?:SetupError(@"File Sharing’s update couldn’t be prepared."));return;}
                [self.agent runExecutable:staged arguments:@[reuseServer?@"--install-client":@"--install",directory] timeout:0 completion:^(NSDictionary *reply,NSError *error){
                    [self finishFileSharing:reply error:error completion:completion];
                }];
            }];
        }];
}
- (void)prepareFileSharing:(void (^)(NSDictionary *,NSError *))completion {
    NSString *program=[GuestTools stringByAppendingPathComponent:@"opengl-file-sharing"];
    // Cold once per boot, not continuous background transfer/polling. Existing
    // current helpers/payloads avoid any multi-MB UART staging. The native tool
    // checks the real service files and repairs a minor update transaction.
    [self.agent runExecutable:@"/bin/test" arguments:@[@"-x",program] timeout:0 completion:^(NSDictionary *reply,NSError *error){
        if(error){completion(nil,error);return;}
        if(![reply[@"exitcode"] isEqual:@0]){[self stageFileSharing:completion];return;}
        [self.agent runExecutable:program arguments:@[@"--repair"] timeout:0 completion:^(NSDictionary *proof,NSError *failure){
            id record=proof[@"stdout"]?[NSJSONSerialization JSONObjectWithData:proof[@"stdout"] options:0 error:NULL]:nil;
            BOOL client=[@[@"tiger",@"leopard"] containsObject:self.machine.guest];
            BOOL oldClient=[proof[@"exitcode"] isEqual:@0]&&
                [record isKindOfClass:NSDictionary.class]&&(![record[@"PolicyVersion"] isEqual:[self fileSharingPolicy]]||
                                                          (client&&![record[@"ClientPolicyVersion"] isEqual:@2]));
            if(!failure&&([proof[@"exitcode"] isEqual:@66]||oldClient)){
                BOOL reuse=client&&oldClient&&[record[@"PolicyVersion"] isEqual:[self fileSharingPolicy]];
                [self stageFileSharingReusingServer:reuse completion:completion];return;
            }
            [self finishFileSharing:proof error:failure completion:completion];
        }];
    }];
}
- (void)advanceReport:(NSDictionary *)report completion:(void (^)(NSDictionary *,NSError *))completion {
    NSMutableDictionary *identified=[report mutableCopy];identified[@"BootSessionUUID"]=self.bootUUID;report=identified;
    NSDictionary *account=report[@"Account"];
    BOOL automatic=[self.profile[@"Enabled"] boolValue];NSString *phase=account[@"Phase"];
    BOOL needsFirstLoginPolicy=[phase isEqual:@"autologin-armed"]&&![account[@"FirstLoginPolicyPrepared"] boolValue];
    BOOL recoverLogin=[@[@"elcapitan",@"sierra"] containsObject:self.machine.guest]&&[report[@"AutoLoginHandoffMissing"] isEqual:@YES];
    BOOL needsSharingCredential=[self.machine.guest isEqual:@"leopard"]&&[self.profile[@"FileSharing"] boolValue]&&
        [@[@"autologin-armed",@"complete"] containsObject:phase?:@""]&&[account[@"FileSharingAccountPrepared"] boolValue]&&
        [account[@"FileSharingCredentialVersion"] integerValue]<1;
    if(automatic&&(![@[@"autologin-armed",@"complete"] containsObject:phase?:@""]||needsFirstLoginPolicy||needsSharingCredential||recoverLogin)) {
        if([report[@"AccountBusy"] boolValue]){completion(@{@"Phase":@"account-busy"},nil);return;}
        NSString *credential=self.profile[@"CredentialIdentifier"];
        NSDictionary *identity=@{@"MachineIdentifier":self.machine.configuration[@"Identifier"],@"VolumeUUID":report[@"VolumeUUID"],@"Username":self.profile[@"Username"]};
        NSString *provided=self.resumedPassword;self.resumedPassword=nil;
        // Credential I/O stays away from AppKit/input; any migration from the
        // previous Keychain store is noninteractive. The same single guest-
        // agent owner remains reserved throughout.
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
            NSError *error=nil;NSString *password=provided?:[VIMAutomaticSetup passwordForIdentifier:credential error:&error];
            NSMutableDictionary *request=[identity mutableCopy];if(password)request[@"Password"]=password;
            NSData *input=password?[NSJSONSerialization dataWithJSONObject:request options:0 error:&error]:nil;
            dispatch_async(dispatch_get_main_queue(),^{
                if(!password){
                    // A moved/completed guest may no longer have its original
                    // private host credential. Ask once through the real UI;
                    // never replace its account or retry an empty password.
                    completion(@{@"Phase":@"credential-required",@"Username":self.profile[@"Username"]?:@""},nil);return;
                }
                if(!input){completion(nil,error?:SetupError(@"The setup password could not be prepared."));return;}
                void (^prepared)(NSDictionary *,NSError *)=^(NSDictionary *reply,NSError *failure){
                    // Apple's SMB preference initializer may run after the
                    // account provisioner on the very first installed boot.
                    // Keep the guarded journal/password and use the existing
                    // cold coordinator cadence, not a timer inside the guest.
                    if(!failure&&[reply[@"exitcode"] isEqual:@75]){completion(@{@"Phase":@"desktop-services-wait"},nil);return;}
                    if(failure||![reply[@"exitcode"] isEqual:@0]){
                        NSString *detail=[[NSString alloc] initWithData:reply[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
                        completion(nil,failure?:SetupError(detail.length?detail:@"The account setup could not finish. Its guarded transaction can be resumed without replacing an existing account."));return;
                    }
                    [self readStatus:^(NSDictionary *next,NSError *issue){if(issue)completion(nil,issue);else [self advanceReport:next completion:completion];}];
                };
                if(recoverLogin)
                    [self tool:@"opengl-create-account" arguments:@[@"--recover-first-login"] input:input completion:prepared];
                else
                    [self runAccountToolForSharingRepair:needsSharingCredential input:input completion:prepared];
            });
        });return;
    }
    NSDictionary *restart=[NSDictionary dictionaryWithContentsOfURL:[self.machine.URL URLByAppendingPathComponent:@"State/LegacySetupRestart.plist"]];
    BOOL alreadyRequested=[restart[@"FromBootSessionUUID"] isEqual:self.bootUUID]&&[restart[@"RequestState"] isEqual:@"acknowledged"];
    BOOL changed=VIMOpenGLGraphicsNeedsRestart(report,restart,self.bootUUID);
    BOOL firstLoginPolicyChanged=automatic&&[phase isEqual:@"autologin-armed"]&&
        [account[@"FirstLoginPolicyBootSessionUUID"] isEqual:self.bootUUID];
    // Preboot Tiger graphics already exist before WindowServer loads them,
    // so ChangedThisBoot is false. Still retire the exact owned installer in
    // the same guarded first-setup restart, including manually created users.
    BOOL nativeInstallerAttached=[self.machine.guest isEqual:@"tiger"]&&self.machine.configuration[@"InstallerMedia"];
    BOOL needsRestart=(automatic&&[phase isEqual:@"autologin-armed"]&&!restart)||changed||self.payloadRestartRequired||firstLoginPolicyChanged||nativeInstallerAttached;
    if(alreadyRequested){completion(@{@"Phase":@"restarting"},nil);return;}
    if(needsRestart&&(automatic||[report[@"DesktopRunning"] boolValue])) {
        completion(@{@"Phase":@"needs-restart",@"Automatic":@(automatic&&!self.updatingCompletedGuest),
            @"ExistingGuestUpdate":@(self.updatingCompletedGuest),@"BootSessionUUID":self.bootUUID,@"Report":report},nil);return;
    }
    if(![report[@"DesktopRunning"] boolValue]){completion(@{@"Phase":@"desktop-wait"},nil);return;}
    // Check only once the new login/WindowServer has loaded the published GA
    // and engine. Setup Assistant's pre-restart session may still use stock GL.
    if(!self.testedGraphics) {
        [self tool:@"opengl-graphics-check" arguments:@[@"--desktop"] input:nil completion:^(NSDictionary *reply,NSError *failure){
            if(failure||![reply[@"exitcode"] isEqual:@0]){
                [self logFailedVerification:@"Graphics verification" reply:reply error:failure];
                completion(nil,failure?:SetupError(@"The real accelerated graphics check did not pass. The installer and original graphics backup were retained."));return;
            }
            self.testedGraphics=YES;[self advanceReport:report completion:completion];
        }];return;
    }
    if(automatic&&(![phase isEqual:@"complete"]||[account[@"DesktopServicesVersion"] integerValue]<2)) {
        [self tool:@"opengl-setup-status" arguments:@[@"--finish-first-login"] input:nil completion:^(NSDictionary *reply,NSError *failure){
            if(!failure&&[reply[@"exitcode"] isEqual:@75]){completion(@{@"Phase":@"desktop-services-wait"},nil);return;}
            if(failure||![reply[@"exitcode"] isEqual:@0]){completion(nil,failure?:SetupError(@"The one-time login handoff could not be retired safely. Its state was retained."));return;}
            [self readStatus:^(NSDictionary *next,NSError *issue){if(issue)completion(nil,issue);else [self advanceReport:next completion:completion];}];
        }];return;
    }
    BOOL snowSharing=[self.machine.guest isEqual:@"snowleopard"]&&[account[@"FileSharingConfigured"] boolValue];
    if((snowSharing||[@[@"leopard",@"tiger"] containsObject:self.machine.guest])&&!self.fileSharingChecked){
        [self prepareFileSharing:^(NSDictionary *proof,NSError *failure){
            if(failure){
                if(!snowSharing){completion(nil,failure);return;}
                // Optional file sharing must not strand an accelerated Mac
                // in Setup Assistant or hold on to its installer.
                NSLog(@"Snow Leopard File Sharing update: %@",failure.localizedDescription);
                self.fileSharingChecked=YES;
                [self advanceReport:report completion:completion];return;
            }
            if([proof[@"Phase"] isEqual:@"needs-restart"]){
                completion(@{@"Phase":@"needs-restart",@"Automatic":@(automatic),@"BootSessionUUID":self.bootUUID,@"Report":report},nil);return;
            }
            if([proof[@"Phase"] isEqual:@"file-sharing-busy"]){completion(proof,nil);return;}
            [self advanceReport:report completion:completion];
        }];return;
    }
    if([self.machine.guest isEqual:@"tiger"]&&!self.tigerAudioPrepared&&
       (!self.machine.configuration[@"AudioEnabled"]||[self.machine.configuration[@"AudioEnabled"] boolValue])){
        [self prepareTigerAudio:^(NSDictionary *proof,NSError *failure){
            if(failure){completion(nil,failure);return;}
            if([proof[@"Phase"] isEqual:@"needs-restart"]){
                completion(@{@"Phase":@"needs-restart",@"Automatic":@(automatic),@"BootSessionUUID":self.bootUUID,@"Report":report},nil);return;
            }
            if([proof[@"Phase"] isEqual:@"audio-busy"]){completion(proof,nil);return;}
            [self advanceReport:report completion:completion];
        }];return;
    }
    NSDictionary *previous=[NSDictionary dictionaryWithContentsOfURL:[self.machine.URL URLByAppendingPathComponent:@"State/LegacyGuestInstallation.plist"]];
    BOOL routed=[previous[@"GuestReport"][@"AudioInitialized"] boolValue]&&
        [previous[@"GuestReport"][@"VolumeUUID"] isEqual:report[@"VolumeUUID"]];
    if(routed)self.audioInitialized=YES;
    if(!self.audioInitialized&&!self.audioDefaultsSkipped&&(!self.machine.configuration[@"AudioEnabled"]||[self.machine.configuration[@"AudioEnabled"] boolValue])) {
        [self tool:@"opengl-audio-defaults" arguments:@[@"--set-defaults"] input:nil completion:^(NSDictionary *reply,NSError *failure){
            // IOAudioDevice publication also precedes coreaudiod's initial
            // endpoint discovery on Tiger. Only the explicit missing-device
            // result is pending; preserve real API/routing failures.
            // Some guests never publish an input device (Tiger's VoodooHDA
            // exposes only the output), so exit 3 may never clear. Wait about
            // two minutes (15 s polls) for a late device, then finish setup
            // without default routing instead of blocking it on any guest.
            if(!failure&&[reply[@"exitcode"] isEqual:@3]&&++self.audioDeviceWaits<=8){
                completion(@{@"Phase":@"audio-busy"},nil);return;
            }
            if(!failure&&[reply[@"exitcode"] isEqual:@3]){
                NSLog(@"Sound defaults left unchanged: %@",[[NSString alloc] initWithData:reply[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding]);
                self.audioDefaultsSkipped=YES;[self advanceReport:report completion:completion];return;
            }
            if(failure||![reply[@"exitcode"] isEqual:@0]){
                [self logFailedVerification:@"Audio routing verification" reply:reply error:failure];
                completion(nil,failure?:SetupError(@"The virtual Mac’s sound devices are not ready. Graphics setup is preserved; the audio diagnostic state was retained."));return;
            }
            self.audioInitialized=YES;[self advanceReport:report completion:completion];
        }];return;
    }
    NSMutableDictionary *proof=[report mutableCopy];proof[@"PixelChecksPassed"]=@YES;proof[@"AudioInitialized"]=@(self.audioInitialized);
    completion(@{@"Phase":@"complete",@"Report":proof},nil);
}
@end
