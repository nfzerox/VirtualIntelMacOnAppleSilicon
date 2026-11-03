#import "VIMGuestAgent.h"
#import "VIMQMPConnection.h"
#include <limits.h>
#include <math.h>
#include <arpa/inet.h>
static NSError *GuestError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.GuestAgent" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
}
static BOOL Successful(NSDictionary *result) {
    return [result[@"exitcode"] isKindOfClass:NSNumber.class] &&
        [result[@"exitcode"] integerValue]==0 && !result[@"signal"];
}
static NSString *Output(NSDictionary *result) {
    return [[NSString alloc] initWithData:result[@"stdout"]?:NSData.data encoding:NSUTF8StringEncoding];
}
@interface VIMGuestAgent ()
@property VIMQMPConnection *connection;
@property BOOL closed;
@property BOOL collecting;
@property BOOL staging;
@property BOOL openingPreferences;
@property NSString *statusDirectory;
@property NSData *statusPayload;
@property NSString *socketPath;
@property BOOL recoveryTransport;
@property BOOL connecting;
@property NSMutableArray *connectCompletions;
@end
@implementation VIMGuestAgent
- (instancetype)initWithSocketPath:(NSString *)path {
    if((self=[super init])){_socketPath=[path copy];_connection=[[VIMQMPConnection alloc] initWithGuestAgentSocketPath:path];_processPollInterval=.25;}
    return self;
}
- (NSString *)transportName {
    if(self.recoveryTransport)return @"Apple16X50SerialRecovery";
    return self.preMetalGuest?@"Apple16X50Serial":@"AppleVirtIO";
}
- (void)connectWithCompletion:(void (^)(NSError *))completion {
    NSAssert(NSThread.isMainThread,@"Guest controller is main-thread owned");
    if(self.closed){completion(GuestError(@"The guest controller is closed."));return;}
    if(!self.connectCompletions)self.connectCompletions=[NSMutableArray new];
    [self.connectCompletions addObject:[completion copy]];
    if(self.connecting)return;
    self.connecting=YES;
    [self connectAllowingRecoveryFallback:YES];
}
- (void)connectAllowingRecoveryFallback:(BOOL)fallback {
    [self.connection connectWithCompletion:^(NSError *error){
        if(error&&fallback&&self.recoverySocketPath.length&&!self.closed){
            // No guest command has been dispatched. Release the failed endpoint
            // before selecting the other transport; retain one protocol owner.
            [self.connection close];self.recoveryTransport=!self.recoveryTransport;
            self.statusDirectory=nil;self.statusPayload=nil;
            self.connection=[[VIMQMPConnection alloc] initWithGuestAgentSocketPath:
                self.recoveryTransport?self.recoverySocketPath:self.socketPath];
            [self connectAllowingRecoveryFallback:NO];return;
        }
        NSArray *callbacks=[self.connectCompletions copy];[self.connectCompletions removeAllObjects];self.connecting=NO;
        for(void (^callback)(NSError *) in callbacks)callback(error);
    }];
}
- (void)runExecutable:(NSString *)path arguments:(NSArray<NSString *> *)arguments
              timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    [self runExecutable:path arguments:arguments standardInput:nil timeout:timeout completion:completion];
}
- (void)runExecutable:(NSString *)path arguments:(NSArray<NSString *> *)arguments standardInput:(NSData *)input
              timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    NSAssert(NSThread.isMainThread,@"Guest controller is main-thread owned");
    if(self.closed || !path.isAbsolutePath || !isfinite(timeout) || timeout<0 || timeout>1200 || input.length>16384) {
        completion(nil,GuestError(@"The guest command cannot be started."));return;
    }
    double deadline=timeout>0?NSProcessInfo.processInfo.systemUptime+timeout:0;
    NSMutableDictionary *request=[@{@"path":path,@"arg":[arguments copy],@"capture-output":@YES} mutableCopy];
    if(input)request[@"input-data"]=[input base64EncodedStringWithOptions:0];
    [self.connection execute:@"guest-exec" arguments:request
        completion:^(id result,NSError *error) {
        NSNumber *pid=[result isKindOfClass:NSDictionary.class]?result[@"pid"]:nil;
        if(error || ![pid isKindOfClass:NSNumber.class] || pid.longLongValue<=0) {
            completion(nil,error?:GuestError(@"The guest did not return a process identifier."));return;
        }
        [self pollProcess:pid deadline:deadline completion:completion];
    }];
}
- (void)pollProcess:(NSNumber *)pid deadline:(double)deadline completion:(void (^)(NSDictionary *,NSError *))completion {
    if(self.closed || (deadline>0 && NSProcessInfo.processInfo.systemUptime>=deadline)) {
        completion(nil,GuestError(@"The guest command was interrupted or timed out. It may still be running."));return;
    }
    [self.connection execute:@"guest-exec-status" arguments:@{@"pid":pid} completion:^(id reply,NSError *error) {
        if(error || ![reply isKindOfClass:NSDictionary.class] || ![reply[@"exited"] isKindOfClass:NSNumber.class]) {
            completion(nil,error?:GuestError(@"The guest returned an invalid process status."));return;
        }
        if([reply[@"exited"] boolValue]) {
            NSMutableDictionary *result=[reply mutableCopy];
            for(NSString *stream in @[@"out",@"err"]) {
                id encoded=reply[[stream stringByAppendingString:@"-data"]];
                id truncated=reply[[stream stringByAppendingString:@"-truncated"]];
                if((truncated && (![truncated isKindOfClass:NSNumber.class] || [truncated boolValue])) ||
                   (encoded && ![encoded isKindOfClass:NSString.class])) {
                    completion(nil,GuestError(@"The guest command output was incomplete."));return;
                }
                NSData *data=encoded?[[NSData alloc] initWithBase64EncodedString:encoded options:0]:NSData.data;
                if(!data){completion(nil,GuestError(@"The guest command output was invalid."));return;}
                result[[stream isEqual:@"out"]?@"stdout":@"stderr"]=data;
            }
            if(![reply[@"exitcode"] isKindOfClass:NSNumber.class] && ![reply[@"signal"] isKindOfClass:NSNumber.class]) {
                completion(nil,GuestError(@"The guest command did not report its exit status."));return;
            }
            completion(result,nil);return;
        }
        NSTimeInterval interval=isfinite(self.processPollInterval)?MAX(.25,MIN(15,self.processPollInterval)):.25;
        if(self.recoveryTransport)interval=MAX(1,interval); // cold UART, not a VirtIO work queue
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)(interval*NSEC_PER_SEC)),dispatch_get_main_queue(),^{
            [self pollProcess:pid deadline:deadline completion:completion];
        });
    }];
}
- (void)requestShutdownWithCompletion:(void (^)(NSError *))completion {
    [self requestPowerOperation:@"-h" completion:completion];
}
- (void)requestRestartWithCompletion:(void (^)(NSError *))completion {
    [self requestPowerOperation:@"-r" completion:completion];
}
- (void)requestPowerOperation:(NSString *)operation completion:(void (^)(NSError *))completion {
    NSAssert(NSThread.isMainThread,@"Guest controller is main-thread owned");
    if(self.closed){completion(GuestError(@"The virtual Mac’s guest tools are not connected."));return;}
    // Apple's agent supports guest-exec; its guest-shutdown implementation is
    // not assumed to match upstream QGA. A virtual ACPI power button may sleep
    // macOS instead. Use the same native command as the setup restart handoff,
    // but do not poll a process whose agent normally terminates during shutdown.
    [self.connection execute:@"guest-exec" arguments:@{@"path":@"/sbin/shutdown",
        @"arg":@[operation,@"now"],@"capture-output":@NO} completion:^(id reply,NSError *error){
        NSNumber *pid=[reply isKindOfClass:NSDictionary.class]?reply[@"pid"]:nil;
        completion(error?:(![pid isKindOfClass:NSNumber.class]||pid.longLongValue<=0?
            GuestError(@"The virtual Mac did not acknowledge shutdown. It may already be shutting down; the request was not repeated."):nil));
    }];
}
- (void)writeData:(NSData *)data path:(NSString *)path completion:(void (^)(NSError *))completion {
    [self writeData:data path:path deadline:NSProcessInfo.processInfo.systemUptime+600 progress:nil completion:completion];
}
- (void)writeData:(NSData *)data path:(NSString *)path deadline:(double)deadline
          progress:(void (^)(NSUInteger))progress completion:(void (^)(NSError *))completion {
    [self.connection execute:@"guest-file-open" arguments:@{@"path":path,@"mode":@"wb"} completion:^(id handle,NSError *error) {
        if(error || ![handle isKindOfClass:NSNumber.class]) {
            completion(error?:GuestError(@"The guest could not open the staging file."));return;
        }
        [self writeChunks:data handle:handle offset:0 deadline:deadline progress:progress completion:^(NSError *writeError) {
            [self.connection execute:@"guest-file-close" arguments:@{@"handle":handle} completion:^(id ignored,NSError *closeError) {
                completion(writeError?:closeError);
            }];
        }];
    }];
}
- (void)writeChunks:(NSData *)data handle:(NSNumber *)handle offset:(NSUInteger)offset deadline:(double)deadline
           progress:(void (^)(NSUInteger))progress completion:(void (^)(NSError *))completion {
    if(self.closed||(deadline>0&&NSProcessInfo.processInfo.systemUptime>=deadline)){completion(GuestError(@"Guest transfer was interrupted or timed out. Partial staging was retained."));return;}
    if(offset==data.length){completion(nil);return;}
    // 384 KiB produces a 512 KiB base64 request, comfortably below QGA's
    // 64 MiB JSON limit. The former 48 KiB blocks required eight times as many
    // serial round trips for the large graphics archive. Keep acknowledgement
    // but avoid copying each mapped input slice.
    // A physical 16550 FIFO is not a VirtIO bulk queue. Keep UART packets
    // bounded and acknowledged, rather than feeding half-megabyte JSON lines
    // through the period serial driver's receive watermarks in one burst.
    NSUInteger count=MIN((NSUInteger)((self.preMetalGuest||self.recoveryTransport)?256:384*1024),data.length-offset);
    NSData *slice=[NSData dataWithBytesNoCopy:(void *)((const uint8_t *)data.bytes+offset)
                                     length:count freeWhenDone:NO];
    NSString *chunk=[slice base64EncodedStringWithOptions:0];
    [self.connection execute:@"guest-file-write" arguments:@{@"handle":handle,@"buf-b64":chunk} completion:^(id reply,NSError *error) {
        NSNumber *written=[reply isKindOfClass:NSDictionary.class]?reply[@"count"]:nil;
        if(error || ![written isKindOfClass:NSNumber.class] || written.unsignedIntegerValue!=count) {
            completion(error?:GuestError(@"The guest staging file was not completely written."));return;
        }
        if(progress)progress(offset+count);
        [self writeChunks:data handle:handle offset:offset+count deadline:deadline progress:progress completion:completion];
    }];
}
- (void)collectStatusWithTool:(NSData *)tool completion:(void (^)(NSDictionary *,NSError *))completion {
    NSAssert(NSThread.isMainThread,@"Guest controller is main-thread owned");
    if(self.closed || self.collecting || self.staging || self.openingPreferences || !tool.length || tool.length>1024*1024) {
        completion(nil,GuestError(@"The guest status probe is unavailable or already running."));return;
    }
    self.collecting=YES;
    NSData *payload=[tool copy];
    void (^finish)(NSDictionary *,NSError *)=^(NSDictionary *report,NSError *error){
        self.collecting=NO;completion(report,error);
    };
    if(self.statusDirectory && [self.statusPayload isEqualToData:payload]) {
        NSString *directory=self.statusDirectory;
        NSString *path=[directory stringByAppendingPathComponent:@"guest-status"];
        // BSD stat defaults to lstat: reject symlinks, non-root ownership,
        // shared directories and extra hard links before executing the cache.
        // Root-only execute permission also prevents an unprivileged replacement
        // agent from supplying a successful privileged status report.
        [self runExecutable:@"/usr/bin/stat" arguments:@[@"-f",@"%u:%p:%l:%z",directory,path] timeout:15
            completion:^(NSDictionary *result,NSError *error){
            if(error){finish(nil,error);return;}
            NSString *text=[Output(result) stringByTrimmingCharactersInSet:NSCharacterSet.newlineCharacterSet];
            NSArray *lines=[text componentsSeparatedByString:@"\n"];
            NSArray *parent=lines.count==2?[lines[0] componentsSeparatedByString:@":"]:nil;
            BOOL valid=Successful(result)&&parent.count==4&&[parent[0] isEqual:@"0"]&&[parent[1] isEqual:@"40700"]&&
                [lines[1] isEqual:[NSString stringWithFormat:@"0:100700:1:%lu",(unsigned long)payload.length]];
            if(valid){[self readStatusAtPath:path completion:finish];return;}
            // A guest reboot/recovery environment may remove the private run
            // directory. Restage after this definitive read-only miss, never
            // after an ambiguous transport failure and never delete a changed
            // or untrusted cache path.
            self.statusDirectory=nil;self.statusPayload=nil;
            [self stageStatusTool:payload completion:finish];
        }];return;
    }
    self.statusDirectory=nil;self.statusPayload=nil;
    [self stageStatusTool:payload completion:finish];
}
- (void)openPreferencesPane:(NSString *)pane completion:(void (^)(NSError *))completion {
    NSAssert(NSThread.isMainThread,@"Guest controller is main-thread owned");
    NSString *url=[pane isKindOfClass:NSString.class]?@{@"sharing":@"x-apple.systempreferences:com.apple.preferences.sharing",
                    @"microphone":@"x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone"}[pane]:nil;
    // Period open(1) treats the later x-apple.systempreferences: spelling as
    // a local filename. Open the real Apple pane through Launch Services.
    if(self.preMetalGuest&&[pane isEqual:@"sharing"])url=@"/System/Library/PreferencePanes/SharingPref.prefPane";
    [self openGUIURL:url completion:completion];
}
- (void)openHostConnection:(NSURL *)URL completion:(void (^)(NSError *))completion {
    // The session first matches this IP to its actual host-side network address.
    // Do not resolve arbitrary names or pass untrusted URL options to open.
    struct in_addr address;BOOL numeric=inet_pton(AF_INET,URL.host.UTF8String,&address)==1;
    BOOL allowed=[@[@"ssh",@"smb"] containsObject:URL.scheme.lowercaseString]&&numeric&&!URL.password&&!URL.query&&!URL.fragment&&(!URL.port||[URL.port isEqual:@22]);
    if(!allowed){completion(GuestError(@"Choose a supported connection to this Mac."));return;}
    if(self.preMetalGuest&&[URL.scheme.lowercaseString isEqual:@"ssh"]){
        if(self.closed||self.collecting||self.staging||self.openingPreferences){completion(GuestError(@"Wait for this virtual Mac’s current operation to finish, then try again."));return;}
        self.openingPreferences=YES;
        [self runConsoleExecutable:@"/Library/Application Support/Virtual Intel Mac/GuestTools/guest-host-connection"
            arguments:@[@"--ssh",URL.host,URL.user?:NSUserName()] timeout:30 completion:^(NSDictionary *opened,NSError *error){
                self.openingPreferences=NO;
                completion(error?:(!Successful(opened)?GuestError(@"Terminal couldn’t open the modern SSH client inside this virtual Mac. Make sure its guest support is up to date and sign in to its desktop, then try again."):nil));
            }];return;
    }
    [self openGUIURL:URL.absoluteString completion:completion];
}
- (void)openGUIURL:(NSString *)url completion:(void (^)(NSError *))completion {
    if(!url||self.closed||self.collecting||self.staging||self.openingPreferences) {
        completion(GuestError(@"Start the virtual Mac, sign in and wait for setup to finish, then try again. You can also open System Preferences inside the virtual Mac."));return;
    }
    self.openingPreferences=YES;
    void (^finish)(NSError *)=^(NSError *error){self.openingPreferences=NO;completion(error);};
    [self runConsoleExecutable:@"/usr/bin/open" arguments:@[url] timeout:30 completion:^(NSDictionary *opened,NSError *error){
        finish(error?:(!Successful(opened)?GuestError(@"The connection could not open inside the virtual Mac. Sign in to its desktop and try again."):nil));
    }];
}
- (void)runConsoleExecutable:(NSString *)path arguments:(NSArray<NSString *> *)arguments timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    if(self.tigerGuest){
        [self runExecutable:@"/Library/Application Support/Virtual Intel Mac/GuestTools/opengl-tiger-console"
            arguments:[@[@"--",path] arrayByAddingObjectsFromArray:arguments] timeout:timeout completion:completion];return;
    }
    [self runExecutable:@"/usr/bin/stat" arguments:@[@"-f",@"%u",@"/dev/console"] timeout:15
        completion:^(NSDictionary *result,NSError *error){
        NSString *text=[Output(result) stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
        NSScanner *scanner=text?[NSScanner scannerWithString:text]:nil;unsigned long long uid=0;
        if(error||!Successful(result)||![scanner scanUnsignedLongLong:&uid]||!scanner.isAtEnd||uid<501||uid>INT_MAX) {
            completion(nil,error?:GuestError(@"Sign in to the virtual Mac’s desktop before opening its settings."));return;
        }
        NSString *account=[@(uid) stringValue];
        // Enter the real Aqua session, then drop root before Launch Services.
        // Period sudo predates -n. -S reads the agent's closed stdin instead
        // of prompting; modern guests retain -n. No credential input is sent.
        // All arguments are fixed
        // or allowlisted URLs and validated numeric values, never shell text.
        NSMutableArray *child=[@[@"/usr/bin/sudo",self.preMetalGuest?@"-S":@"-n",@"-u",[@"#" stringByAppendingString:account]] mutableCopy];
        // Leopard liblaunch treats inherited SUDO_COMMAND as a request for
        // the system socket, even after bsexec enters the actual Aqua port.
        // env -i is native there; -u is not. Only this child is affected.
        if(self.preMetalGuest)[child addObjectsFromArray:@[@"/usr/bin/env",@"-i",@"PATH=/usr/bin:/bin:/usr/sbin:/sbin"]];
        [child addObject:path];[child addObjectsFromArray:arguments];
        if(!self.preMetalGuest){[self runExecutable:@"/bin/launchctl" arguments:[@[@"asuser",account] arrayByAddingObjectsFromArray:child] timeout:timeout completion:completion];return;}
        [self runExecutable:@"/bin/ps" arguments:@[@"-axo",@"uid=,pid=,comm="] timeout:15 completion:^(NSDictionary *processes,NSError *failure){
            NSString *pid=nil;
            for(NSString *line in [Output(processes) componentsSeparatedByString:@"\n"]){
                NSScanner *scan=[NSScanner scannerWithString:line];long long owner=0,process=0;
                if([scan scanLongLong:&owner]&&[scan scanLongLong:&process]&&owner==(long long)uid&&process>0&&
                   [[line substringFromIndex:scan.scanLocation] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet].length&&
                   [[[line substringFromIndex:scan.scanLocation] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] isEqual:@"/System/Library/CoreServices/Finder.app/Contents/MacOS/Finder"]){pid=@(process).stringValue;break;}
            }
            if(failure||!Successful(processes)||!pid){completion(nil,failure?:GuestError(@"Sign in to the virtual Mac’s desktop before opening its settings."));return;}
            [self runExecutable:@"/bin/launchctl" arguments:[@[@"bsexec",pid] arrayByAddingObjectsFromArray:child] timeout:timeout completion:completion];
        }];
    }];
}
- (void)readStatusAtPath:(NSString *)path completion:(void (^)(NSDictionary *,NSError *))completion {
    // Cache executable bytes only, never readiness, build, volume or boot data.
    [self runExecutable:path arguments:@[] timeout:30 completion:^(NSDictionary *result,NSError *error){
        NSError *failure=error;
        id report=Successful(result)?[NSJSONSerialization JSONObjectWithData:result[@"stdout"] options:0 error:&failure]:nil;
        if(![report isKindOfClass:NSDictionary.class] || ![report[@"ProtocolVersion"] isEqual:@1] || ![report[@"Ready"] isEqual:@NO]) {
            completion(nil,failure?:GuestError(@"The guest returned an invalid installation report."));return;
        }
        completion(report,failure);
    }];
}
- (void)collectInstalledOpenGLDesktopStatus:(void (^)(NSDictionary *,NSError *))completion {
    // Installed by the root-owned support manifest. This observation cannot
    // authorize framework replacement or mark an imported VM as installed.
    [self readStatusAtPath:@"/Library/Application Support/Virtual Intel Mac/GuestTools/opengl-desktop-status" completion:completion];
}
- (void)confirmPrivilegedAgent:(void (^)(NSError *))completion {
    // Both period DVDs and genuine Big Sur Recovery omit /usr/bin/id. All
    // supported guests ship Bash as /bin/sh; its readonly EUID comes from the
    // process identity, not an inherited environment variable. Use the same
    // native check in Recovery and the installed OS, without probing an
    // optional executable and disrupting an otherwise healthy agent stream.
    [self runExecutable:@"/bin/sh" arguments:@[@"-c",@"test \"${EUID-}\" = 0 && printf '0\\n'"] timeout:15 completion:^(NSDictionary *identity,NSError *error) {
        completion(error?:((Successful(identity)&&[Output(identity) isEqual:@"0\n"])?nil:
            GuestError(@"Automatic guest setup requires the built-in privileged guest agent.")));
    }];
}
- (void)stageStatusTool:(NSData *)payload completion:(void (^)(NSDictionary *,NSError *))finish {
    [self confirmPrivilegedAgent:^(NSError *error) {
        if(error){finish(nil,error);return;}
        [self runExecutable:@"/usr/bin/mktemp" arguments:@[@"-d",@"/private/var/run/VirtualIntelMacStatus.XXXXXX"] timeout:15
            completion:^(NSDictionary *created,NSError *createError) {
            NSString *directory=[Output(created) stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
            NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^/private/var/run/VirtualIntelMacStatus\\.[A-Za-z0-9]{6}$" options:0 error:NULL];
            if(createError || !Successful(created) || !directory || [pattern numberOfMatchesInString:directory options:0 range:NSMakeRange(0,directory.length)]!=1) {
                finish(nil,createError?:GuestError(@"The guest could not create a private staging directory."));return;
            }
            NSString *path=[directory stringByAppendingPathComponent:@"guest-status"];
            void (^cleanup)(NSDictionary *,NSError *)=^(NSDictionary *report,NSError *originalError) {
                [self runExecutable:@"/bin/rm" arguments:@[@"-f",path] timeout:15 completion:^(NSDictionary *removed,NSError *removeError) {
                    NSError *failure=originalError?:removeError;
                    if(!failure&&!Successful(removed))failure=GuestError(@"Could not remove the temporary guest probe.");
                    [self runExecutable:@"/bin/rmdir" arguments:@[directory] timeout:15 completion:^(NSDictionary *removedDirectory,NSError *directoryError) {
                        finish(report,failure?:directoryError?:(!Successful(removedDirectory)?GuestError(@"Could not remove the temporary guest directory."):nil));
                    }];
                }];
            };
            [self writeData:payload path:path completion:^(NSError *writeError) {
                if(writeError){cleanup(nil,writeError);return;}
                [self runExecutable:@"/bin/chmod" arguments:@[@"0700",path] timeout:15 completion:^(NSDictionary *mode,NSError *modeError) {
                    if(modeError || !Successful(mode)){cleanup(nil,modeError?:GuestError(@"Could not prepare the guest probe."));return;}
                    self.statusDirectory=directory;self.statusPayload=payload;
                    [self readStatusAtPath:path completion:finish];
                }];
            }];
        }];
    }];
}
- (void)stageFiles:(NSDictionary<NSString *,NSURL *> *)files progress:(void (^)(uint64_t,uint64_t))progress
        completion:(void (^)(NSDictionary *,NSError *))completion {
    [self stageFiles:files persistent:NO progress:progress completion:completion];
}
- (void)stageInstallationFiles:(NSDictionary<NSString *,NSURL *> *)files progress:(void (^)(uint64_t,uint64_t))progress
        completion:(void (^)(NSDictionary *,NSError *))completion {
    [self stageFiles:files persistent:YES progress:progress completion:completion];
}
- (void)stageFiles:(NSDictionary<NSString *,NSURL *> *)files persistent:(BOOL)persistent progress:(void (^)(uint64_t,uint64_t))progress
        completion:(void (^)(NSDictionary *,NSError *))completion {
    NSAssert(NSThread.isMainThread,@"Guest controller is main-thread owned");
    if(self.closed||self.staging||self.collecting||self.openingPreferences||!files.count||files.count>16) {
        completion(nil,GuestError(@"Guest setup transfer is unavailable or another operation is active."));return;
    }
    self.staging=YES;NSDictionary *inputs=[files copy];
    void (^finish)(NSDictionary *,NSError *)=^(NSDictionary *receipt,NSError *error){self.staging=NO;completion(receipt,error);};
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        NSMutableArray *entries=[NSMutableArray array];NSError *failure=nil;uint64_t total=0;
        NSArray *names=[inputs.allKeys sortedArrayUsingSelector:@selector(compare:)];
        NSCharacterSet *invalid=[[NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_."] invertedSet];
        for(NSString *name in names) {
            NSURL *url=inputs[name];
            if(!name.length||[name hasPrefix:@"."]||name.length>128||[name rangeOfCharacterFromSet:invalid].location!=NSNotFound||!url.isFileURL) {
                failure=GuestError(@"Invalid guest setup file name or source.");break;
            }
            NSNumber *regular=nil;[url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:&failure];
            if(failure||!regular.boolValue){failure=failure?:GuestError(@"Guest setup input is not a regular file.");break;}
            NSData *data=[NSData dataWithContentsOfURL:url options:NSDataReadingMappedIfSafe error:&failure];
            if(!data||data.length>512ULL*1024*1024||total+data.length>1024ULL*1024*1024) {
                failure=failure?:GuestError(@"Guest setup files exceed the transfer limit.");break;
            }
            [entries addObject:@{@"Name":name,@"Data":data}];total+=data.length;
        }
        dispatch_async(dispatch_get_main_queue(),^{
            if(failure||self.closed){finish(nil,failure?:GuestError(@"Guest transfer was cancelled."));return;}
            [self confirmPrivilegedAgent:^(NSError *error) {
                if(error){finish(nil,error);return;}
                NSString *base=persistent?@"/private/var/db":@"/private/var/tmp";
                [self runExecutable:@"/usr/bin/mktemp" arguments:@[@"-d",[base stringByAppendingPathComponent:@"VirtualIntelMac.XXXXXX"]] timeout:15 completion:^(NSDictionary *created,NSError *createError) {
                    NSString *directory=[Output(created) stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                    NSString *expression=[NSString stringWithFormat:@"^%@/VirtualIntelMac\\.[A-Za-z0-9]{6}$",[NSRegularExpression escapedPatternForString:base]];
                    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:expression options:0 error:NULL];
                    if(createError||!Successful(created)||!directory||[pattern numberOfMatchesInString:directory options:0 range:NSMakeRange(0,directory.length)]!=1) {
                        finish(nil,createError?:GuestError(@"The guest could not create a private transfer directory."));return;
                    }
                    if(progress)progress(0,total);
                    [self stageEntries:entries index:0 directory:directory completed:0 total:total
                        deadline:0 progress:progress completion:^(NSError *uploadError) {
                        if(uploadError) {
                            finish(nil,[NSError errorWithDomain:uploadError.domain code:uploadError.code userInfo:@{
                                NSLocalizedDescriptionKey:uploadError.localizedDescription,@"GuestStagingDirectory":directory}]);return;
                        }
                        NSMutableArray *records=[NSMutableArray array];
                        for(NSDictionary *entry in entries)[records addObject:@{@"Name":entry[@"Name"],@"Size":@([entry[@"Data"] length])}];
                        finish(@{@"Directory":directory,@"Files":records,@"FilesStaged":@YES},nil);
                    }];
                }];
            }];
        });
    });
}
- (void)stageEntries:(NSArray<NSDictionary *> *)entries index:(NSUInteger)index directory:(NSString *)directory
           completed:(uint64_t)completed total:(uint64_t)total deadline:(double)deadline
            progress:(void (^)(uint64_t,uint64_t))progress completion:(void (^)(NSError *))completion {
    if(index==entries.count){completion(nil);return;}
    NSDictionary *entry=entries[index];NSData *data=entry[@"Data"];
    NSString *path=[directory stringByAppendingPathComponent:entry[@"Name"]];
    [self writeData:data path:path deadline:deadline progress:^(NSUInteger count){if(progress)progress(completed+count,total);}
        completion:^(NSError *writeError) {
        if(writeError){completion(writeError);return;}
        [self runExecutable:@"/bin/chmod" arguments:@[[entry[@"Name"] isEqual:@"guest-macho"]?@"0700":@"0600",path] timeout:15 completion:^(NSDictionary *mode,NSError *modeError) {
            if(modeError||!Successful(mode)){completion(modeError?:GuestError(@"Could not secure a guest staging file."));return;}
            [self stageEntries:entries index:index+1 directory:directory completed:completed+data.length total:total
                deadline:deadline progress:progress completion:completion];
        }];
    }];
}
- (void)close { self.closed=YES;self.statusDirectory=nil;self.statusPayload=nil;[self.connection close]; }
@end
