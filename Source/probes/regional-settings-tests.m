#import <Cocoa/Cocoa.h>
#import "VIMMachine.h"
#import "VIMModernSetup.h"
#import "VIMGuestAgent.h"
#import "VIMRegionalSettings.h"
#import "VIMRegionalSettingsView.h"
#import "VIMOpenGLSetup.h"
#import "VIMAutomaticSetup.h"
#import "VIMQMPConnection.h"
#import "VIMNewMachineWindow.h"

@interface VIMNewMachineWindow (RegionalOptionsTests)
- (NSStackView *)automaticSetupSection;
- (void)finishAutomaticOptions:(NSButton *)sender;
@end
@interface RegionalOptionsParent : NSWindow
@property BOOL acceptedOptions;
@end
@implementation RegionalOptionsParent
- (void)endSheet:(NSWindow *)sheet returnCode:(NSModalResponse)response { self.acceptedOptions=response==NSModalResponseOK; }
@end

@interface VIMModernSetup (RegionalTests)
- (NSDictionary *)requestForReport:(NSDictionary *)report;
- (void)prepareRegionalForBoot:(NSString *)boot completion:(void (^)(NSError *))completion;
@end

@interface VIMOpenGLSetup (RegionalTests)
- (void)runAccountToolForSharingRepair:(BOOL)repair input:(NSData *)input completion:(void (^)(NSDictionary *,NSError *))completion;
@end
@interface VIMRegionalSettings (PreflightTests)
+ (void)preflightRequest:(NSDictionary *)request agent:(VIMGuestAgent *)agent executable:(NSString *)executable packageURL:(NSURL *)package completion:(void (^)(NSError *))completion;
@end
@interface RegionalAgent : VIMGuestAgent
@property NSDictionary *reply;
@property NSError *failure;
@property NSUInteger probes;
@property NSUInteger stages;
@property NSString *lastProgram;
@property NSData *planInput;
@property NSMutableArray *commands;
@end
@implementation RegionalAgent
- (void)stageFiles:(NSDictionary *)files progress:(void (^)(uint64_t,uint64_t))progress completion:(void (^)(NSDictionary *,NSError *))completion {
    self.stages++;completion(@{@"Directory":@"/private/var/tmp/VirtualIntelMac.CurrentRegional"},nil);
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    self.lastProgram=path;
    if(!self.commands)self.commands=[NSMutableArray new];[self.commands addObject:arguments.firstObject?:path];
    if([arguments.firstObject isEqual:@"--regional-plan"]){self.probes++;completion(self.reply,self.failure);}
    else completion(@{@"exitcode":@0},nil);
}
- (void)runExecutable:(NSString *)path arguments:(NSArray *)arguments standardInput:(NSData *)input timeout:(NSTimeInterval)timeout completion:(void (^)(NSDictionary *,NSError *))completion {
    if([arguments.firstObject isEqual:@"--regional-plan"])self.planInput=input;
    [self runExecutable:path arguments:arguments timeout:timeout completion:completion];
}
@end

static void Check(BOOL good, NSString *message) {
    if (!good) { fprintf(stderr, "%s\n", message.UTF8String); exit(1); }
}

static VIMMachine *Fixture(NSURL *root, NSString *guest) {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *identifier=NSUUID.UUID.UUIDString;
    NSURL *package=[root URLByAppendingPathComponent:[identifier stringByAppendingString:@".vintelmac"]];
    for (NSString *name in @[@"Disks",@"Firmware",@"State",@"Installation"])
        Check([fm createDirectoryAtURL:[package URLByAppendingPathComponent:name] withIntermediateDirectories:YES attributes:nil error:NULL], @"Cannot create isolated fixture");
    for (NSString *name in @[@"Disks/System.qcow2",@"Firmware/NVRAM.fd",@"Installation/Installer.iso"])
        Check([@"fixture" writeToURL:[package URLByAppendingPathComponent:name] atomically:YES encoding:NSUTF8StringEncoding error:NULL], @"Cannot write fixture");
    NSDictionary *config=@{@"FormatVersion":@1,@"Identifier":identifier,@"Name":@"Regional test",@"Guest":guest,@"CPUCount":@4,@"MemoryMiB":@4096,
        @"Disk":@"Disks/System.qcow2",@"NVRAM":@"Firmware/NVRAM.fd",@"Firmware":@"Firmware/CODE.fd",@"BootMedia":@"Firmware/EFI.img",@"FirmwareSource":@"application",
        @"InstallState":@"installing",@"InstallerMedia":@"Installation/Installer.iso"};
    Check([config writeToURL:[package URLByAppendingPathComponent:@"Machine.plist"] atomically:YES], @"Cannot write configuration");
    Check([@{@"FormatVersion":@1,@"VolumeUUID":NSUUID.UUID.UUIDString,@"Filesystem":@"HFS+"} writeToURL:[package URLByAppendingPathComponent:@"State/InstallationTarget.plist"] atomically:YES], @"Cannot write installation identity");
    NSError *error=nil; VIMMachine *machine=[VIMMachine loadURL:package error:&error];
    Check(machine!=nil, error.localizedDescription?:@"Cannot load fixture"); return machine;
}

int main(void) { @autoreleasepool {
    [NSApplication sharedApplication];
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *root=[fm.temporaryDirectory URLByAppendingPathComponent:[@"vim-regional-profile-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    Check([fm createDirectoryAtURL:root withIntermediateDirectories:YES attributes:nil error:NULL], @"Cannot create test directory");
    @try {
        NSDictionary *regional=@{@"FormatVersion":@1,@"Source":@"custom",@"PreferredLanguages":@[@"vi-VN",@"en-GB"],@"RegionCode":@"VN",@"TimeZoneIdentifier":@"Asia/Ho_Chi_Minh",@"UnsupportedPolicy":@"fallback"};
        VIMRegionalSettingsView *optional=[[VIMRegionalSettingsView alloc] initWithRequest:nil];NSError *optionalError=nil;
        Check(![optional requestWithError:&optionalError]&&!optionalError,@"Experimental regional settings must be disabled by default without producing a request or validation error");
        NSButton *experimental=[optional valueForKey:@"experimental"];
        Check(experimental.state==NSControlStateValueOff&&![[optional valueForKey:@"followHost"] isEnabled],@"A fresh advanced panel enabled the experimental feature implicitly");
        NSDictionary *offDraft=optional.draftState;
        [experimental performClick:nil];
        Check([[optional requestWithError:NULL][@"Source"] isEqual:@"host"],@"Explicitly enabling the experiment must allow following host settings");
        [optional restoreRequest:regional];NSDictionary *onDraft=optional.draftState;
        [experimental performClick:nil];
        NSMutableDictionary *disabledDraft=[optional.draftState mutableCopy];disabledDraft[@"Zone"]=@"../../invalid";[optional restoreDraftState:disabledDraft];
        Check(![optional requestWithError:&optionalError]&&!optionalError,@"Disabled experimental inputs must not validate or generate installation settings");
        [optional restoreDraftState:onDraft];
        Check([[optional requestWithError:NULL] isEqual:regional],@"Cancelling advanced options must restore the experimental opt-in and custom values");
        [optional restoreDraftState:offDraft];
        Check(![optional requestWithError:NULL]&&experimental.state==NSControlStateValueOff,@"Cancelling an experimental opt-in must restore the disabled state");
        [optional restoreRequest:nil];optional.frozen=YES;
        Check(![optional requestWithError:NULL]&&!experimental.enabled,@"Resuming a task without regional intent must remain disabled and immutable");
        [optional restoreRequest:regional];
        Check([[optional requestWithError:NULL] isEqual:regional]&&!experimental.enabled,@"Resuming an explicitly configured task must retain its frozen regional intent");
        optional.frozen=NO;optional.enabled=NO;
        Check(![optional requestWithError:NULL]&&!experimental.enabled,@"Turning off automatic setup must also suppress the experimental request");
        RegionalOptionsParent *optionsParent=[[RegionalOptionsParent alloc] initWithContentRect:NSMakeRect(0,0,480,600) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
        VIMNewMachineWindow *options=[[VIMNewMachineWindow alloc] initWithWindow:optionsParent];
        NSStackView *advanced=[options automaticSetupSection];
        [advanced.widthAnchor constraintEqualToConstant:432].active=YES;
        VIMRegionalSettingsView *advancedRegional=[options valueForKey:@"regionalView"];
        Check([advancedRegional isDescendantOf:advanced]&&![advancedRegional requestWithError:NULL],@"The advanced panel must host the experiment without enabling it");
        [options setValue:[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,480,600) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO] forKey:@"automaticSheet"];
        NSButton *accept=[NSButton new];accept.tag=1;[options finishAutomaticOptions:accept];
        Check(optionsParent.acceptedOptions,@"Advanced options cannot be accepted while the experimental feature is disabled");
        fprintf(stderr,"Advanced regional section fitting size: %.0f x %.0f\n",advanced.fittingSize.width,advanced.fittingSize.height);
        for(NSDictionary *invalid in @[@{@"Resolved":@{@"DesktopLanguage":@1}},@{@"Resolved":@{@"LocaleIdentifier":@[]}},@{@"Resolved":@{},@"Warnings":@"not a list"}]) {
            BOOL rejected=NO;
            @try { rejected=[[VIMRegionalSettings summaryForResult:invalid] isEqual:@""]; } @catch(NSException *exception) { (void)exception; }
            Check(rejected,@"A damaged regional receipt must not crash settings or display an invented result");
        }
        NSArray *globalLanguages=CFBridgingRelease(CFPreferencesCopyValue(CFSTR("AppleLanguages"),kCFPreferencesAnyApplication,kCFPreferencesCurrentUser,kCFPreferencesAnyHost));
        NSDictionary *host=[VIMRegionalSettings hostSnapshotWithError:NULL];
        Check([host[@"Source"] isEqual:@"host"]&&[host[@"PreferredLanguages"] count], @"Host snapshot is missing ordered preferences");
        if(globalLanguages.count)Check([[host[@"PreferredLanguages"] firstObject] isEqual:[NSLocale canonicalLanguageIdentifierFromString:globalLanguages.firstObject]], @"An application language override leaked into the host snapshot");
        Check([[VIMRegionalSettings.languageChoices valueForKey:@"Value"] containsObject:@"de"]&&[[VIMRegionalSettings.languageChoices valueForKey:@"Value"] containsObject:@"ja"], @"Language selection was limited to the app translations");
        VIMRegionalSettingsView *editor=[[VIMRegionalSettingsView alloc] initWithRequest:regional];
        Check([[editor requestWithError:NULL] isEqual:regional], @"Restoring an unfinished setup lost its secondary language preferences");
        NSDictionary *draft=editor.draftState;NSMutableDictionary *edited=[draft mutableCopy];edited[@"Zone"]=@"../../etc/passwd";[editor restoreDraftState:edited];
        Check(![editor requestWithError:NULL], @"The settings editor accepted a path as a time zone");
        [editor restoreDraftState:draft];Check([[editor requestWithError:NULL] isEqual:regional], @"Cancelling regional edits changed the creation policy");
        NSMutableDictionary *custom=[draft mutableCopy];custom[@"Languages"]=@[@"fr",@"de",@"en"];custom[@"Fallback"]=@0;
        [editor restoreDraftState:custom];
        Check([[[editor requestWithError:NULL] objectForKey:@"PreferredLanguages"] isEqual:@[@"fr",@"de",@"en"]], @"Custom languages cannot replace hidden host languages in user order");
        custom[@"Languages"]=@[@"en",@"fr"];[editor restoreDraftState:custom];
        Check([[[editor requestWithError:NULL] objectForKey:@"PreferredLanguages"] isEqual:@[@"en",@"fr"]], @"Custom language preferences cannot be reordered or removed");
        [(NSTokenField *)[editor valueForKey:@"language"] setObjectValue:nil];
        NSError *emptyError=nil;
        Check(![editor requestWithError:&emptyError]&&emptyError, @"An empty language list did not produce a validation error");
        [editor restoreDraftState:draft];
        editor.frozen=YES;[editor restoreDraftState:edited];Check([[editor requestWithError:NULL] isEqual:regional], @"An installation in progress resampled or changed its regional snapshot");
        NSDictionary *base=@{@"FormatVersion":@1,@"Enabled":@YES,@"Username":@"newuser",@"FileSharing":@YES,@"RemoteLogin":@YES,@"AutomaticLogin":@YES,@"CredentialIdentifier":NSUUID.UUID.UUIDString};
        for (NSString *guest in @[@"mojave",@"catalina",@"bigsur"]) {
            VIMMachine *machine=Fixture(root,guest);
            NSMutableDictionary *profile=[base mutableCopy]; profile[@"RegionalSettings"]=regional;
            NSError *error=nil;
            Check([machine saveAutomaticSetup:profile error:&error], [@"Regional settings rejected: " stringByAppendingString:error.localizedDescription?:@""]);
            NSURL *savedURL=[machine.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"];
            NSDictionary *saved=[NSDictionary dictionaryWithContentsOfURL:savedURL];
            Check([saved[@"RegionalSettings"] isEqual:regional], @"The user's requested language/region/timezone was changed while saving");
            Check(!saved[@"Password"], @"A non-secret policy contains credentials");
            NSDictionary *receipt=@{@"Requested":regional,@"Resolved":@{@"DesktopLanguage":@"en",@"LocaleIdentifier":@"en_VN",@"TimeZoneIdentifier":@"Asia/Saigon",@"Password":@"never-export"},
                @"Warnings":@[@"DesktopLanguageFallback"],@"Skipped":@[],@"Phase":@"verified",@"Steps":@[@{@"Secret":@"never-export"}]};
            NSMutableDictionary *mapped=[receipt mutableCopy],*mappedResolution=[receipt[@"Resolved"] mutableCopy];
            mappedResolution[@"LanguageMappings"]=@[@{@"RequestedLanguage":@"en-IN",@"AcceptedLanguage":@"en-GB",@"NativeLanguage":@"en_GB",@"PreferenceLanguage":@"en-IN",@"PreferenceEncoding":@"en-IN",@"Password":@"never-export"}];
            mapped[@"Resolved"]=mappedResolution;receipt=mapped;
            Check([VIMRegionalSettings storeResult:receipt requested:regional packageURL:machine.URL error:NULL], @"A matching verified receipt was not persisted");
            NSDictionary *clean=[NSDictionary dictionaryWithContentsOfURL:[machine.URL URLByAppendingPathComponent:@"State/RegionalSettings.plist"]];
            Check([clean[@"Resolved"][@"LocaleIdentifier"] isEqual:@"en_VN"]&&!clean[@"Resolved"][@"Password"]&&!clean[@"Steps"], @"Diagnostic receipt did not filter unrelated guest fields");
            NSDictionary *mapping=[clean[@"Resolved"][@"LanguageMappings"] firstObject];
            Check([mapping[@"AcceptedLanguage"] isEqual:@"en-GB"]&&!mapping[@"Password"], @"Accepted regional language mapping was lost or leaked unrelated guest data");
            Check([[VIMRegionalSettings summaryForResult:clean] containsString:@"→"], @"Regional result summary did not explain the actual language fallback");
            NSURL *receiptURL=[machine.URL URLByAppendingPathComponent:@"State/RegionalSettings.plist"];
            NSData *receiptBefore=[NSData dataWithContentsOfURL:receiptURL];
            for(NSDictionary *badResolution in @[@{@"DesktopLanguage":@1},@{@"UserLanguages":@"en"},@{@"LanguageMappings":@"not mappings"}]) {
                NSMutableDictionary *invalidReceipt=[receipt mutableCopy];invalidReceipt[@"Resolved"]=badResolution;NSError *receiptError=nil;
                Check(![VIMRegionalSettings storeResult:invalidReceipt requested:regional packageURL:machine.URL error:&receiptError]&&receiptError,@"Invalid recognized receipt fields need a validation error");
                Check([[NSData dataWithContentsOfURL:receiptURL] isEqual:receiptBefore],@"Rejected guest metadata replaced the last valid regional receipt");
            }
            NSMutableDictionary *foreign=[receipt mutableCopy];foreign[@"Requested"]=@{};
            Check(![VIMRegionalSettings storeResult:foreign requested:regional packageURL:machine.URL error:NULL], @"A result from another regional request was accepted");
            Check([machine saveAutomaticSetup:profile error:&error], @"An unchanged setup cannot resume");
            VIMGuestAgent *agent=[[VIMGuestAgent alloc] initWithSocketPath:@"/unused/regional-test.sock"];
            VIMModernSetup *setup=[[VIMModernSetup alloc] initWithMachine:machine agent:agent];
            NSDictionary *report=@{@"VolumeUUID":saved[@"VolumeUUID"]};
            Check([[setup requestForReport:report][@"RegionalSettings"] isEqual:regional], @"Modern account request lost the regional snapshot");
            NSMutableDictionary *plan=[@{@"MachineIdentifier":machine.configuration[@"Identifier"],@"VolumeUUID":saved[@"VolumeUUID"],@"RegionalSettings":regional} mutableCopy];
            NSMutableDictionary *state=[@{@"InstallDispatched":@YES,@"InstallerPlan":plan} mutableCopy];
            [setup setValue:state forKey:@"state"];
            Check([[setup requestForReport:report][@"NativeInstallDispatched"] boolValue], @"Matching installer policy lost Big Sur recovery authorization");
            plan[@"RegionalSettings"]=@{@"other":@YES};
            Check(![setup requestForReport:report][@"NativeInstallDispatched"], @"A different dispatched regional policy authorized Big Sur recovery");
            [plan removeObjectForKey:@"RegionalSettings"];
            Check(![setup requestForReport:report][@"NativeInstallDispatched"], @"Missing dispatched regional policy authorized Big Sur recovery");
            RegionalAgent *probe=[[RegionalAgent alloc] initWithSocketPath:@"/unused"];
            NSDictionary *planReply=@{@"FormatVersion":@1,@"Environment":@"Installed",@"Plan":@{@"PlanVersion":@2,@"Requested":regional,@"UserLanguages":@[@"vi-VN",@"en-GB"],@"DesktopLanguage":@"vi",@"DesktopNativeLanguage":@"vi",@"LoginLanguage":@"vi",@"LoginNativeLanguage":@"vi",@"LocaleIdentifier":@"vi_VN",@"TimeZoneIdentifier":@"Asia/Ho_Chi_Minh",@"Warnings":@[],@"Skipped":@[]}};
            probe.reply=@{@"exitcode":@0,@"stdout":[NSJSONSerialization dataWithJSONObject:planReply options:0 error:NULL]};
            [setup setValue:probe forKey:@"agent"];[setup setValue:@"/private/test-tools" forKey:@"helperDirectory"];
            __block BOOL called=NO;__block NSError *probeError=nil;
            void (^completed)(NSError *)=^(NSError *failure){called=YES;probeError=failure;};
            [setup prepareRegionalForBoot:@"boot-a" completion:completed];
            Check(called&&!probeError&&probe.probes==1, @"Installed guest-native plan did not precede account setup");
            Check([[NSJSONSerialization JSONObjectWithData:probe.planInput options:0 error:NULL] isEqual:regional], @"Guest-native preflight did not receive the exact non-secret regional request");
            [setup prepareRegionalForBoot:@"boot-a" completion:completed];
            Check(probe.probes==1, @"Regional capabilities were polled again in the same boot");
            [setup prepareRegionalForBoot:@"boot-b" completion:completed];
            Check(probe.probes==2, @"Regional capability cache crossed guest boots");
            NSMutableDictionary *recovery=[planReply mutableCopy];recovery[@"Environment"]=@"Recovery";
            probe.reply=@{@"exitcode":@0,@"stdout":[NSJSONSerialization dataWithJSONObject:recovery options:0 error:NULL]};
            [VIMRegionalSettings preflightRequest:regional agent:probe executable:@"/unused" packageURL:machine.URL completion:completed];
            Check(probeError!=nil, @"Recovery resources incorrectly authorized installed desktop settings");
            probe.reply=@{@"exitcode":@64};
            [VIMRegionalSettings preflightRequest:regional agent:probe executable:@"/unused" packageURL:machine.URL completion:completed];
            Check(!probeError, @"Fallback policy cannot continue with an older guest tool");
            NSDictionary *skipped=[NSDictionary dictionaryWithContentsOfURL:[machine.URL URLByAppendingPathComponent:@"State/RegionalSettings.plist"]];
            Check([skipped[@"Phase"] isEqual:@"skipped"]&&[skipped[@"Warnings"] containsObject:@"RegionalToolsUnavailable"], @"Older guest tools silently dropped regional intent");
            NSMutableDictionary *strict=[regional mutableCopy];strict[@"UnsupportedPolicy"]=@"strict";
            [VIMRegionalSettings preflightRequest:strict agent:probe executable:@"/unused" packageURL:machine.URL completion:completed];
            Check(probeError!=nil, @"Strict policy accepted an older guest tool");
            probe.reply=@{@"exitcode":@1};
            [VIMRegionalSettings preflightRequest:regional agent:probe executable:@"/unused" packageURL:machine.URL completion:completed];
            Check(probeError!=nil, @"A failed capability command was mistaken for unsupported settings");
            probe.reply=@{@"exitcode":@0,@"stdout":[@"{}" dataUsingEncoding:NSUTF8StringEncoding]};
            [VIMRegionalSettings preflightRequest:regional agent:probe executable:@"/unused" packageURL:machine.URL completion:completed];
            Check(probeError!=nil, @"Malformed capability output was accepted");
            // Exercise the real installed-system coordinator branch: a strict
            // failure must precede credential lookup and any account command.
            VIMModernSetup *blocked=[[VIMModernSetup alloc] initWithMachine:machine agent:probe];
            NSMutableDictionary *strictProfile=[saved mutableCopy];strictProfile[@"RegionalSettings"]=strict;
            [blocked setValue:strictProfile forKey:@"profile"];[blocked setValue:[NSMutableDictionary new] forKey:@"state"];
            NSString *boot=NSUUID.UUID.UUIDString;[blocked setValue:boot forKey:@"helperBoot"];[blocked setValue:@"/private/test-tools" forKey:@"helperDirectory"];
            probe.reply=@{@"exitcode":@64};[probe.commands removeAllObjects];
            NSMutableDictionary *installed=[report mutableCopy];installed[@"Guest"]=guest;installed[@"BootSessionUUID"]=boot;installed[@"LiveRoot"]=@YES;installed[@"KernelMatchesSystem"]=@YES;installed[@"InstalledRootPresent"]=@YES;
            called=NO;
            [blocked advanceReport:installed completion:^(NSDictionary *outcome,NSError *failure){called=YES;probeError=failure;}];
            Check(called&&probeError&&[probe.commands isEqual:@[@"--regional-plan"]], @"Modern strict regional failure reached account provisioning");
            NSData *prior=[NSData dataWithContentsOfURL:savedURL];
            NSMutableDictionary *different=[regional mutableCopy];different[@"RegionCode"]=@"US";profile[@"RegionalSettings"]=different;
            Check(![machine saveAutomaticSetup:profile error:&error], @"Resume overwrote the saved regional snapshot");
            Check([[NSData dataWithContentsOfURL:savedURL] isEqual:prior], @"Failed mutation changed setup state");
        }
        for(NSString *guest in @[@"tiger",@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra"]){
            VIMMachine *machine=Fixture(root,guest);NSMutableDictionary *profile=[base mutableCopy];profile[@"RegionalSettings"]=regional;
            Check([machine saveAutomaticSetup:profile error:NULL], @"Legacy profile rejected regional intent");
            NSDictionary *saved=[NSDictionary dictionaryWithContentsOfURL:[machine.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"]];
            NSMutableDictionary *policy=[@{@"FormatVersion":@1,@"Kind":@"VirtualIntelMacLegacySetup",@"Guest":guest,@"MachineIdentifier":machine.configuration[@"Identifier"],@"VolumeUUID":saved[@"VolumeUUID"]} mutableCopy];
            for(NSString *key in @[@"Enabled",@"Username",@"FileSharing",@"RemoteLogin",@"AutomaticLogin",@"RegionalSettings"])policy[key]=saved[key];
            [policy writeToURL:[machine.URL URLByAppendingPathComponent:@"State/LegacySetupMedia.plist"] atomically:YES];
            NSURL *iso=[machine.URL URLByAppendingPathComponent:@"State/LegacySetup.iso"];
            [@"cached-fixture-iso" writeToURL:iso atomically:YES encoding:NSUTF8StringEncoding error:NULL];
            Check([[VIMAutomaticSetup configurationMediaForMachine:machine error:NULL] isEqual:iso], @"Legacy configuration media cache lost regional policy");
            RegionalAgent *agent=[[RegionalAgent alloc] initWithSocketPath:@"/unused"];agent.reply=@{@"exitcode":@64};
            VIMOpenGLSetup *setup=[[VIMOpenGLSetup alloc] initWithMachine:machine agent:agent control:[[VIMQMPConnection alloc] initWithSocketPath:@"/unused-qmp"]];[setup setValue:@"boot-a" forKey:@"bootUUID"];
            __block NSError *failure=nil;
            [setup runAccountToolForSharingRepair:NO input:NSData.data completion:^(NSDictionary *reply,NSError *error){failure=error;}];
            BOOL period=YES;
            NSArray *firstCommands=period?@[@"0700",@"--regional-plan",@"--create"]:@[@"--regional-plan",@"--create"];
            Check(!failure&&[agent.commands isEqual:firstCommands], @"Legacy account dispatch did not prepare and probe the tool before account creation");
            if(period)Check(agent.stages==1&&[agent.lastProgram hasPrefix:@"/private/var/tmp/VirtualIntelMac.CurrentRegional/"], @"An unfinished period setup reused the installed account tool instead of the current bundled correction");
            [agent.commands removeAllObjects];
            [setup runAccountToolForSharingRepair:NO input:NSData.data completion:^(NSDictionary *reply,NSError *error){failure=error;}];
            Check(!failure&&[agent.commands isEqual:@[@"--create"]], @"Legacy coordinator polled regional capabilities within the same boot");
            if(period)Check(agent.stages==1,@"The same boot restaged a current period account tool");
        }
        VIMMachine *legacy=Fixture(root,@"mojave");
        Check([legacy saveAutomaticSetup:base error:NULL], @"Profiles created before regional settings must remain supported");
        NSDictionary *old=[NSDictionary dictionaryWithContentsOfURL:[legacy.URL URLByAppendingPathComponent:@"State/AutomaticSetup.plist"]];
        Check(!old[@"RegionalSettings"], @"An old task acquired new regional settings implicitly");
        RegionalAgent *disabledAgent=[[RegionalAgent alloc] initWithSocketPath:@"/unused"];
        VIMModernSetup *disabledSetup=[[VIMModernSetup alloc] initWithMachine:legacy agent:disabledAgent];
        __block BOOL disabledCalled=NO;
        [disabledSetup prepareRegionalForBoot:@"disabled-boot" completion:^(NSError *failure){disabledCalled=YES;Check(!failure,@"Disabled experiment failed regional preflight");}];
        Check(disabledCalled&&disabledAgent.probes==0,@"Disabled regional settings still invoked guest capability probing");
        VIMMachine *disabledLegacy=Fixture(root,@"tiger");
        Check([disabledLegacy saveAutomaticSetup:base error:NULL],@"Default legacy setup rejected an absent regional policy");
        RegionalAgent *legacyAgent=[[RegionalAgent alloc] initWithSocketPath:@"/unused"];
        VIMOpenGLSetup *legacySetup=[[VIMOpenGLSetup alloc] initWithMachine:disabledLegacy agent:legacyAgent control:[[VIMQMPConnection alloc] initWithSocketPath:@"/unused-qmp"]];
        __block BOOL legacyCalled=NO;
        [legacySetup runAccountToolForSharingRepair:NO input:NSData.data completion:^(NSDictionary *reply,NSError *failure){legacyCalled=YES;Check(!failure,@"Disabled legacy experiment blocked account dispatch");}];
        Check(legacyCalled&&legacyAgent.stages==0&&legacyAgent.probes==0&&[legacyAgent.commands isEqual:@[@"--create"]],@"A disabled legacy experiment staged or probed the regional helper");
        VIMMachine *bad=Fixture(root,@"bigsur");NSMutableDictionary *invalid=[base mutableCopy];
        invalid[@"RegionalSettings"]=@{@"FormatVersion":@1,@"PreferredLanguages":@[@"en"],@"TimeZoneIdentifier":@"../../etc/passwd"};
        Check(![bad saveAutomaticSetup:invalid error:NULL], @"An invalid request entered a durable installation policy");
        puts("PASS: regional request persistence, modern handoff, immutable resume, legacy profile compatibility and invalid-policy refusal");
    } @finally { [fm removeItemAtURL:root error:NULL]; }
} return 0; }
