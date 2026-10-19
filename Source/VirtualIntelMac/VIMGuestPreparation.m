#import "VIMGuestPreparation.h"
#import "VIMGuestAgent.h"
#import "VIMQMPConnection.h"
#import "VIMGuestProgress.h"
static NSError *PreparationError(NSString *text) {return [NSError errorWithDomain:@"VirtualIntelMac.GuestPreparation" code:1 userInfo:@{NSLocalizedDescriptionKey:text}];}
static BOOL CommandOK(NSDictionary *r) {return [r[@"exitcode"] isEqual:@0]&&!r[@"signal"];}
static NSDictionary *CommandJSON(NSDictionary *r) {
    id value=CommandOK(r)?[NSJSONSerialization JSONObjectWithData:r[@"stdout"]?:NSData.data options:0 error:NULL]:nil;
    return [value isKindOfClass:NSDictionary.class]?value:nil;
}
@interface VIMGuestPreparation ()
@property VIMGuestAgent *agent;
@property VIMQMPConnection *control;
@property VIMGuestProgress *progressUI;
@property BOOL supportInserted;
@property BOOL supportUnmounted;
@property BOOL headlessRepair;
@property double transferStarted;
@property NSURL *runtime;
@property NSURL *journal;
@property NSDictionary *initialReport;
@property NSString *archiveFilename;
@property NSMutableDictionary *state;
@property(copy) void (^progress)(NSString *);
@property(copy) void (^completion)(NSDictionary *,NSError *);
@end
@implementation VIMGuestPreparation
+ (void)prepareWithAgent:(VIMGuestAgent *)agent report:(NSDictionary *)report runtime:(NSURL *)runtime journal:(NSURL *)journal control:(VIMQMPConnection *)control
     allowHeadlessRepair:(BOOL)headlessRepair
                progress:(void (^)(NSString *))progress completion:(void (^)(NSDictionary *,NSError *))completion {
    VIMGuestPreparation *operation=[self new];operation.agent=agent;operation.runtime=runtime;operation.journal=journal;
    operation.control=control;operation.headlessRepair=headlessRepair;
    operation.initialReport=report;operation.progress=progress;operation.completion=completion;[operation start];
}
- (BOOL)savePhase:(NSString *)phase error:(NSError **)error {
    self.state[@"Phase"]=phase;self.state[@"UpdatedAt"]=NSDate.date;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:self.state format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
    return data&&[data writeToURL:self.journal options:NSDataWritingAtomic error:error];
}
- (void)finish:(NSDictionary *)receipt error:(NSError *)error {
    NSError *writeError=nil;
    if(error)self.state[@"Failure"]=error.localizedDescription;
    if(receipt)self.state[@"Receipt"]=receipt;
    if(error)[self.progressUI updateMode:@"failed" fraction:0 message:@"Setup couldn’t finish. Check Session Details in the Virtual Intel Mac app before trying again." completion:nil];
    if(self.state)[self savePhase:error?@"failed":@"prepared" error:&writeError];
    [self releaseSupportMedia:^(NSError *ejectError){self.completion(receipt,error?:writeError?:ejectError);}];
}
- (void)releaseSupportMedia:(void (^)(NSError *))completion {
    if(!self.supportInserted){completion(nil);return;}
    // macOS can retain PREVENT MEDIUM REMOVAL even after unmounting. Forcing
    // the tray is safe only after the verified volume is no longer mounted.
    [self.control execute:@"eject" arguments:@{@"id":@"setup-support-device",@"force":@(self.supportUnmounted)}
        completion:^(id result,NSError *error){if(!error)self.supportInserted=NO;completion(error);}];
}
- (BOOL)phase:(NSString *)phase message:(NSString *)message {
    NSError *error=nil;if(![self savePhase:phase error:&error]){[self finish:nil error:error];return NO;}
    NSDictionary *fractions=@{@"preparing-utilities":@.12,@"checking-environment":@.14,@"extracting":@.18,@"preparing":@.25,@"verifying-guest":@.42,@"planning-installation":@.46};
    if(fractions[phase])[self.progressUI updateMode:@"working" fraction:[fractions[phase] doubleValue] message:message completion:nil];
    self.progress(message);return YES;
}
// Disk Arbitration does not automount optical volumes before a GUI session.
// Only request a read-only mount of the uniquely named support disc; stage-media
// still verifies its entire manifest and every file before executing anything.
- (void)mountHeadlessSupport:(void (^)(NSError *))completion {
    [self mountHeadlessSupport:completion deadline:NSProcessInfo.processInfo.systemUptime+45];
}
- (void)mountHeadlessSupport:(void (^)(NSError *))completion deadline:(NSTimeInterval)deadline {
    [self.agent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"list",@"-plist"] timeout:20 completion:^(NSDictionary *result,NSError *failure){
        NSDictionary *list=CommandOK(result)?[NSPropertyListSerialization propertyListWithData:result[@"stdout"] options:0 format:NULL error:nil]:nil;
        if(failure||![list isKindOfClass:NSDictionary.class]){completion(failure?:PreparationError(@"The support disc couldn’t be located before login. Nothing was installed."));return;}
        NSMutableArray *queue=[list[@"AllDisksAndPartitions"] mutableCopy],*matches=[NSMutableArray new];
        if(![queue isKindOfClass:NSMutableArray.class]||queue.count>128){completion(PreparationError(@"The guest disk list was not recognized."));return;}
        for(NSUInteger i=0;i<queue.count&&i<128;i++) {
            NSDictionary *entry=queue[i];if(![entry isKindOfClass:NSDictionary.class])continue;
            if([entry[@"VolumeName"] isEqual:@"Virtual Intel Mac Support"]&&[entry[@"DeviceIdentifier"] isKindOfClass:NSString.class])[matches addObject:entry[@"DeviceIdentifier"]];
            if([entry[@"Partitions"] isKindOfClass:NSArray.class]&&queue.count+[entry[@"Partitions"] count]<=128)[queue addObjectsFromArray:entry[@"Partitions"]];
        }
        NSString *identifier=matches.count==1?matches.firstObject:nil;
        if(!matches.count&&NSProcessInfo.processInfo.systemUptime<deadline) {
            // Insertion is acknowledged before the guest's asynchronous CD/HFS
            // probe publishes its partition label. Retry discovery only, never
            // repeat a mount with an uncertain outcome.
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{[self mountHeadlessSupport:completion deadline:deadline];});return;
        }
        NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^disk[0-9]+(s[0-9]+)*$" options:0 error:nil];
        if(!identifier||[pattern numberOfMatchesInString:identifier options:0 range:NSMakeRange(0,identifier.length)]!=1){completion(PreparationError(@"The guest support disc is missing or ambiguous. Nothing was installed."));return;}
        [self.agent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"info",@"-plist",identifier] timeout:15 completion:^(NSDictionary *reply,NSError *issue){
            NSDictionary *info=CommandOK(reply)?[NSPropertyListSerialization propertyListWithData:reply[@"stdout"] options:0 format:NULL error:nil]:nil;
            if(issue||![info[@"VolumeName"] isEqual:@"Virtual Intel Mac Support"]||![info[@"WritableMedia"] isEqual:@NO]){completion(issue?:PreparationError(@"The support disc isn’t verified as read-only. Nothing was installed."));return;}
            [self.agent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"mount",@"readOnly",identifier] timeout:30 completion:^(NSDictionary *mounted,NSError *error){
                completion(error?:(!CommandOK(mounted)?PreparationError(@"The read-only support disc couldn’t be mounted before login. Nothing was installed."):nil));
            }];
        }];
    }];
}
- (void)start {
    NSAssert(NSThread.isMainThread,@"Guest preparation is main-thread owned");
    NSDictionary *report=self.initialReport;NSString *guest=report[@"Guest"];
    if(![@[@"mojave",@"catalina",@"bigsur"] containsObject:guest]||![report[@"LiveRoot"] isEqual:@YES]||
       ![report[@"KernelMatchesSystem"] isEqual:@YES]||![report[@"InstalledSystemCandidate"] isEqual:@YES]||
       (!self.headlessRepair&&![report[@"DesktopRunning"] isEqual:@YES])||
       ![report[@"VolumeUUID"] isKindOfClass:NSString.class]||![report[@"VolumeUUID"] length]) {
        self.completion(nil,PreparationError(@"Preparation requires a matching installed Mojave, Catalina or Big Sur system with verified volume identity. Initial setup also requires a usable desktop."));return;
    }
    NSURL *payloads=[self.runtime URLByAppendingPathComponent:@"GuestPayloads"];
    NSDictionary *manifest=[NSDictionary dictionaryWithContentsOfURL:[payloads URLByAppendingPathComponent:@"Payloads.plist"]];
    NSDictionary *record=[manifest[@"Guests"] isKindOfClass:NSDictionary.class]?manifest[@"Guests"][guest]:nil;
    if(![record isKindOfClass:NSDictionary.class])record=nil;
    NSString *archiveName=record[@"Archive"],*hash=record[@"SHA256"];
    BOOL xz=[archiveName isEqual:[guest stringByAppendingString:@".tar.xz"]];
    self.archiveFilename=xz?@"payload.tar.xz":@"payload.tar.gz";
    NSCharacterSet *invalid=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet];
    if(![manifest[@"FormatVersion"] isEqual:@1]||(!xz&&![archiveName isEqual:[guest stringByAppendingString:@".tar.gz"]])||
       ![hash isKindOfClass:NSString.class]||hash.length!=64||[hash rangeOfCharacterFromSet:invalid].location!=NSNotFound) {
        self.completion(nil,PreparationError(@"The bundled guest payload manifest is missing or invalid."));return;
    }
    NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
    if(![fm createDirectoryAtURL:self.journal.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:&error]){self.completion(nil,error);return;}
    if([fm fileExistsAtPath:self.journal.path]) {
        NSURL *history=[self.journal.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"GuestPreparationHistory"];
        if(![fm createDirectoryAtURL:history withIntermediateDirectories:YES attributes:nil error:&error]||
           ![fm copyItemAtURL:self.journal toURL:[history URLByAppendingPathComponent:[NSUUID.UUID.UUIDString stringByAppendingString:@".plist"]] error:&error]) {
            self.completion(nil,error);return;
        }
    }
    self.state=[@{@"FormatVersion":@1,@"Attempt":NSUUID.UUID.UUIDString,@"HeadlessUpdateRepair":@(self.headlessRepair),@"InitialGuestReport":report,
                   @"ExpectedArchiveSHA256":hash,@"Installed":@NO} mutableCopy];
    if(![self phase:@"transferring" message:@"Making guest setup files available…"])return;
    self.transferStarted=NSProcessInfo.processInfo.systemUptime;
    NSURL *tools=[self.runtime URLByAppendingPathComponent:@"GuestTools"];
    NSURL *media=[payloads URLByAppendingPathComponent:[guest stringByAppendingString:@".iso"]];
    NSURL *supportManifest=[payloads URLByAppendingPathComponent:[guest stringByAppendingString:@"-support.plist"]];
    NSDictionary *support=[NSDictionary dictionaryWithContentsOfURL:supportManifest];
    if(![support[@"FormatVersion"] isEqual:@1]||![support[@"Guest"] isEqual:guest]||
       ![support[@"Files"][self.archiveFilename][@"SHA256"] isEqual:hash]||![fm fileExistsAtPath:media.path]) {
        [self finish:nil error:PreparationError(@"This app’s guest support disc is incomplete. Reinstall the app and try again.")];return;
    }
    [self.control execute:@"blockdev-change-medium" arguments:@{@"id":@"setup-support-device",@"filename":media.path,@"format":@"raw",@"read-only-mode":@"read-only"}
        completion:^(id inserted,NSError *insertError) {
        if(insertError){[self finish:nil error:insertError];return;}
        self.supportInserted=YES;
        [self.agent stageInstallationFiles:@{@"guest-macho":[tools URLByAppendingPathComponent:@"guest-macho"],
            @"guest-progress":[tools URLByAppendingPathComponent:@"guest-progress"],@"SupportIcon.png":[tools URLByAppendingPathComponent:[guest stringByAppendingString:@"-support-icon.png"]],@"support-manifest.plist":supportManifest}
            progress:nil completion:^(NSDictionary *bootstrap,NSError *bootstrapError) {
        if(bootstrapError){[self finish:nil error:bootstrapError];return;}
        NSString *directory=bootstrap[@"Directory"];
        self.state[@"GuestStagingDirectory"]=directory;
        void (^continueSetup)(VIMGuestProgress *,NSError *)=^(VIMGuestProgress *progressUI,NSError *uiError){
        if(uiError){[self finish:nil error:uiError];return;}
        self.progressUI=progressUI;self.state[@"GuestProgress"]=progressUI.receipt;
        self.progress(@"Copying and verifying guest setup files…");
        [progressUI updateMode:@"working" fraction:.05 message:@"Copying and verifying guest support files…" completion:nil];
        [self.agent runExecutable:[directory stringByAppendingPathComponent:@"guest-macho"]
            arguments:@[@"stage-media",[directory stringByAppendingPathComponent:@"support-manifest.plist"],directory] timeout:180
            completion:^(NSDictionary *copied,NSError *failure) {
        NSDictionary *transfer=CommandJSON(copied);
        if(failure){if(failure.userInfo[@"GuestStagingDirectory"])self.state[@"GuestStagingDirectory"]=failure.userInfo[@"GuestStagingDirectory"];[self finish:nil error:failure];return;}
        if(![transfer[@"Directory"] isEqual:directory]) {
            NSString *detail=[[NSString alloc] initWithData:copied[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
            [self finish:nil error:PreparationError(detail.length?detail:@"Could not verify the private guest support copy. Nothing was installed.")];return;
        }
        self.state[@"Transfer"]=transfer;self.state[@"GuestStagingDirectory"]=transfer[@"Directory"];
        self.state[@"TransferSeconds"]=@(NSProcessInfo.processInfo.systemUptime-self.transferStarted);
        BOOL match=NO;for(NSDictionary *file in transfer[@"Files"])if([file[@"Name"] isEqual:self.archiveFilename]&&[file[@"SHA256"] isEqual:hash])match=YES;
        if(!match||![transfer[@"SHA256Verified"] isEqual:@YES]){[self finish:nil error:PreparationError(@"Transferred archive does not match the bundled manifest. Nothing was installed.")];return;}
        NSString *device=transfer[@"MediaDevice"];
        NSRegularExpression *disk=[NSRegularExpression regularExpressionWithPattern:@"^/dev/disk[0-9]+(s[0-9]+)*$" options:0 error:NULL];
        if(![device isKindOfClass:NSString.class]||[disk numberOfMatchesInString:device options:0 range:NSMakeRange(0,device.length)]!=1) {
            [self finish:nil error:PreparationError(@"Could not identify the verified support disc for removal. Nothing was installed.")];return;
        }
        // Tell guest Disk Arbitration first. A forced host-only tray removal
        // can leave the now-invalid mounted volume visible in Finder.
        [self.agent runExecutable:@"/usr/sbin/diskutil" arguments:@[@"unmount",device] timeout:60 completion:^(NSDictionary *unmounted,NSError *unmountError){
        NSMutableDictionary *unmountEvidence=[NSMutableDictionary new];
        for(NSString *key in @[@"exitcode",@"signal"])if(unmounted[key])unmountEvidence[key]=unmounted[key];
        for(NSString *key in @[@"stdout",@"stderr"]) {
            NSData *data=unmounted[key];
            if([data isKindOfClass:NSData.class]&&data.length)
                unmountEvidence[key]=[[NSString alloc] initWithData:[data subdataWithRange:NSMakeRange(0,MIN(data.length,8192UL))]
                    encoding:NSUTF8StringEncoding]?:@"Non-text disk unmount response";
        }
        self.state[@"SupportUnmountResult"]=unmountEvidence;
        if(unmountError||!CommandOK(unmounted)) {
            NSString *message=[unmountEvidence[@"stderr"] containsString:@"loginwindow"]?
                @"macOS kept the support disc in use. Unlock your virtual Mac, then try setup again. No system files were installed.":
                @"macOS couldn’t release the setup disc. Close any windows using it, then try setup again. No system files were installed.";
            [self finish:nil error:unmountError?:PreparationError(message)];return;
        }
        self.supportUnmounted=YES;
        [self releaseSupportMedia:^(NSError *ejectError){
            if(ejectError){[self finish:nil error:ejectError];return;}
            [self extractInDirectory:transfer[@"Directory"]];
        }];
        }];
        }];
        };
        // An update can replace framebuffer support before WindowServer starts.
        // A previously verified update repair reports progress in the host app;
        // opening a guest AppKit window here would deadlock on WindowServer.
        if(self.headlessRepair)[self mountHeadlessSupport:^(NSError *mountError){continueSetup(nil,mountError);}];
        else [VIMGuestProgress startWithAgent:self.agent staging:directory completion:continueSetup];
        }];
    }];
}
- (void)extractInDirectory:(NSString *)directory {
    if(![self phase:@"preparing-utilities" message:@"Preparing guest setup utilities…"])return;
    NSString *prepare=[directory stringByAppendingPathComponent:@"guest-prepare"],*patch=[directory stringByAppendingPathComponent:@"guest-image-patch"];
    [self.agent runExecutable:@"/bin/chmod" arguments:@[@"0700",prepare,patch,[directory stringByAppendingPathComponent:@"guest-install"],[directory stringByAppendingPathComponent:@"codesign_allocate"]] timeout:15 completion:^(NSDictionary *mode,NSError *error) {
        if(error||!CommandOK(mode)){[self finish:nil error:error?:PreparationError(@"Could not prepare guest utilities.")];return;}
        if(![self phase:@"checking-environment" message:@"Checking guest setup requirements…"])return;
        [self.agent runExecutable:[directory stringByAppendingPathComponent:@"guest-install"] arguments:@[@"--check-environment"] timeout:30 completion:^(NSDictionary *checked,NSError *checkError) {
        NSDictionary *admission=CommandJSON(checked);
        NSString *diagnostic=[[NSString alloc] initWithData:checked[@"stderr"]?:NSData.data encoding:NSUTF8StringEncoding];
        if(diagnostic.length)self.state[@"AdmissionLog"]=diagnostic;
        if(checkError||![admission[@"Eligible"] isEqual:@YES]||
           ![admission[@"VolumeUUID"] isEqual:self.initialReport[@"VolumeUUID"]]||
           ![admission[@"ProductBuildVersion"] isEqual:self.initialReport[@"ProductBuildVersion"]]) {
            [self finish:nil error:checkError?:PreparationError(diagnostic.length?diagnostic:@"The guest does not meet setup requirements or changed since it was checked. Nothing was installed.")];return;
        }
        self.state[@"Admission"]=admission;
        if(![self phase:@"extracting" message:@"Unpacking guest setup files…"])return;
        NSString *extracted=[directory stringByAppendingPathComponent:@"Extracted"];
        [self.agent runExecutable:prepare arguments:@[@"--extract",[directory stringByAppendingPathComponent:self.archiveFilename],extracted] timeout:300 completion:^(NSDictionary *result,NSError *extractError) {
            if([result[@"stderr"] length])self.state[@"ExtractionLog"]=[[NSString alloc] initWithData:result[@"stderr"] encoding:NSUTF8StringEncoding]?:@"Non-text extraction output";
            NSDictionary *receipt=CommandJSON(result);NSString *payload=[extracted stringByAppendingPathComponent:@"payload"];
            if(receipt)self.state[@"Extraction"]=receipt;
            // Foundation in Catalina canonicalizes /private/var to /var.
            // Accept only that exact alias of our known private staging path.
            BOOL samePath=[receipt[@"PayloadDirectory"] isEqual:payload];
            if([payload hasPrefix:@"/private/var/tmp/VirtualIntelMac."]||[payload hasPrefix:@"/private/var/db/VirtualIntelMac."])
                samePath|=[receipt[@"PayloadDirectory"] isEqual:[payload substringFromIndex:8]];
            if(extractError||![receipt[@"Extracted"] isEqual:@YES]||![receipt[@"Installed"] isEqual:@NO]||!samePath) {
                [self finish:nil error:extractError?:PreparationError(@"Guest payload extraction failed. Diagnostic staging was retained.")];return;
            }
            if(![self phase:@"preparing" message:@"Preparing this macOS build’s graphics files…"])return;
            [self.agent runExecutable:prepare arguments:@[@"/",payload,directory] timeout:600 completion:^(NSDictionary *prepared,NSError *prepareError) {
                if([prepared[@"stderr"] length])self.state[@"PreparationLog"]=[[NSString alloc] initWithData:prepared[@"stderr"] encoding:NSUTF8StringEncoding]?:@"Non-text preparation output";
                NSDictionary *result=CommandJSON(prepared);
                if(prepareError||![result[@"Prepared"] isEqual:@YES]||![result[@"Installed"] isEqual:@NO]||
                   ![result[@"Guest"] isEqual:self.initialReport[@"Guest"]]||![result[@"ProductBuildVersion"] isEqual:self.initialReport[@"ProductBuildVersion"]]) {
                    [self finish:nil error:prepareError?:PreparationError(@"Guest file preparation failed. Nothing was installed; diagnostic staging was retained.")];return;
                }
                self.state[@"Preparation"]=result;
                if(![self phase:@"verifying-guest" message:@"Verifying the prepared guest…"])return;
                NSData *status=[NSData dataWithContentsOfURL:[self.runtime URLByAppendingPathComponent:@"GuestTools/guest-status"]];
                [self.agent collectStatusWithTool:status completion:^(NSDictionary *report,NSError *statusError) {
                    if(statusError||![report[@"LiveRoot"] isEqual:@YES]||![report[@"KernelMatchesSystem"] isEqual:@YES]||
                       ![report[@"VolumeUUID"] isEqual:self.initialReport[@"VolumeUUID"]]||![report[@"ProductBuildVersion"] isEqual:self.initialReport[@"ProductBuildVersion"]]) {
                        [self finish:nil error:statusError?:PreparationError(@"The guest changed while setup files were being prepared.")];return;
                    }
                    if(![self phase:@"planning-installation" message:@"Checking installation targets and recording recovery information…"])return;
                    NSString *journal=[directory stringByAppendingPathComponent:@"InstallTransaction.plist"];
                    [self.agent runExecutable:[directory stringByAppendingPathComponent:@"guest-install"] arguments:@[@"--plan",@"/",payload,journal] timeout:300 completion:^(NSDictionary *planned,NSError *planError) {
                        if([planned[@"stderr"] length])self.state[@"PlanningLog"]=[[NSString alloc] initWithData:planned[@"stderr"] encoding:NSUTF8StringEncoding]?:@"Non-text planning output";
                        NSDictionary *plan=CommandJSON(planned);
                        if(planError||![plan[@"Planned"] isEqual:@YES]||![plan[@"Installed"] isEqual:@NO]||![plan[@"Ready"] isEqual:@NO]||![plan[@"Guest"] isEqual:self.initialReport[@"Guest"]]) {
                            [self finish:nil error:planError?:PreparationError(@"Installation planning failed. Nothing was installed; diagnostics were retained.")];return;
                        }
                        self.state[@"InstallationPlan"]=plan;
                        if(![plan[@"TransactionID"] isKindOfClass:NSString.class]||![[NSUUID alloc] initWithUUIDString:plan[@"TransactionID"]]) {
                            [self finish:nil error:PreparationError(@"Installation plan has no valid transaction identity.")];return;
                        }
                        [self finish:@{@"Prepared":@YES,@"Planned":@YES,@"Installed":@NO,@"GuestReport":report,@"PayloadDirectory":payload,@"TransactionJournal":journal,@"TransactionID":plan[@"TransactionID"],@"GuestProgress":self.progressUI.receipt?:@{}} error:nil];
                    }];
                }];
            }];
        }];
        }];
    }];
}
@end
