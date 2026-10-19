// Native installation entry point. Planning is read-only with respect to the
// guest system. Explicit apply retains originals and cache backups; reboot
// verification is a separate operation and never inferred from copying files.
#import <Foundation/Foundation.h>
#import "VIMFileTransaction.h"
#import <Metal/Metal.h>
#import <objc/runtime.h>
#import <IOKit/IOKitLib.h>
#include "guest-install-targets.h"
#include <unistd.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/sysctl.h>
#include <dlfcn.h>

static void Check(BOOL ok,NSString *message){if(!ok)@throw [NSException exceptionWithName:@"InstallRejected" reason:message userInfo:nil];}
static NSString *KernelValue(const char *key) {
    size_t length=0;Check(sysctlbyname(key,NULL,&length,NULL,0)==0&&length>0&&length<65536,@"Required kernel identity is unavailable");
    NSMutableData *data=[NSMutableData dataWithLength:length];Check(sysctlbyname(key,data.mutableBytes,&length,NULL,0)==0,@"Cannot read kernel identity");
    return [[NSString alloc] initWithBytes:data.bytes length:strnlen(data.bytes,length) encoding:NSUTF8StringEncoding]?:@"";
}
static NSDictionary *LiveAdmissionWithPolicy(BOOL requireLibraryValidation) {
    Check(geteuid()==0,@"Guest installation requires root privileges");
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *release=version[@"ProductVersion"],*build=version[@"ProductBuildVersion"];
    Check([release isKindOfClass:NSString.class]&&([release hasPrefix:@"10.14."]||[release hasPrefix:@"10.15."]||[release hasPrefix:@"11."]),@"This installer must run inside a Mojave, Catalina or Big Sur guest, not on the host");
    Check([KernelValue("kern.osversion") isEqual:build],@"The running kernel does not match the installed system; finish updating and restart first");
    // Values and API from vendored xnu-6153.141.1/bsd/sys/csr.h:
    // Require untrusted kexts (bit 0) and unrestricted filesystem (bit 1).
    // kext_tools security.c:isInvalidSignatureAllowed uses bit 0. Bit 9 is
    // separate SystemPolicy approval, not a prerequisite for every kext load.
    // Report it separately; cache construction and postboot driver checks must
    // still succeed. Do not reject our existing 0x67 EFI policy solely for it.
    // Read only; never changes SIP or NVRAM.
    int (*csrCheck)(uint32_t)=dlsym(RTLD_DEFAULT,"csr_check");
    Check(csrCheck!=NULL,@"The guest SIP query API is unavailable");
    NSMutableArray *missing=[NSMutableArray array];
    for(NSDictionary *permission in @[@{@"Mask":@(1U<<0),@"Name":@"untrusted kernel extensions"},@{@"Mask":@(1U<<1),@"Name":@"unrestricted filesystem"}])
        if(csrCheck([permission[@"Mask"] unsignedIntValue])!=0)[missing addObject:permission[@"Name"]];
    Check(missing.count==0,[NSString stringWithFormat:@"Guest SIP policy does not allow %@. The general 'SIP disabled' status is not sufficient; use the required guest EFI policy.",[missing componentsJoinedByString:@", "]]);
    NSArray *arguments=[KernelValue("kern.bootargs") componentsSeparatedByCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    BOOL legacyAMFI=[arguments containsObject:@"amfi_get_out_of_my_way=0x1"]||[arguments containsObject:@"amfi_get_out_of_my_way=1"];
    BOOL signatureOnly=[arguments containsObject:@"amfi_allow_any_signature=1"]||[arguments containsObject:@"amfi_allow_any_signature=0x1"];
    if(requireLibraryValidation&&signatureOnly&&!legacyAMFI) {
        // Measured Mojave policy: preserve third-party identity/TCC while amfid
        // admits donor libraries. Do not mistake the narrower boot arg alone
        // for a graphics-compatible policy, or require Apple-internal SIP.
        NSDictionary *policy=[NSDictionary dictionaryWithContentsOfFile:@"/Library/Preferences/com.apple.security.libraryvalidation.plist"];
        id disabled=policy[@"DisableLibraryValidation"];
        Check(disabled&&CFGetTypeID((__bridge CFTypeRef)disabled)==CFBooleanGetTypeID()&&[disabled boolValue],
            @"Guest Library Validation compatibility policy is missing; finish guest setup before using signature-only AMFI");
    }
    Check(legacyAMFI||signatureOnly,@"Required guest AMFI boot policy is missing; restart using the app-provided EFI configuration");
    NSFileManager *fm=NSFileManager.defaultManager;
    Check([fm fileExistsAtPath:@"/private/var/db/.AppleSetupDone"]&&[fm fileExistsAtPath:@"/System/Library/CoreServices/Finder.app"],@"Complete macOS setup before installing graphics drivers");
    for(NSString *path in @[@"/macOS Install Data",@"/OS X Install Data",@"/System/Installation/CDIS/macOS Utilities.app",@"/System/Installation/CDIS/Recovery Springboard.app"])
        Check(![fm fileExistsAtPath:path],@"Installation cannot run from Recovery or during a macOS update");
    NSString *volume=nil;[[NSURL fileURLWithPath:@"/"] getResourceValue:&volume forKey:NSURLVolumeUUIDStringKey error:NULL];
    Check(volume.length>0,@"Cannot identify the guest system volume");
    return @{@"Eligible":@YES,@"ProductVersion":release,@"ProductBuildVersion":build,@"VolumeUUID":volume,@"BootSessionUUID":KernelValue("kern.bootsessionuuid"),@"UnapprovedKextsAllowed":@(csrCheck(1U<<9)==0),@"Ready":@NO};
}
static NSDictionary *LiveAdmission(void) {return LiveAdmissionWithPolicy(YES);}
#include "guest-boot-cache.h"
#include "guest-install-permissions.h"
#include "guest-install-volume.h"
#include "guest-install-trust.h"
#include "guest-shader-cache.h"
#include "guest-progress-state.h"
#include "guest-boot-policy.h"
static NSArray<NSString *> *InstallTargets(NSString *guest) {
    if([guest isEqual:@"bigsur"])return VIMBigSurInstallTargets();
    return [guest isEqual:@"mojave"]?VIMMojaveInstallTargets():VIMCatalinaInstallTargets();
}
static BOOL TransactionTargetsMatchPolicy(NSArray *actual,NSString *guest) {
    NSArray *current=InstallTargets(guest);
    if([actual isEqual:current])return YES;
    // Continue to inspect/restore already saved Mojave graphics transactions.
    // This exact prior policy predates the additive Aperture preference. New
    // plans always include it; arbitrary target subsets remain forbidden.
    if(![guest isEqual:@"mojave"])return NO;
    NSMutableArray *previous=[current mutableCopy];
    [previous removeObject:@"Library/Preferences/com.apple.Aperture.plist"];
    [previous removeObject:@"Library/Preferences/com.apple.iPhoto.plist"];
    return [actual isEqual:previous];
}
static NSDictionary *SnapshotLibraryOnlyInstallation(NSURL *root,NSURL *directory,NSString *build) {
    NSError *error=nil;
    Check([NSFileManager.defaultManager createDirectoryAtURL:directory withIntermediateDirectories:NO
        attributes:@{NSFilePosixPermissions:@0700} error:&error],error.localizedDescription);
    NSDictionary *snapshot=@{@"FormatVersion":@1,@"Policy":@"library-only",@"Root":root.path,
        @"ProductBuildVersion":build,@"Caches":@[]};
    NSURL *file=[directory URLByAppendingPathComponent:@"Snapshot.plist"];
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:snapshot format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
    Check(data&&[data writeToURL:file options:NSDataWritingAtomic error:&error],error.localizedDescription);
    int fd=open(file.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Check(fd>=0,@"Cannot open library-only installation policy");
    int result=fsync(fd);if(!result)result=fcntl(fd,F_FULLFSYNC);close(fd);
    Check(result==0,@"Cannot persist library-only installation policy");return snapshot;
}
static NSString *InstallProgressDirectory;
static void InstallProgress(NSString *phase) {
    if(!InstallProgressDirectory)return;
    NSDictionary *stages=@{@"publishing":@[@.55,@"Installing graphics and guest support…"],
        @"rebuilding-cache":@[@.70,@"Updating macOS boot caches. This may take several minutes…"],
        @"awaiting-reboot":@[@.95,@"Finishing installation…"],
        @"rolling-back":@[@(-1),@"Restoring the original system files. Keep this Mac running…"],
        @"rolled-back":@[@0,@"Setup couldn’t finish. The original system files were restored."]};
    NSArray *stage=stages[phase];if(!stage)return;
    @try {VIMProgressUpdate(InstallProgressDirectory,[phase isEqual:@"rolled-back"]?@"failed":@"working",[stage[0] doubleValue],stage[1]);}
    @catch(NSException *error){fprintf(stderr,"Setup progress unavailable: %s\n",error.reason.UTF8String);}
}
static void VerifySignature(NSString *path,BOOL deep) {
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/usr/bin/codesign"];
    task.arguments=deep?@[@"--verify",@"--deep",@"--strict",path]:@[@"--verify",@"--strict",path];
    task.standardOutput=NSFileHandle.fileHandleWithStandardError;
    NSError *error=nil;Check([task launchAndReturnError:&error],error.localizedDescription);
    [task waitUntilExit];Check(task.terminationReason==NSTaskTerminationReasonExit&&task.terminationStatus==0,[@"Invalid payload signature: " stringByAppendingString:path]);
}
static void VerifyTargets(NSURL *payload,NSArray<NSString *> *targets) {
    for(NSString *relative in targets) {
        NSString *path=[payload URLByAppendingPathComponent:relative].path;
        NSString *extension=path.pathExtension,*name=path.lastPathComponent;
        if([@[@"kext",@"bundle",@"framework",@"xpc"] containsObject:extension])VerifySignature(path,YES);
        else if([extension isEqual:@"dylib"]||[@[@"CoreDisplay",@"SkyLight",@"IOAccelerator",@"OpenGL"] containsObject:name])VerifySignature(path,NO);
        else if([name isEqual:@"3903"])VerifySignature([path stringByAppendingPathComponent:@"MTLCompiler"],NO);
        else if([name isEqual:@"31001"]) {
            NSString *libraries=[path stringByAppendingPathComponent:@"Libraries"];
            NSArray *names=[NSFileManager.defaultManager contentsOfDirectoryAtPath:libraries error:NULL];
            Check(names!=nil,@"Missing GPU compiler libraries");NSUInteger count=0;
            for(NSString *library in names)if([library.pathExtension isEqual:@"dylib"]){VerifySignature([libraries stringByAppendingPathComponent:library],NO);count++;}
            Check(count>0,@"GPU compiler payload has no signed libraries");
        }
    }
}
static void SaveInstallResult(NSURL *journal,NSDictionary *state,NSString *phase,NSDictionary *details) {
    NSString *volume=nil;[[NSURL fileURLWithPath:state[@"Root"]] getResourceValue:&volume forKey:NSURLVolumeUUIDStringKey error:NULL];
    Check(volume.length>0,@"Cannot identify installation result volume");
    NSMutableDictionary *receipt=[@{@"FormatVersion":@1,@"TransactionJournal":journal.path,
        @"TransactionID":state[@"ID"],@"Root":state[@"Root"],
        @"VolumeUUID":volume,
        @"ProductBuildVersion":state[@"Metadata"][@"ProductBuildVersion"],
        @"Phase":phase,@"UpdatedAt":NSDate.date,@"Ready":@NO,
        @"BootSessionUUID":KernelValue("kern.bootsessionuuid")} mutableCopy];
    [receipt addEntriesFromDictionary:details];
    NSError *error=nil;
    NSURL *destination=[NSURL fileURLWithPath:[journal.path stringByAppendingString:@".result.plist"]];
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:receipt format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
    Check(data&&[data writeToURL:destination options:NSDataWritingAtomic error:&error],error.localizedDescription?:@"Cannot record installation result");
    int fd=open(destination.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Check(fd>=0,@"Cannot open installation result");
    int result=fsync(fd);if(!result)result=fcntl(fd,F_FULLFSYNC);close(fd);
    Check(result==0,@"Cannot persist installation result");
    fd=open(destination.URLByDeletingLastPathComponent.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Check(fd>=0,@"Cannot open installation result directory");result=fsync(fd);close(fd);
    Check(result==0,@"Cannot persist installation result directory");
    InstallProgress(phase);
}
static NSDictionary *ApplyTransaction(NSURL *journal,BOOL rollback) {
#ifndef VIM_INSTALL_PLAN_TESTING
    Check(geteuid()==0,@"Guest installation requires root privileges");
    NSString *trusted=TrustedStagingPath(journal.path,NO,0);
#endif
    NSError *error=nil;VIMFileTransaction *transaction=[VIMFileTransaction loadURL:journal error:&error];
    Check(transaction!=nil,error.localizedDescription);
    NSDictionary *state=transaction.state,*metadata=state[@"Metadata"],*preparation=metadata[@"Preparation"];
    NSURL *root=[NSURL fileURLWithPath:state[@"Root"]];
    NSString *guest=preparation[@"Guest"],*build=metadata[@"ProductBuildVersion"];
    Check([@[@"mojave",@"catalina",@"bigsur"] containsObject:guest],@"Invalid transaction guest");
    BOOL mojave=[guest isEqual:@"mojave"];
    BOOL bigsur=[guest isEqual:@"bigsur"];
    NSArray *targets=[state[@"Entries"] valueForKey:@"RelativePath"];
    Check(TransactionTargetsMatchPolicy(targets,guest),@"Transaction targets differ from compiled policy");
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[root URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
    Check([version[@"ProductBuildVersion"] isEqual:build]&&[version[@"ProductVersion"] isEqual:preparation[@"ProductVersion"]],@"Cannot apply or restore a transaction from another OS build");
    NSURL *cacheDirectory=[NSURL fileURLWithPath:metadata[@"BootCacheDirectory"]];
#ifndef VIM_INSTALL_PLAN_TESTING
    Check([TrustedStagingPath(state[@"Payload"],NO,0) isEqual:trusted]&&
          [TrustedStagingPath(cacheDirectory.path,NO,0) isEqual:trusted],@"Installer inputs belong to different staging attempts");
    TrustedStagingPath([cacheDirectory URLByAppendingPathComponent:@"Snapshot.plist"].path,NO,0);
#endif
    Check([cacheDirectory.URLByDeletingLastPathComponent.path isEqual:journal.URLByDeletingLastPathComponent.path]&&[cacheDirectory.lastPathComponent hasPrefix:@"BootCaches-"],@"Cache backup is outside the installation journal directory");
    Check([[NSDictionary dictionaryWithContentsOfURL:[cacheDirectory URLByAppendingPathComponent:@"Snapshot.plist"]] isEqual:metadata[@"BootCaches"]],@"Cache backup record changed since planning");
#ifdef VIM_INSTALL_PLAN_TESTING
    Check(![root.path isEqual:@"/"],@"Fixture cannot apply to the live system");
#else
    NSDictionary *admission=LiveAdmission();
    Check([root.path isEqual:@"/"]&&[admission[@"ProductBuildVersion"] isEqual:build],@"Transaction does not target this running guest");
    // Recheck signatures at publication, not only during earlier planning.
    if(!rollback)VerifyTargets([NSURL fileURLWithPath:state[@"Payload"]],targets);
#endif
    int lock=OpenInstallLock(root);
    @try {
        Check(flock(lock,LOCK_EX|LOCK_NB)==0,@"Another installation is active");
        // A matching receipt is not proof that the retained bytes still exist.
        // Refuse before publication (or rollback) if recovery is unavailable.
        if(bigsur) {
            Check([metadata[@"BootCaches"] isEqual:@{@"FormatVersion":@1,@"Policy":@"library-only",@"Root":root.path,
                @"ProductBuildVersion":build,@"Caches":@[]}],@"Invalid Big Sur library-only installation policy");
        } else {
            Check([VerifyBootCacheSnapshot(root,cacheDirectory,mojave,build) isEqual:metadata[@"BootCaches"]],@"Boot-cache recovery snapshot changed");
            MakeGuestSystemWritable(root,mojave);
        }
        if(rollback) {
            SaveInstallResult(journal,state,@"rolling-back",@{});
            Check([transaction rollback:&error],error.localizedDescription);
            if(!bigsur)RestoreBootCaches(root,cacheDirectory,mojave,build);
            SaveInstallResult(journal,state,@"rolled-back",@{@"Installed":@NO});
            return @{@"RolledBack":@YES,@"Installed":@NO,@"Ready":@NO};
        }
        @try {
            SaveInstallResult(journal,state,@"publishing",@{@"Installed":@NO});
            Check([transaction publish:&error],error.localizedDescription);
            NSURL *log=[journal.URLByDeletingLastPathComponent URLByAppendingPathComponent:[NSString stringWithFormat:@"CacheBuild-%@.log",NSUUID.UUID.UUIDString]];
            if(!bigsur)SaveInstallResult(journal,state,@"rebuilding-cache",@{@"FilesPublished":@YES,@"CacheLog":log.path});
            NSDictionary *cache=bigsur?@{@"CacheRequired":@NO,@"Policy":@"library-only"}:RebuildBootCaches(root,mojave,log);
            NSDictionary *shaders=QuarantineGuestShaderCaches(root);
            Check([shaders[@"Errors"] count]==0,[@"Could not preserve old shader caches: " stringByAppendingString:[shaders[@"Errors"] componentsJoinedByString:@"; "]]);
            SaveInstallResult(journal,state,@"awaiting-reboot",@{@"FilesPublished":@YES,@"Cache":cache,@"ShaderCaches":shaders,@"RebootRequired":@YES,@"CacheLog":log.path});
            return @{@"FilesPublished":@YES,@"Cache":cache,@"ShaderCaches":shaders,@"RebootRequired":@YES,@"Ready":@NO};
        } @catch(NSException *failure) {
            // Do not let a result-write failure prevent restoring the system.
            @try {SaveInstallResult(journal,state,@"rolling-back",@{@"Failure":failure.reason});} @catch(NSException *ignored) {}
            transaction=[VIMFileTransaction loadURL:journal error:&error];
            Check(transaction&&[transaction rollback:&error],[NSString stringWithFormat:@"%@; file rollback also failed: %@",failure.reason,error.localizedDescription]);
            if(!bigsur)RestoreBootCaches(root,cacheDirectory,mojave,build);
            SaveInstallResult(journal,state,@"rolled-back",@{@"Installed":@NO,@"Failure":failure.reason});
            @throw [NSException exceptionWithName:@"InstallRolledBack" reason:[NSString stringWithFormat:@"%@; original files and caches restored",failure.reason] userInfo:nil];
        }
    } @finally {close(lock);}
}
#include "guest-install-verification.h"
int main(int argc,const char **argv){@autoreleasepool {
    if(argc==3&&(!strcmp(argv[1],"--configure-boot-policy")||!strcmp(argv[1],"--boot-policy-status"))) {
        @try {
            NSDictionary *result=BootPolicyOperation(@(argv[2]),!strcmp(argv[1],"--configure-boot-policy"));
            NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
            fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);return 0;
        }@catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
    }
    if(argc==3&&!strcmp(argv[1],"--status")) {
        @try {
            NSURL *journal=[NSURL fileURLWithPath:@(argv[2])];
            TrustedStagingPath(journal.path,NO,0);
            NSError *error=nil;VIMFileTransaction *transaction=[VIMFileTransaction loadURL:journal error:&error];
            Check(transaction!=nil,error.localizedDescription);
            NSMutableDictionary *report=[@{@"TransactionID":transaction.state[@"ID"],@"Phase":transaction.state[@"Phase"],@"Ready":@NO} mutableCopy];
            NSString *path=[journal.path stringByAppendingString:@".result.plist"];
            if([NSFileManager.defaultManager fileExistsAtPath:path]) {
                TrustedStagingPath(path,NO,0);NSDictionary *result=[NSDictionary dictionaryWithContentsOfFile:path];
                Check([result[@"TransactionID"] isEqual:report[@"TransactionID"]],@"Installation result belongs to another transaction");
                for(NSString *key in @[@"Phase",@"ProductBuildVersion",@"VolumeUUID",@"BootSessionUUID",@"CacheLog",@"Failure",@"Cache",@"ShaderCaches",@"RebootRequired",@"FilesPublished",@"Installed"])
                    if(result[key])report[key]=result[key];
            }
            NSData *data=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:&error];
            Check(data!=nil,error.localizedDescription);fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);return 0;
        } @catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
    }
    if(argc==3&&!strcmp(argv[1],"--verify-after-reboot")) {
        @try {
            NSDictionary *result=VerifyInstalledAfterReboot([NSURL fileURLWithPath:@(argv[2])]);
            NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
            fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);return 0;
        } @catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
    }
    if(argc==2&&!strcmp(argv[1],"--check-environment")) {
        @try {NSData *data=[NSJSONSerialization dataWithJSONObject:LiveAdmission() options:NSJSONWritingPrettyPrinted error:NULL];fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);return 0;}
        @catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
    }
    if((argc==3||(argc==5&&!strcmp(argv[3],"--progress")))&&(!strcmp(argv[1],"--apply")||!strcmp(argv[1],"--rollback"))) {
        id activity=[NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated|NSActivityIdleDisplaySleepDisabled reason:@"Installing virtual Mac graphics support"];
        @try {
            if(argc==5){int directory=VIMProgressDirectory(@(argv[4]));Check(directory>=0,@"Invalid guest progress state");close(directory);InstallProgressDirectory=@(argv[4]);}
            NSDictionary *result=ApplyTransaction([NSURL fileURLWithPath:@(argv[2])],!strcmp(argv[1],"--rollback"));
            NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:NULL];
            fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);return 0;
        }@catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
        @finally {[NSProcessInfo.processInfo endActivity:activity];}
    }
    if(argc!=5||strcmp(argv[1],"--plan")){fprintf(stderr,"usage: guest-install --plan GUEST_ROOT PREPARED_PAYLOAD NEW_JOURNAL\n");return 64;}
    @try {
        NSURL *root=[NSURL fileURLWithPath:@(argv[2])].URLByResolvingSymlinksInPath;
        NSURL *payload=[NSURL fileURLWithPath:@(argv[3])].URLByResolvingSymlinksInPath;
        NSURL *journal=[NSURL fileURLWithPath:@(argv[4])];
        Check(![NSFileManager.defaultManager fileExistsAtPath:journal.path],@"Installation journal already exists; resume that transaction instead");
        Check([@(argv[2]) isAbsolutePath]&&[@(argv[3]) isAbsolutePath]&&[@(argv[4]) isAbsolutePath],@"Use absolute installation paths");
        NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[root URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
        NSDictionary *receipt=[NSDictionary dictionaryWithContentsOfURL:[payload URLByAppendingPathComponent:@".VirtualIntelMacPreparation.plist"]];
        Check([receipt[@"FormatVersion"] isEqual:@1]&&[receipt[@"Prepared"] isEqual:@YES]&&[receipt[@"Installed"] isEqual:@NO],@"Payload has no completed preparation receipt");
        Check([receipt[@"ProductVersion"] isEqual:version[@"ProductVersion"]]&&[receipt[@"ProductBuildVersion"] isEqual:version[@"ProductBuildVersion"]],@"Guest build changed; prepare a new payload from its current system files");
        NSString *recordedPayload=receipt[@"PayloadDirectory"];
        Check([recordedPayload isKindOfClass:NSString.class]&&recordedPayload.isAbsolutePath&&
              [[[NSURL fileURLWithPath:recordedPayload] URLByResolvingSymlinksInPath].path isEqual:payload.path],@"Payload moved after preparation; prepare it again at its current location");
        NSString *release=version[@"ProductVersion"],*guest=receipt[@"Guest"];
        Check([release isKindOfClass:NSString.class]&&[guest isKindOfClass:NSString.class],@"Invalid guest identity");
        BOOL mojave=[release isEqual:@"10.14"]||[release hasPrefix:@"10.14."];
        BOOL catalina=[release isEqual:@"10.15"]||[release hasPrefix:@"10.15."];
        BOOL bigsur=[release hasPrefix:@"11."];
        Check((mojave&&[guest isEqual:@"mojave"])||(catalina&&[guest isEqual:@"catalina"])||(bigsur&&[guest isEqual:@"bigsur"]),@"Unsupported backport target");
        for(NSString *marker in @[@"macOS Install Data",@"OS X Install Data"])
            Check(![NSFileManager.defaultManager fileExistsAtPath:[root URLByAppendingPathComponent:marker].path],@"Finish the macOS installation/update before preparing a driver transaction");
        NSArray *targets=InstallTargets(guest);
        BOOL live=[root.path isEqual:@"/"];
        if(live) {
            Check(geteuid()==0,@"Guest planning requires root privileges");
            Check([payload.path hasPrefix:@"/private/var/tmp/VirtualIntelMac."]||[payload.path hasPrefix:@"/var/tmp/VirtualIntelMac."]||
                  [payload.path hasPrefix:@"/private/var/db/VirtualIntelMac."]||[payload.path hasPrefix:@"/var/db/VirtualIntelMac."],@"Live payload must be in private guest staging");
            Check([TrustedStagingPath(payload.path,NO,0) isEqual:TrustedStagingPath(journal.path,YES,0)],@"Payload and journal must belong to the same private staging attempt");
            TrustedStagingPath([payload URLByAppendingPathComponent:@".VirtualIntelMacPreparation.plist"].path,NO,0);
        }
#ifdef VIM_INSTALL_PLAN_TESTING
        Check(![root.path isEqual:@"/"],@"Fixture build cannot inspect a live installation");
#else
        VerifyTargets(payload,targets);
#endif
        // Modes become part of the transaction fingerprints; ownership is
        // normalized before copying so kext authentication sees root:wheel.
        for(NSString *relative in targets)NormalizeReplacement([payload URLByAppendingPathComponent:relative].path,live);
        // Fixed targets come from the checked-in build inputs, not from a guest
        // receipt, command line, downloaded manifest, or installer archive.
        NSError *error=nil;
        NSURL *cacheDirectory=[journal.URLByDeletingLastPathComponent URLByAppendingPathComponent:[@"BootCaches-" stringByAppendingString:NSUUID.UUID.UUIDString]];
        NSDictionary *caches=bigsur?SnapshotLibraryOnlyInstallation(root,cacheDirectory,version[@"ProductBuildVersion"]):SnapshotBootCaches(root,cacheDirectory,mojave,version[@"ProductBuildVersion"]);
        VIMFileTransaction *transaction=[VIMFileTransaction createAtURL:journal root:root payload:payload targets:targets metadata:@{@"Preparation":receipt,@"ProductBuildVersion":version[@"ProductBuildVersion"],@"BootCaches":caches,@"BootCacheDirectory":cacheDirectory.path} error:&error];
        Check(transaction!=nil,error.localizedDescription);
        NSDictionary *result=@{@"Planned":@YES,@"Installed":@NO,@"Ready":@NO,@"TargetCount":@(targets.count),@"Journal":journal.path,@"Guest":guest,@"TransactionID":transaction.state[@"ID"]};
        NSData *data=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:&error];
        Check(data!=nil,error.localizedDescription);fwrite(data.bytes,1,data.length,stdout);fputc('\n',stdout);
        return 0;
    }@catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
}}
