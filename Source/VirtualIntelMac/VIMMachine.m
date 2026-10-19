#import "VIMMachine.h"
#import "VIMNetworkDefaults.h"
#import "VIMLibraryLocation.h"
#import "VIMFirmwareBoot.h"
#import "VIMGuestTransactionEvidence.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>

static BOOL EFIDisk(NSURL *URL) {
    int fd=open(URL.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW);
    if(fd<0)return NO;
    char signature[8];ssize_t count=pread(fd,signature,sizeof(signature),512);close(fd);
    return count==sizeof(signature)&&memcmp(signature,"EFI PART",8)==0;
}

static BOOL Fail(NSError **error, NSString *message) {
    if (error) *error = [NSError errorWithDomain:@"VirtualIntelMac.Machine" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
static BOOL Save(NSDictionary *value, NSURL *URL, NSError **error) {
    NSData *data = [NSPropertyListSerialization dataWithPropertyList:value
        format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    return data && [data writeToURL:URL options:NSDataWritingAtomic error:error];
}
static BOOL Supported(NSString *guest) { return [@[@"mojave", @"catalina", @"bigsur"] containsObject:guest]; }
static BOOL NameValid(NSString *name) {
    return [name isKindOfClass:NSString.class] && name.length && name.length <= 128 && [name stringByTrimmingCharactersInSet:
        NSCharacterSet.whitespaceAndNewlineCharacterSet].length;
}
static NSURL *NewPackageURL(NSURL *library, NSString *name) {
    NSArray *parts=[name componentsSeparatedByCharactersInSet:[NSCharacterSet characterSetWithCharactersInString:@"/:\n\r"]];
    NSString *base=[[parts componentsJoinedByString:@"-"] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if(!base.length||[base hasPrefix:@"."])base=[@"Mac " stringByAppendingString:base];
    for(NSUInteger i=1;;i++) {
        NSString *candidate=i==1?base:[NSString stringWithFormat:@"%@ %lu",base,(unsigned long)i];
        NSURL *url=[library URLByAppendingPathComponent:[candidate stringByAppendingString:@".vintelmac"]];
        if(![NSFileManager.defaultManager fileExistsAtPath:url.path])return url;
    }
}
static BOOL SettingsValid(NSDictionary *c) {
    if(c[@"NetworkMode"]&&![@[@"shared",@"host",@"bridged"] containsObject:c[@"NetworkMode"]])return NO;
    if(c[@"BridgeInterface"]&&(![c[@"BridgeInterface"] isKindOfClass:NSString.class]||[c[@"BridgeInterface"] length]>64))return NO;
    for(NSString *key in @[@"CPUCount",@"MemoryMiB",@"SSHPort",@"FileSharingPort"])
        if(c[key] && (![c[key] isKindOfClass:NSNumber.class] || [c[key] doubleValue] != [c[key] integerValue]))return NO;
    NSInteger cpu=[c[@"CPUCount"] integerValue], memory=[c[@"MemoryMiB"] integerValue];
    if(cpu<1||cpu>64||memory<1024||memory>1048576)return NO;
    for(NSString *key in @[@"SSHEnabled",@"FileSharingEnabled",@"AudioEnabled",@"RetinaEnabled",@"NetworkConnected",@"ClipboardEnabled"])
        if(c[key] && (![c[key] isKindOfClass:NSNumber.class] || ([c[key] doubleValue]!=0 && [c[key] doubleValue]!=1)))return NO;
    for(NSString *key in @[@"SSHPort",@"FileSharingPort"])
        if(c[key] && ([c[key] integerValue]<1024||[c[key] integerValue]>65535))return NO;
    if([c[@"SSHEnabled"] boolValue] && !c[@"SSHPort"])return NO;
    if([c[@"FileSharingEnabled"] boolValue] && !c[@"FileSharingPort"])return NO;
    if([c[@"SSHEnabled"] boolValue] && [c[@"FileSharingEnabled"] boolValue] &&
        [c[@"SSHPort"] isEqual:c[@"FileSharingPort"]])return NO;
    if(c[@"Description"]&&(![c[@"Description"] isKindOfClass:NSString.class]||[c[@"Description"] length]>4096))return NO;
    if(c[@"OpticalMedia"]&&(![c[@"OpticalMedia"] isKindOfClass:NSString.class]||([c[@"OpticalMedia"] length]&&![c[@"OpticalMedia"] isAbsolutePath])))return NO;
    if(c[@"MACAddress"]&&(![c[@"MACAddress"] isKindOfClass:NSString.class]||([c[@"MACAddress"] length]&&!VIMNetworkMACAddressIsValid(c[@"MACAddress"]))))return NO;
    if(c[@"DisplayMode"]&&![@[@"retina",@"scaled",@"space"] containsObject:c[@"DisplayMode"]])return NO;
    for(NSString *key in @[@"AudioOutputDevice",@"AudioInputDevice"])
        if(c[key]&&(![c[key] isKindOfClass:NSString.class]||[c[key] length]>1024))return NO;
    return YES;
}
static NSSet *BaseEditableSettingKeys(void) {
    static NSSet *keys;static dispatch_once_t once;dispatch_once(&once,^{keys=[NSSet setWithArray:@[@"CPUCount",@"MemoryMiB",@"SSHEnabled",@"SSHPort",@"FileSharingEnabled",@"FileSharingPort",@"AudioEnabled",@"Description",@"RetinaEnabled",@"OpticalMedia",@"NetworkConnected",@"MACAddress",@"DisplayMode",@"AudioOutputDevice",@"AudioInputDevice",@"ClipboardEnabled"]];});return keys;
}
static NSSet *EditableSettingKeys(void) {
    NSMutableSet *keys=[BaseEditableSettingKeys() mutableCopy];[keys addObjectsFromArray:@[@"NetworkMode",@"BridgeInterface"]];return keys;
}
static int ConfigurationLock(NSURL *package,NSError **error) {
    NSURL *lock=[package URLByAppendingPathComponent:@"State/Configuration.lock"];
    int fd=open(lock.fileSystemRepresentation,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);struct stat info;
    if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB)) {
        if(fd>=0)close(fd);Fail(error,@"This Mac’s settings are being saved. Please try again.");return -1;
    }return fd;
}
static void RelocatedPackage(NSURL *old,NSURL *new,NSString *identifier) {
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    for(NSString *key in defaults.dictionaryRepresentation.allKeys) {
        if([key hasPrefix:@"VIMLibraryOrder-"]||[key isEqual:@"VIMRegisteredPackages"]) {
            NSArray *paths=[defaults arrayForKey:key];if(![paths containsObject:old.path])continue;
            NSMutableArray *updated=[paths mutableCopy];for(NSUInteger i=0;i<updated.count;i++)if([updated[i] isEqual:old.path])updated[i]=new.path;
            [defaults setObject:updated forKey:key];
        } else if([key hasPrefix:@"VIMLibraryKnown-"]) {
            NSMutableDictionary *known=[[defaults dictionaryForKey:key] mutableCopy];if(!known[old.path])continue;
            known[new.path]=known[old.path];[known removeObjectForKey:old.path];[defaults setObject:known forKey:key];
        }
    }
    NSMutableDictionary *job=[[defaults dictionaryForKey:@"VIMCreationJob"] mutableCopy];
    if([job[@"Machine"] isEqual:old.path]){job[@"Machine"]=new.path;[defaults setObject:job forKey:@"VIMCreationJob"];}
    NSString *oldKey=[@"VIMGuestUsername-" stringByAppendingString:old.path],*newKey=[@"VIMGuestUsername-" stringByAppendingString:new.path];
    if([defaults objectForKey:oldKey]){[defaults setObject:[defaults objectForKey:oldKey] forKey:newKey];[defaults removeObjectForKey:oldKey];}
    dispatch_async(dispatch_get_main_queue(),^{[NSNotificationCenter.defaultCenter postNotificationName:@"VIMMachineRelocated" object:nil userInfo:@{@"OldURL":old,@"URL":new,@"Identifier":identifier}];});
}
static BOOL Run(NSURL *executable, NSArray *arguments, NSURL *log, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    if (![fm createFileAtPath:log.path contents:nil attributes:nil]) return Fail(error, @"Cannot create import log.");
    NSFileHandle *output = [NSFileHandle fileHandleForWritingToURL:log error:error];
    if (!output) return NO;
    NSTask *task = [NSTask new]; task.executableURL = executable; task.arguments = arguments;
    task.standardOutput = output; task.standardError = output;
    BOOL launched = [task launchAndReturnError:error];
    if (launched) [task waitUntilExit];
    [output closeFile];
    if (!launched) return NO;
    return task.terminationStatus == 0 || Fail(error, [NSString stringWithFormat:
        @"The disk tool could not complete the operation. See %@. Source disks were not modified.", log.path]);
}
// Only called inside a freshly created, unpublished package. Never format an
// existing guest disk. Sparse staging keeps large logical disks cheap on exFAT.
static BOOL CreateFormattedDisk(NSURL *stage, NSURL *qemuImage, NSUInteger diskGiB, NSError **error) {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *seed=[stage URLByAppendingPathComponent:@"Disks/Formatting.sparsebundle"];
    NSURL *disk=[stage URLByAppendingPathComponent:@"Disks/System.qcow2"];
    NSURL *hdiutil=[NSURL fileURLWithPath:@"/usr/bin/hdiutil"];
    NSURL *(^log)(NSString *)=^NSURL *(NSString *name){return [stage URLByAppendingPathComponent:[@"Logs/" stringByAppendingString:name]];};
    if(!Run(hdiutil,@[@"create",@"-size",[NSString stringWithFormat:@"%lug",diskGiB],
        @"-type",@"SPARSEBUNDLE",@"-layout",@"GPTSPUD",@"-fs",@"Journaled HFS+",
        @"-volname",@"Macintosh HD",seed.path],log(@"FormatDisk.log"),error))return NO;
    NSURL *attachLog=log(@"AttachFormattingDisk.log");
    if(!Run(hdiutil,@[@"attach",@"-nomount",@"-readonly",@"-plist",seed.path],attachLog,error))return NO;
    // hdiutil on newer hosts appends a deprecation warning to the merged log.
    NSString *output=[NSString stringWithContentsOfURL:attachLog encoding:NSUTF8StringEncoding error:error];
    NSRange start=[output rangeOfString:@"<?xml"],end=[output rangeOfString:@"</plist>"];
    NSDictionary *attached=nil;
    if(output && start.location!=NSNotFound && end.location!=NSNotFound && end.location>=start.location) {
        NSData *plist=[[output substringWithRange:NSMakeRange(start.location,NSMaxRange(end)-start.location)] dataUsingEncoding:NSUTF8StringEncoding];
        attached=[NSPropertyListSerialization propertyListWithData:plist options:0 format:nil error:error];
    }
    NSString *device=nil;
    for(NSDictionary *entity in attached[@"system-entities"])
        if([entity[@"content-hint"] isEqual:@"GUID_partition_scheme"])device=entity[@"dev-entry"];
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^/dev/disk[0-9]+$" options:0 error:nil];
    if(![device isKindOfClass:NSString.class] || ![pattern numberOfMatchesInString:device options:0 range:NSMakeRange(0,device.length)]) {
        // Detach by our exact image path if the plist was not understood.
        Run(hdiutil,@[@"detach",seed.path],log(@"DetachFormattingDisk.log"),nil);
        return Fail(error,@"Cannot identify the newly formatted virtual disk. See AttachFormattingDisk.log.");
    }
    NSString *raw=[device stringByReplacingOccurrencesOfString:@"/dev/disk" withString:@"/dev/rdisk"];
    BOOL converted=Run(qemuImage,@[@"convert",@"-f",@"raw",@"-O",@"qcow2",raw,disk.path],log(@"CreateDisk.log"),error);
    NSError *detachError=nil;
    BOOL detached=Run(hdiutil,@[@"detach",device],log(@"DetachFormattingDisk.log"),&detachError);
    if(!converted)return NO;
    if(!detached){if(error)*error=detachError;return NO;}
    // Discard only our generated seed after conversion and successful detach.
    return [fm removeItemAtURL:seed error:error];
}
@interface VIMMachine ()
@property NSNumber *cachedDiskCapacity;
- (BOOL)authorizeUpdateRepair:(NSDictionary *)report claim:(BOOL)claim error:(NSError **)error;
@end
/// Media the app prepared lives in its own Downloads/Preparation/<UUID>
/// attempt and is never reused for another Mac. Once the owner is installed,
/// delete that attempt unless another Mac in the library still references it.
static BOOL RemovePreparedMedia(NSString *path,NSURL *owner) {
    if([NSUserDefaults.standardUserDefaults boolForKey:@"VIMKeepInstallers"])return NO;
    if(![path isKindOfClass:NSString.class]||!path.isAbsolutePath)return NO;
    NSURL *media=[NSURL fileURLWithPath:path].URLByResolvingSymlinksInPath;
    NSURL *attempt=media.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent;
    NSURL *container=attempt.URLByDeletingLastPathComponent;
    BOOL legacy=[container.lastPathComponent isEqual:@"Preparation"]&&[container.URLByDeletingLastPathComponent.lastPathComponent isEqual:@"Downloads"];
    BOOL organized=[container.lastPathComponent isEqual:@"Installers"]&&[owner.URLByDeletingLastPathComponent.lastPathComponent isEqual:@"Machines"]&&
        [container.URLByDeletingLastPathComponent.path isEqual:owner.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent.path];
    if(![media.URLByDeletingLastPathComponent.lastPathComponent isEqual:@"Media"]||
       (!legacy&&!organized)||![[NSUUID alloc] initWithUUIDString:attempt.lastPathComponent])return NO;
    NSFileManager *fm=NSFileManager.defaultManager;
    NSString *prefix=[attempt.path stringByAppendingString:@"/"];
    NSDictionary *creation=[NSUserDefaults.standardUserDefaults dictionaryForKey:@"VIMCreationJob"];
    NSString *creatingMedia=creation[@"Media"];
    if([creatingMedia isKindOfClass:NSString.class]&&[creatingMedia.stringByResolvingSymlinksInPath hasPrefix:prefix])return NO;
    for(NSURL *sibling in [fm contentsOfDirectoryAtURL:owner.URLByDeletingLastPathComponent includingPropertiesForKeys:nil options:0 error:nil]?:@[]) {
        if(![sibling.pathExtension isEqual:@"vintelmac"]||[sibling.URLByResolvingSymlinksInPath.path isEqual:owner.URLByResolvingSymlinksInPath.path])continue;
        NSDictionary *config=[NSDictionary dictionaryWithContentsOfURL:[sibling URLByAppendingPathComponent:@"Machine.plist"]];
        if(!config)return NO;
        NSString *other=config[@"InstallerMedia"];
        if([other isKindOfClass:NSString.class]&&[other.stringByResolvingSymlinksInPath hasPrefix:prefix])return NO;
    }
    return [fm removeItemAtURL:attempt error:nil];
}
@implementation VIMMachine
- (NSString *)name {
    NSString *stem=self.URL.lastPathComponent.stringByDeletingPathExtension;
    // Legacy UUID packages retain their display name until safely migrated.
    return [[NSUUID alloc] initWithUUIDString:stem]&&self.configuration[@"Name"]?self.configuration[@"Name"]:stem;
}
- (NSString *)guest { return self.configuration[@"Guest"]; }
- (uint64_t)diskCapacityBytes {
    if(self.cachedDiskCapacity)return self.cachedDiskCapacity.unsignedLongLongValue;
    uint64_t capacity=0;
    NSNumber *recorded=self.configuration[@"DiskGiB"];
    if([recorded isKindOfClass:NSNumber.class]&&recorded.doubleValue>0&&recorded.doubleValue<=1048576)
        capacity=(uint64_t)(recorded.doubleValue*(1ULL<<30));
    NSString *path=self.configuration[@"Disk"];
    if(!capacity&&[path isKindOfClass:NSString.class]&&path.length) {
        int fd=open([self.URL URLByAppendingPathComponent:path].fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
        struct stat info;unsigned char header[32];
        if(fd>=0) {
            // QCOW2 v2/v3: big-endian virtual size at bytes 24–31. Never read
            // disk contents, traverse snapshots, or launch a tool to paint a row.
            if(!fstat(fd,&info)&&S_ISREG(info.st_mode)&&pread(fd,header,sizeof(header),0)==sizeof(header)&&
               !memcmp(header,"QFI\xfb",4)&&header[4]==0&&header[5]==0&&header[6]==0&&(header[7]==2||header[7]==3)) {
                uint64_t size;memcpy(&size,header+24,sizeof(size));capacity=CFSwapInt64BigToHost(size);
            }
            close(fd);
        }
    }
    self.cachedDiskCapacity=@(capacity);return capacity;
}
- (NSDictionary *)editableConfiguration {
    NSMutableDictionary *result=[self.configuration mutableCopy];
    NSDictionary *pending=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/PendingConfiguration.plist"]];
    if([pending[@"Settings"] isKindOfClass:NSDictionary.class])for(NSString *key in pending[@"Settings"])if([EditableSettingKeys() containsObject:key])result[key]=pending[@"Settings"][key];
    return SettingsValid(result)?result:self.configuration;
}
- (NSString *)pendingName {id value=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/PendingConfiguration.plist"]][@"RequestedBundleName"];return NameValid(value)?value:nil;}
+ (instancetype)loadURL:(NSURL *)URL error:(NSError **)error {
    if (!URL.isFileURL || ![URL.pathExtension isEqual:@"vintelmac"]) {
        Fail(error, @"Choose a .vintelmac package."); return nil;
    }
    URL = URL.URLByResolvingSymlinksInPath;
    NSError *readError=nil;
    NSData *data = [NSData dataWithContentsOfURL:[URL URLByAppendingPathComponent:@"Machine.plist"] options:0 error:&readError];
    if (!data) {
        if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.Machine" code:1 userInfo:@{NSLocalizedDescriptionKey:@"This Mac isn’t available. Reconnect its drive or locate its current folder.",NSUnderlyingErrorKey:readError?:[NSError errorWithDomain:NSCocoaErrorDomain code:NSFileNoSuchFileError userInfo:nil]}];
        return nil;
    }
    id c = [NSPropertyListSerialization propertyListWithData:data options:0 format:nil error:error];
    if (![c isKindOfClass:NSDictionary.class] || ![c[@"FormatVersion"] isEqual:@1] ||
        !NameValid(URL.lastPathComponent.stringByDeletingPathExtension) ||
        (c[@"Name"]&&!NameValid(c[@"Name"])) ||
        !Supported(c[@"Guest"]) || ![c[@"CPUCount"] isKindOfClass:NSNumber.class] ||
        [c[@"CPUCount"] integerValue] < 1 || [c[@"CPUCount"] integerValue] > 64 ||
        ![c[@"MemoryMiB"] isKindOfClass:NSNumber.class] || [c[@"MemoryMiB"] integerValue] < 1024 ||
        ![c[@"Identifier"] isKindOfClass:NSString.class] ||
        ![[NSUUID alloc] initWithUUIDString:c[@"Identifier"]]) {
        Fail(error, @"This Mac’s configuration is invalid or not supported by this version."); return nil;
    }
    NSMutableArray *fileKeys=[NSMutableArray arrayWithArray:@[@"Disk",@"Firmware",@"NVRAM",@"BootMedia"]];
    if([c[@"FirmwareSource"] isEqual:@"application"])[fileKeys removeObjectsInArray:@[@"Firmware",@"BootMedia"]];
    if(c[@"BootMediaKind"]&&![@[@"legacy-opencore-iso",@"efi-disk"] containsObject:c[@"BootMediaKind"]]) {
        Fail(error,@"This Mac uses an unsupported boot media format.");return nil;
    }
    if(c[@"InstallerMedia"]) {
        if(c[@"InstallerMediaKind"]&&![@[@"optical",@"disk"] containsObject:c[@"InstallerMediaKind"]]) {
            Fail(error,@"Unsupported installer media device type.");return nil;
        }
        if(![@[@"installing",@"patching"] containsObject:c[@"InstallState"]] ||
           (![c[@"InstallerMedia"] isEqual:@"Installation/Installer.iso"]&&
            !([c[@"InstallerMediaShared"] isEqual:@YES]&&[c[@"InstallerMedia"] isKindOfClass:NSString.class]&&[c[@"InstallerMedia"] isAbsolutePath]))) {
            Fail(error,@"Installer media is attached in an invalid installation state.");return nil;
        }
        [fileKeys addObject:@"InstallerMedia"];
    }
    for (NSString *key in fileKeys) {
        id path = c[key];
        if([key isEqual:@"InstallerMedia"]&&[c[@"InstallerMediaShared"] isEqual:@YES]) {
            if(![path isKindOfClass:NSString.class]||![path isAbsolutePath]) {
                Fail(error,@"Shared installer path must be absolute.");return nil;
            }
            NSNumber *regular=nil;
            if(![[NSURL fileURLWithPath:path] getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error]||!regular.boolValue) {
                Fail(error,@"The shared installer is missing. Restore it to its original location.");return nil;
            }
            continue;
        }
        if (![path isKindOfClass:NSString.class] || ![path length] || [path isAbsolutePath]) {
            Fail(error, @"The package contains an invalid file path."); return nil;
        }
        NSURL *file = [[URL URLByAppendingPathComponent:path] URLByResolvingSymlinksInPath];
        NSNumber *regular = nil;
        if (![file.path hasPrefix:[URL.path stringByAppendingString:@"/"]] ||
            ![file getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error] || !regular.boolValue) {
            Fail(error, [NSString stringWithFormat:@"The %@ file is missing or outside this package.", key]); return nil;
        }
    }
    if(!SettingsValid(c)) { Fail(error,@"This Mac has invalid memory, processor or network settings.");return nil; }
    NSDictionary *state=[NSFileManager.defaultManager attributesOfItemAtPath:[URL URLByAppendingPathComponent:@"State"].path error:error];
    if(![state[NSFileType] isEqual:NSFileTypeDirectory]){Fail(error,@"This Mac’s State folder is missing or is not a regular folder.");return nil;}
    VIMMachine *machine = [self new]; machine->_URL = URL; machine->_configuration = [c copy];
    (void)machine.diskCapacityBytes; // Warm on the library's background scan.
    return machine;
}
- (BOOL)requiresInstallerDisplay {
    // Security updates boot a temporary OS without the installed system's PVG
    // patches. Preserve its verified receipt, but use software video whenever
    // Apple's live firmware variable explicitly selects that installer.
    if(VIMFirmwareBootTargetAtURL([self.URL URLByAppendingPathComponent:self.configuration[@"NVRAM"]])==VIMFirmwareBootInstaller)return YES;
    NSDictionary *updater=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/UpdateDisplay.plist"]];
    if([updater[@"FormatVersion"] isEqual:@1]&&[updater[@"Guest"] isEqual:self.guest]&&
       [updater[@"PreviousTransactionID"] isKindOfClass:NSString.class]&&[[NSUUID alloc] initWithUUIDString:updater[@"PreviousTransactionID"]]&&
       [updater[@"PreviousBuild"] isKindOfClass:NSString.class]&&[updater[@"PreviousBuild"] length]>0&&
       [updater[@"VolumeUUID"] isKindOfClass:NSString.class]&&[updater[@"VolumeUUID"] length]>0) {
        NSDictionary *state=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"]];
        NSDictionary *result=state[@"GuestResult"];
        // The updater may clear its firmware selection before the first normal
        // boot, although it has replaced PVG. A preparation/unknown write is
        // not enough: only a completed *new-build*, same-volume publish can
        // release the software-display hold for postboot Metal verification.
        BOOL repaired=self.hasPublishedGuestGraphics&&
            ![state[@"TransactionID"] isEqual:updater[@"PreviousTransactionID"]]&&
            [result[@"ProductBuildVersion"] isKindOfClass:NSString.class]&&
            ![result[@"ProductBuildVersion"] isEqual:updater[@"PreviousBuild"]]&&
            [result[@"VolumeUUID"] isEqual:updater[@"VolumeUUID"]];
        if(!repaired)return YES;
    }
    if(!self.configuration[@"InstallerMedia"] &&
       ![self.configuration[@"InstallState"] isEqual:@"host-populated"])return NO;
    return !self.hasPublishedGuestGraphics;
}
- (BOOL)prepareUpdaterDisplayForBoot:(NSError **)error {
    if(!self.hasPublishedGuestGraphics||VIMFirmwareBootTargetAtURL(
        [self.URL URLByAppendingPathComponent:self.configuration[@"NVRAM"]])!=VIMFirmwareBootInstaller)return YES;
    NSDictionary *state=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"]];
    NSDictionary *verified=state[@"PostBootVerification"]?:state[@"PreviousPostBootVerification"],*result=state[@"GuestResult"];
    if(![verified isKindOfClass:NSDictionary.class]||![verified[@"TransactionID"] isEqual:state[@"TransactionID"]]||
       ![verified[@"ProductBuildVersion"] isKindOfClass:NSString.class]||![verified[@"ProductBuildVersion"] length]||
       ![verified[@"VolumeUUID"] isKindOfClass:NSString.class]||![verified[@"VolumeUUID"] length]||
       ![verified[@"VerifiedAfterReboot"] isEqual:@YES]||![verified[@"MetalCommandVerified"] isEqual:@YES]||
       ![verified[@"ProductBuildVersion"] isEqual:result[@"ProductBuildVersion"]]||
       ![verified[@"VolumeUUID"] isEqual:result[@"VolumeUUID"]])
        return YES; // Firmware still selects software video; do not invent a verified baseline/automatic-repair authorization.
    NSDictionary *record=@{@"FormatVersion":@1,@"Guest":self.guest,@"PreviousTransactionID":state[@"TransactionID"],
        @"PreviousBuild":verified[@"ProductBuildVersion"],@"VolumeUUID":verified[@"VolumeUUID"],@"StartedAt":NSDate.date};
    return Save(record,[self.URL URLByAppendingPathComponent:@"State/UpdateDisplay.plist"],error);
}
- (BOOL)hasPublishedGuestGraphics {
    NSDictionary *state=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"]];
    NSString *identity=state[@"TransactionID"];
    NSDictionary *result=state[@"GuestResult"],*prepared=state[@"Preparation"];
    if(![state[@"FormatVersion"] isEqual:@1]||![identity isKindOfClass:NSString.class]||
       ![[NSUUID alloc] initWithUUIDString:identity]||
       ![result isKindOfClass:NSDictionary.class]||![prepared isKindOfClass:NSDictionary.class])return NO;
    // A preparation receipt alone, interrupted publication, or rollback must
    // never switch a fresh installation to a driver it cannot yet load.
    return ([@[@"awaiting-reboot",@"graphics-verified"] containsObject:state[@"Phase"]]&&
        [result[@"Phase"] isEqual:@"awaiting-reboot"]&&
        [result[@"TransactionID"] isEqual:identity]&&[prepared[@"TransactionID"] isEqual:identity]&&
        [prepared[@"GuestReport"] isKindOfClass:NSDictionary.class]&&
        [prepared[@"GuestReport"][@"Guest"] isEqual:self.guest]&&
        [prepared[@"GuestReport"][@"ProductBuildVersion"] isEqual:result[@"ProductBuildVersion"]]&&
        [prepared[@"GuestReport"][@"VolumeUUID"] isEqual:result[@"VolumeUUID"]]&&
        [result[@"FilesPublished"] isEqual:@YES]&&[result[@"RebootRequired"] isEqual:@YES]);
}
- (BOOL)rename:(NSString *)name error:(NSError **)error {
    if(!NameValid(name)||[name hasPrefix:@"."]||[name rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"/:\n\r"]].location!=NSNotFound)
        return Fail(error,@"Enter a name of 1–128 characters without slashes, colons, or a leading dot.");
    int fd=ConfigurationLock(self.URL,error);if(fd<0)return NO;
    NSURL *URL=[self.URL URLByAppendingPathComponent:@"State/PendingConfiguration.plist"];
    NSMutableDictionary *pending=[[NSDictionary dictionaryWithContentsOfURL:URL] mutableCopy]?:[NSMutableDictionary dictionary];pending[@"RequestedBundleName"]=name;
    BOOL saved=Save(pending,URL,error);close(fd);
    return saved&&[self applyPendingChanges:error];
}
- (BOOL)saveSettings:(NSDictionary *)settings error:(NSError **)error {
    NSSet *allowed=EditableSettingKeys();
    for(NSString *key in settings)if(![allowed containsObject:key])return Fail(error,@"Unsupported setting.");
    // Stage edits independently of the live launch snapshot. Never rewrite the
    // active configuration or move a live package. Session locking decides when
    // publication is safe, including when another app process owns the VM.
    int edit=ConfigurationLock(self.URL,error);if(edit<0)return NO;
    @try {
    NSMutableDictionary *candidate=[self.editableConfiguration mutableCopy];[candidate addEntriesFromDictionary:settings];
    if(!SettingsValid(candidate))return Fail(error,@"Use 1–64 processors, 1–1024 GB of memory and distinct ports from 1024–65535.");
    NSURL *pendingURL=[self.URL URLByAppendingPathComponent:@"State/PendingConfiguration.plist"];
    NSMutableDictionary *pending=[[NSDictionary dictionaryWithContentsOfURL:pendingURL] mutableCopy]?:[NSMutableDictionary dictionary];
    NSMutableDictionary *values=[pending[@"Settings"] mutableCopy]?:[NSMutableDictionary dictionary];[values addEntriesFromDictionary:settings];pending[@"Settings"]=values;
    if(!Save(pending,pendingURL,error))return NO;
    } @finally {close(edit);}
    return [self applyPendingChanges:error];
}
- (BOOL)applyPendingChanges:(NSError **)error {
    NSURL *lock=[self.URL URLByAppendingPathComponent:@"State/Session.lock"];
    int fd=open(lock.fileSystemRepresentation,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat info;
    if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1) {
        if(fd>=0)close(fd);return Fail(error,@"This Mac’s configuration lock could not be opened.");
    }
    if(flock(fd,LOCK_EX|LOCK_NB)){int issue=errno;close(fd);return issue==EWOULDBLOCK?YES:Fail(error,@"This Mac’s configuration lock is unavailable.");}
    int edit=ConfigurationLock(self.URL,error);if(edit<0){close(fd);return NO;}
    @try {
    VIMMachine *fresh=[VIMMachine loadURL:self.URL error:error];if(!fresh)return NO;
    NSURL *old=self.URL,*pendingURL=[old URLByAppendingPathComponent:@"State/PendingConfiguration.plist"];
    NSDictionary *pending=[NSDictionary dictionaryWithContentsOfURL:pendingURL];
    NSMutableDictionary *c=[fresh.configuration mutableCopy];
    if([pending[@"Settings"] isKindOfClass:NSDictionary.class]) {
        for(NSString *key in pending[@"Settings"])if(![EditableSettingKeys() containsObject:key])return Fail(error,@"The queued configuration contains an unsupported setting.");
        [c addEntriesFromDictionary:pending[@"Settings"]];
    }
    if(!SettingsValid(c))return Fail(error,@"Use 1–64 processors, 1–1024 GB of memory and distinct ports from 1024–65535.");
    NSDictionary *network=VIMNetworkConfigurationWithDefaults(c,self.URL.URLByDeletingLastPathComponent,self.URL);
    if(!network)return Fail(error,@"No local connection ports are available. Close unused virtual Macs or network services, then try again.");
    [c addEntriesFromDictionary:network];
    NSString *requested=pending[@"RequestedBundleName"];
    if(!requested&&[[NSUUID alloc] initWithUUIDString:old.lastPathComponent.stringByDeletingPathExtension]&&[c[@"Name"] isKindOfClass:NSString.class])requested=c[@"Name"];
    NSURL *destination=old;
    if(NameValid(requested)&&![requested isEqual:old.lastPathComponent.stringByDeletingPathExtension])destination=NewPackageURL(old.URLByDeletingLastPathComponent,requested);
    BOOL moved=![destination isEqual:old];
    if(moved&&![NSFileManager.defaultManager moveItemAtURL:old toURL:destination error:error])return NO;
    BOOL needsSave=pending!=nil||c[@"Name"]!=nil||![c isEqualToDictionary:fresh.configuration];
    [c removeObjectForKey:@"Name"];
    if(needsSave&&!Save(c,[destination URLByAppendingPathComponent:@"Machine.plist"],error)) {
        if(moved)[NSFileManager.defaultManager moveItemAtURL:destination toURL:old error:nil];return NO;
    }
    _URL=destination;_configuration=[c copy];
    if(moved)RelocatedPackage(old,destination,c[@"Identifier"]);
    if(pending&&![NSFileManager.defaultManager removeItemAtURL:[destination URLByAppendingPathComponent:@"State/PendingConfiguration.plist"] error:error])return NO;
    return YES;
    } @finally {close(edit);flock(fd,LOCK_UN);close(fd);}
}
- (BOOL)claimAutomaticInstallationWithGuestReport:(NSDictionary *)report error:(NSError **)error {
    if(![@[@"mojave",@"catalina",@"bigsur"] containsObject:self.guest]||
       ![self.configuration[@"InstallState"] isEqual:@"installing"]||
       !self.configuration[@"InstallerMedia"]||
       ![report[@"Guest"] isEqual:self.guest]||![report[@"InstalledSystemCandidate"] isEqual:@YES]||
       ![report[@"DesktopRunning"] isEqual:@YES]||
       ![report[@"LiveRoot"] isEqual:@YES]||![report[@"KernelMatchesSystem"] isEqual:@YES])return NO;
    for(NSString *key in @[@"VolumeUUID",@"BootSessionUUID",@"ProductBuildVersion"])
        if(![report[key] isKindOfClass:NSString.class]||![report[key] length])return NO;
    NSFileManager *fm=NSFileManager.defaultManager;
    for(NSString *name in @[@"GuestPreparation.plist",@"GuestInstallation.plist",@"AutomaticInstallation.plist"])
        if([fm fileExistsAtPath:[self.URL URLByAppendingPathComponent:[@"State" stringByAppendingPathComponent:name]].path])return NO;
    NSDictionary *owner=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"Installation/Ownership.plist"]];
    if(![owner[@"FormatVersion"] isEqual:@1]||![owner[@"MachineIdentifier"] isEqual:self.configuration[@"Identifier"]]||
       ![owner[@"Media"] isEqual:self.configuration[@"InstallerMedia"]])return NO;
    NSDictionary *claim=@{@"FormatVersion":@1,@"MachineIdentifier":self.configuration[@"Identifier"],
        @"Phase":@"claimed",@"GuestReport":report,@"StartedAt":NSDate.date};
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:claim format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
    return data&&[data writeToURL:[self.URL URLByAppendingPathComponent:@"State/AutomaticInstallation.plist"]
        options:NSDataWritingWithoutOverwriting error:error];
}
- (BOOL)claimAutomaticUpdateRepairWithGuestReport:(NSDictionary *)report error:(NSError **)error {
    return [self authorizeUpdateRepair:report claim:YES error:error];
}
- (BOOL)canRepairUpdatedSystemWithGuestReport:(NSDictionary *)report error:(NSError **)error {
    return [self authorizeUpdateRepair:report claim:NO error:error];
}
- (BOOL)authorizeUpdateRepair:(NSDictionary *)report claim:(BOOL)claim error:(NSError **)error {
    if(!Supported(self.guest)||![report[@"Guest"] isEqual:self.guest]||
       ![report[@"InstalledSystemCandidate"] isEqual:@YES]||
       ![report[@"LiveRoot"] isEqual:@YES]||![report[@"KernelMatchesSystem"] isEqual:@YES])return NO;
    for(NSString *key in @[@"VolumeUUID",@"BootSessionUUID",@"ProductBuildVersion"])
        if(![report[key] isKindOfClass:NSString.class]||![report[key] length])return NO;
    // Unlike initial installation, repair must not require Finder: an update
    // may replace the very driver/framework that permits WindowServer to run.
    // The prior verified transaction, same-volume/new-build checks and exclusive
    // once-per-build claim below remain mandatory before authorizing any write.
    NSString *build=report[@"ProductBuildVersion"];
    // Build identifiers are used only as receipt filenames, never as commands.
    // Refuse paths or malformed evidence rather than normalizing two builds to
    // the same claim. The guest status tool separately recognizes OS families.
    NSCharacterSet *buildCharacters=[NSCharacterSet characterSetWithCharactersInString:@"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-_"];
    if(build.length>128||[build hasPrefix:@"."]||[build rangeOfCharacterFromSet:buildCharacters.invertedSet].location!=NSNotFound)return NO;
    int lock=ConfigurationLock(self.URL,error);if(lock<0)return NO;
    @try {
        NSURL *journal=[self.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"];
        NSDictionary *state=[NSDictionary dictionaryWithContentsOfURL:journal];
        NSString *identity=state[@"TransactionID"];
        NSDictionary *verified=state[@"PostBootVerification"]?:state[@"PreviousPostBootVerification"];
        // A read-only reconciliation after an update can already have reported
        // an old-build verification failure. Retain the last proven success;
        // never treat an unverified/interrupted new write as permission to
        // retry. A failed read of a completed *old-build* transaction is
        // different: the retained Metal proof, ID, disk and build must agree.
        if(!VIMVerifiedInstallationSuperseded(state,report))return NO;
        NSString *previous=verified[@"ProductBuildVersion"];
        if(!claim)return YES;
        NSURL *directory=[self.URL URLByAppendingPathComponent:@"State/UpdateRepairs" isDirectory:YES];
        if(mkdir(directory.fileSystemRepresentation,0700)&&errno!=EEXIST)
            return Fail(error,@"The macOS update’s graphics setup record could not be saved. Make sure the library drive is connected and writable, then restart the virtual Mac.");
        struct stat info;
        if(lstat(directory.fileSystemRepresentation,&info)||!S_ISDIR(info.st_mode))
            return Fail(error,@"The macOS update repair records folder is not a regular folder. No automatic repair was started.");
        NSURL *claim=[directory URLByAppendingPathComponent:[build stringByAppendingPathExtension:@"plist"]];
        NSDictionary *record=@{@"FormatVersion":@1,@"MachineIdentifier":self.configuration[@"Identifier"],
            @"Phase":@"claimed",@"PreviousTransactionID":identity,@"PreviousBuild":previous,
            @"GuestReport":report,@"StartedAt":NSDate.date};
        NSData *data=[NSPropertyListSerialization dataWithPropertyList:record format:NSPropertyListBinaryFormat_v1_0 options:0 error:error];
        NSError *writeError=nil;
        if(data&&[data writeToURL:claim options:NSDataWritingWithoutOverwriting error:&writeError])return YES;
        // Recover only a pre-dispatch failure: the old proven journal is still
        // present with its original ID and a failed *read*. A dispatched new
        // installation exclusively creates a new ID/journal after archival,
        // so it cannot satisfy this condition. Never reuse that new attempt.
        if(writeError.code==NSFileWriteFileExistsError&&[writeError.domain isEqual:NSCocoaErrorDomain]) {
            NSDictionary *saved=[NSDictionary dictionaryWithContentsOfURL:claim];
            BOOL recover=[state[@"Phase"] isEqual:@"outcome-unknown"]&&[state[@"ReconciliationError"] isKindOfClass:NSString.class]&&[state[@"ReconciliationError"] length]>0&&
                [saved[@"FormatVersion"] isEqual:@1]&&[saved[@"Phase"] isEqual:@"claimed"]&&
                [saved[@"MachineIdentifier"] isEqual:self.configuration[@"Identifier"]]&&
                [saved[@"PreviousTransactionID"] isEqual:identity]&&[saved[@"PreviousBuild"] isEqual:previous]&&
                [saved[@"GuestReport"][@"VolumeUUID"] isEqual:report[@"VolumeUUID"]]&&
                [saved[@"GuestReport"][@"ProductBuildVersion"] isEqual:build];
            if(!recover)return NO;
            NSMutableDictionary *recovery=[saved mutableCopy];
            recovery[@"Phase"]=@"preflight-recovered";recovery[@"RecoveredAt"]=NSDate.date;
            recovery[@"RecoveryGuestReport"]=report;
            // A separate durable transition bounds this migration to once,
            // even if preparation subsequently fails or the app restarts.
            return Save(recovery,claim,error);
        }
        if(error&&writeError)*error=writeError;return NO;
    } @finally {close(lock);}
}
- (BOOL)retryInstallerCleanup:(NSError **)error {
    NSDictionary *pending=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/Install.plist"]];
    if(![pending[@"Phase"] isEqual:@"cleanup-pending"]) {
        // Verification is durable, but the session's completionGuestReport is
        // not. Recover it after an app restart/close instead of indefinitely
        // retaining the installer when the original session no longer exists.
        NSDictionary *installation=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"]];
        if(!self.configuration[@"InstallerMedia"]||![installation[@"Phase"] isEqual:@"graphics-verified"])return YES;
        NSDictionary *observed=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/GuestStatus.plist"]];
        NSDictionary *report=observed[@"Report"];
        if(![report isKindOfClass:NSDictionary.class])return Fail(error,@"Saved guest status is unavailable. Installer media was retained.");
        // finishInstallation rechecks the transaction, boot, volume, guest,
        // ownership and stopped-session lock before changing any media.
        return [self finishInstallationWithGuestReport:report error:error];
    }
    if(![pending[@"Identifier"] isEqual:self.configuration[@"Identifier"]]||
       ![pending[@"GuestReport"] isKindOfClass:NSDictionary.class])
        return Fail(error,@"Saved installer cleanup does not match this Mac. Media was retained.");
    return [self finishInstallationWithGuestReport:pending[@"GuestReport"] error:error];
}
- (BOOL)finishInstallationWithGuestReport:(NSDictionary *)report error:(NSError **)error {
    NSURL *lock=[self.URL URLByAppendingPathComponent:@"State/Session.lock"];
    int fd=open(lock.fileSystemRepresentation,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat info;
    if(fd<0||fstat(fd,&info)||!S_ISREG(info.st_mode)||info.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB)) {
        if(fd>=0)close(fd);return Fail(error,@"Shut down this Mac before finishing installer cleanup.");
    }
    @try {
        VIMMachine *fresh=[VIMMachine loadURL:self.URL error:error];if(!fresh)return NO;
        NSDictionary *state=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"State/GuestInstallation.plist"]];
        NSDictionary *verified=state[@"PostBootVerification"];
        if(![state[@"Phase"] isEqual:@"graphics-verified"]||
           ![verified[@"TransactionID"] isEqual:state[@"TransactionID"]]||
           ![verified[@"VerifiedAfterReboot"] isEqual:@YES]||![verified[@"MetalCommandVerified"] isEqual:@YES]||
           ![report[@"InstalledSystemCandidate"] isEqual:@YES]||![report[@"LiveRoot"] isEqual:@YES]||
           ![report[@"KernelMatchesSystem"] isEqual:@YES]||![report[@"Guest"] isEqual:fresh.guest]||
           ![report[@"VolumeUUID"] isEqual:verified[@"VolumeUUID"]]||
           ![report[@"BootSessionUUID"] isKindOfClass:NSString.class]||![report[@"BootSessionUUID"] length]||
           // A verified installation survives ordinary later reboots. The
           // transaction/volume/build and actual Metal proof must still match;
           // requiring the original verification boot stranded installer media.
           ![verified[@"BootSessionUUID"] isKindOfClass:NSString.class]||![verified[@"BootSessionUUID"] length]||
           ![report[@"ProductBuildVersion"] isEqual:verified[@"ProductBuildVersion"]])
            return Fail(error,@"Matching completed guest installation and graphics verification are required. Media was retained.");
        NSURL *media=[self.URL URLByAppendingPathComponent:@"Installation/Installer.iso"];
        NSDictionary *owner=[NSDictionary dictionaryWithContentsOfURL:[self.URL URLByAppendingPathComponent:@"Installation/Ownership.plist"]];
        // Shared library media belongs to the download cache, not this VM. Detach
        // it after verification, but never delete another VM's or user's ISO.
        if([fresh.configuration[@"InstallerMediaShared"] isEqual:@YES]) {
            if(![owner[@"MachineIdentifier"] isEqual:fresh.configuration[@"Identifier"]]||
               ![owner[@"Media"] isEqual:fresh.configuration[@"InstallerMedia"]])return Fail(error,@"Installer reference does not match its record.");
            NSMutableDictionary *config=[fresh.configuration mutableCopy];
            [config removeObjectsForKeys:@[@"InstallerMedia",@"InstallerMediaKind",@"InstallerMediaShared"]];
            config[@"InstallState"]=@"installed";
            if(!Save(config,[self.URL URLByAppendingPathComponent:@"Machine.plist"],error))return NO;
            _configuration=[config copy];
            BOOL removed=RemovePreparedMedia(fresh.configuration[@"InstallerMedia"],self.URL);
            return Save(@{@"Phase":@"complete",@"Identifier":config[@"Identifier"],@"InstallerDetached":@YES,
                @"SharedMediaRetained":@(!removed),@"CompletedAt":NSDate.date},[self.URL URLByAppendingPathComponent:@"State/Install.plist"],error);
        }
        if(![owner[@"FormatVersion"] isEqual:@1]||![owner[@"MachineIdentifier"] isEqual:fresh.configuration[@"Identifier"]]||
           ![owner[@"Media"] isEqual:@"Installation/Installer.iso"]||
           (fresh.configuration[@"InstallerMedia"]&&![fresh.configuration[@"InstallerMedia"] isEqual:owner[@"Media"]]))
            return Fail(error,@"Installer ownership could not be established. Media was retained.");
        // Never follow a relocated Installation directory or delete a linked file.
        if(![media.URLByResolvingSymlinksInPath.path isEqual:media.path])
            return Fail(error,@"Installer media has been relocated. Automatic cleanup is unavailable.");
        BOOL exists=lstat(media.fileSystemRepresentation,&info)==0;
        if(!exists&&errno!=ENOENT)return Fail(error,@"Cannot inspect installer media.");
        if(exists&&(!S_ISREG(info.st_mode)||info.st_nlink!=1||info.st_size<=0||
                    (uint64_t)info.st_size!=[owner[@"Size"] unsignedLongLongValue]))
            return Fail(error,@"Installer media no longer matches its ownership record.");
        NSFileManager *fm=NSFileManager.defaultManager;
        for(NSString *key in @[@"BootMedia",@"Disk",@"Firmware",@"NVRAM"])
            if([[[self.URL URLByAppendingPathComponent:fresh.configuration[key]] URLByResolvingSymlinksInPath].path isEqual:media.path])
                return Fail(error,@"Installer media is still used by another device in this Mac.");
        NSArray *siblings=[fm contentsOfDirectoryAtURL:self.URL.URLByDeletingLastPathComponent includingPropertiesForKeys:nil options:0 error:error];
        if(!siblings)return NO;
        for(NSURL *sibling in siblings) {
            if(![sibling.pathExtension isEqual:@"vintelmac"]||[sibling.URLByResolvingSymlinksInPath.path isEqual:self.URL.path])continue;
            NSDictionary *config=[NSDictionary dictionaryWithContentsOfURL:[sibling URLByAppendingPathComponent:@"Machine.plist"]];
            if(!config)return Fail(error,@"Another Mac's configuration could not be checked. Media was retained.");
            for(NSString *key in @[@"InstallerMedia",@"BootMedia",@"Disk",@"Firmware",@"NVRAM"]) {
                NSString *relative=config[key];if(![relative isKindOfClass:NSString.class])continue;
                NSURL *reference=[sibling URLByAppendingPathComponent:relative].URLByResolvingSymlinksInPath;
                if([reference.path isEqual:media.path])return Fail(error,@"Another Mac still references this installer.");
            }
        }
        NSMutableDictionary *config=[fresh.configuration mutableCopy];
        [config removeObjectForKey:@"InstallerMedia"];[config removeObjectForKey:@"InstallerMediaKind"];config[@"InstallState"]=@"installed";
        // Persist the validated evidence before retiring routing. A deletion or
        // final journal failure can then be retried after app/process restart.
        if(!Save(@{@"Phase":@"cleanup-pending",@"Identifier":config[@"Identifier"],
            @"TransactionID":verified[@"TransactionID"],@"GuestReport":report,@"UpdatedAt":NSDate.date},
            [self.URL URLByAppendingPathComponent:@"State/Install.plist"],error))return NO;
        // Config first: a crash can leave surplus media, never a dangling boot
        // reference. A subsequent call can finish the same cleanup idempotently.
        if(!Save(config,[self.URL URLByAppendingPathComponent:@"Machine.plist"],error))return NO;
        _configuration=[config copy];
        BOOL keep=[NSUserDefaults.standardUserDefaults boolForKey:@"VIMKeepInstallers"];
        if(exists&&!keep&&![fm removeItemAtURL:media error:error])return NO;
        return Save(@{@"Phase":@"complete",@"Identifier":config[@"Identifier"],
            @"TransactionID":verified[@"TransactionID"],@"InstallerMediaRemoved":@(!keep),@"CompletedAt":NSDate.date},
            [self.URL URLByAppendingPathComponent:@"State/Install.plist"],error);
    } @finally {flock(fd,LOCK_UN);close(fd);}
}
+ (instancetype)importDisk:(NSURL *)disk guest:(NSString *)guest name:(NSString *)name
    library:(NSURL *)library firmware:(NSURL *)firmware nvram:(NSURL *)nvram
    bootMedia:(NSURL *)bootMedia qemuImage:(NSURL *)qemuImage error:(NSError **)error {
    if (!Supported(guest) || !NameValid(name)) { Fail(error, @"Unsupported guest or invalid name."); return nil; }
    NSFileManager *fm = NSFileManager.defaultManager;
    library=[VIMLibraryLocation machinesURLForLibrary:library];
    if(![fm createDirectoryAtURL:library withIntermediateDirectories:YES attributes:nil error:error])return nil;
    for (NSURL *input in @[disk, firmware, nvram, bootMedia, qemuImage]) {
        NSNumber *regular = nil;
        if (!input.isFileURL || ![input getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error] || !regular.boolValue) return nil;
    }
    NSString *identifier = NSUUID.UUID.UUIDString;
    NSURL *stage = [library URLByAppendingPathComponent:[NSString stringWithFormat:@".%@.importing", identifier]];
    NSURL *destination = NewPackageURL(library,name);
    name=destination.lastPathComponent.stringByDeletingPathExtension;
    if (![fm createDirectoryAtURL:stage withIntermediateDirectories:NO attributes:nil error:error]) return nil;
    // Import is a snapshot copy: qemu-img reads/locks the entire source chain.
    // Never use -U (force-share), rebase originals, or copy a live writable disk.
    NSURL *info = [stage URLByAppendingPathComponent:@"SourceDisk.json"];
    if (!Run(qemuImage, @[@"info", @"--output=json", disk.path], info, error)) return nil;
    NSData *infoData = [NSData dataWithContentsOfURL:info];
    NSDictionary *metadata = infoData ? [NSJSONSerialization JSONObjectWithData:infoData options:0 error:error] : nil;
    if (![metadata isKindOfClass:NSDictionary.class] || ![metadata[@"format"] isEqual:@"qcow2"] ||
        ![metadata[@"virtual-size"] isKindOfClass:NSNumber.class]) { Fail(error, @"Import requires a valid QCOW2 disk."); return nil; }
    NSDictionary *space = [fm attributesOfFileSystemForPath:library.path error:error];
    // Conservative worst-case admission; sparse output usually uses much less.
    unsigned long long required = [metadata[@"virtual-size"] unsignedLongLongValue];
    unsigned long long free = [space[NSFileSystemFreeSize] unsignedLongLongValue];
    if (!required || free < required || free - required < (2ULL << 30)) { Fail(error, @"The selected library needs more free space for a safe import."); return nil; }
    for (NSString *directory in @[@"Disks", @"Firmware", @"Logs", @"State"])
        if (![fm createDirectoryAtURL:[stage URLByAppendingPathComponent:directory] withIntermediateDirectories:NO attributes:nil error:error]) return nil;
    NSDictionary *journal = @{@"Phase":@"importing", @"Source":disk.path, @"Identifier":identifier};
    if (!Save(journal, [stage URLByAppendingPathComponent:@"State/Import.plist"], error)) return nil;
    NSURL *target = [stage URLByAppendingPathComponent:@"Disks/System.qcow2"];
    if (!Run(qemuImage, @[@"convert", @"-p", @"-f", @"qcow2", @"-O", @"qcow2", disk.path, target.path],
        [stage URLByAppendingPathComponent:@"Logs/Import.log"], error)) return nil;
    if (!Run(qemuImage, @[@"info", @"--output=json", target.path], [stage URLByAppendingPathComponent:@"Disk.json"], error)) return nil;
    NSDictionary *result = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfURL:[stage URLByAppendingPathComponent:@"Disk.json"]]
        options:0 error:error];
    if (![result isKindOfClass:NSDictionary.class] || result[@"backing-filename"] ||
        ![result[@"virtual-size"] isEqual:metadata[@"virtual-size"]]) { Fail(error, @"The imported disk did not pass the independence check."); return nil; }
    NSArray *sources = @[firmware, nvram, bootMedia];
    BOOL efiDisk=EFIDisk(bootMedia);
    NSArray *paths = @[@"Firmware/CODE.fd", @"Firmware/NVRAM.fd", efiDisk?@"Firmware/EFI.img":@"Firmware/OpenCore.iso"];
    for (NSUInteger i = 1; i < 2; i++)
        if (![fm copyItemAtURL:sources[i] toURL:[stage URLByAppendingPathComponent:paths[i]] error:error]) return nil;
    NSDictionary *c = VIMNetworkConfigurationWithDefaults(@{@"FormatVersion":@1, @"Identifier":identifier, @"Guest":guest,
        @"Disk":@"Disks/System.qcow2", @"Firmware":paths[0], @"NVRAM":paths[1], @"BootMedia":paths[2],
        @"BootMediaKind":efiDisk?@"efi-disk":@"legacy-opencore-iso", @"CPUCount":@4, @"MemoryMiB":@4096,
        @"FirmwareSource":@"application", @"InstallState":@"imported-prepared", @"ImportedFrom":disk.path},library,destination);
    if(!c){Fail(error,@"No local connection ports are available. Close unused virtual Macs or network services, then try again.");return nil;}
    if (!Save(c, [stage URLByAppendingPathComponent:@"Machine.plist"], error) ||
        !Save(@{@"Phase":@"complete", @"Source":disk.path}, [stage URLByAppendingPathComponent:@"State/Import.plist"], error) ||
        ![fm moveItemAtURL:stage toURL:destination error:error]) return nil;
    return [self loadURL:destination error:error];
}
+ (instancetype)createGuest:(NSString *)guest name:(NSString *)name diskGiB:(NSUInteger)diskGiB
    library:(NSURL *)library firmware:(NSURL *)firmware nvram:(NSURL *)nvram
    bootMedia:(NSURL *)bootMedia installer:(NSURL *)installer qemuImage:(NSURL *)qemuImage error:(NSError **)error {
    if(!Supported(guest)||!NameValid(name)||diskGiB<40||diskGiB>2048||!library.isFileURL) {
        Fail(error,@"Choose a supported macOS version, a name, and a disk size from 40 to 2048 GB.");return nil;
    }
    NSFileManager *fm=NSFileManager.defaultManager;
    library=[VIMLibraryLocation machinesURLForLibrary:library];
    if(![fm createDirectoryAtURL:library withIntermediateDirectories:YES attributes:nil error:error])return nil;
    for(NSURL *input in @[firmware,nvram,bootMedia,installer,qemuImage]) {
        NSNumber *regular=nil;
        if(!input.isFileURL||![input getResourceValue:&regular forKey:NSURLIsRegularFileKey error:error]||!regular.boolValue) {
            Fail(error,@"A required firmware, installer, or disk tool file is missing.");return nil;
        }
    }
    NSDictionary *attributes=[fm attributesOfItemAtPath:installer.path error:error];
    NSDictionary *space=[fm attributesOfFileSystemForPath:library.path error:error];
    if(!attributes||!space)return nil;
    uint64_t imageSize=[attributes[NSFileSize] unsignedLongLongValue];
    uint64_t free=[space[NSFileSystemFreeSize] unsignedLongLongValue];
    // Installer media is shared in place, not copied into every VM. The disk
    // is sparse: require room for an installed system, not its full capacity.
    uint64_t diskSize=MIN((uint64_t)diskGiB,24ULL)<<30;
    if(!imageSize||free<diskSize+(2ULL<<30)) {
        Fail(error,@"The library needs enough free space for the requested disk.");return nil;
    }
    NSString *identifier=NSUUID.UUID.UUIDString;
    NSURL *stage=[library URLByAppendingPathComponent:[NSString stringWithFormat:@".%@.creating",identifier]];
    NSURL *destination=NewPackageURL(library,name);
    name=destination.lastPathComponent.stringByDeletingPathExtension;
    if(![fm createDirectoryAtURL:stage withIntermediateDirectories:NO attributes:nil error:error])return nil;
    for(NSString *folder in @[@"Disks",@"Firmware",@"State",@"Logs",@"Installation"])
        if(![fm createDirectoryAtURL:[stage URLByAppendingPathComponent:folder] withIntermediateDirectories:NO attributes:nil error:error])return nil;
    if(!Save(@{@"Phase":@"creating",@"Identifier":identifier},[stage URLByAppendingPathComponent:@"State/Install.plist"],error))return nil;
    if(!CreateFormattedDisk(stage,qemuImage,diskGiB,error))return nil;
    NSArray *sources=@[firmware,nvram,bootMedia,installer];
    BOOL efiDisk=EFIDisk(bootMedia);
    NSArray *paths=@[@"Firmware/CODE.fd",@"Firmware/NVRAM.fd",efiDisk?@"Firmware/EFI.img":@"Firmware/OpenCore.iso",installer.URLByResolvingSymlinksInPath.path];
    for(NSUInteger i=1;i<2;i++)
        if(![fm copyItemAtURL:sources[i] toURL:[stage URLByAppendingPathComponent:paths[i]] error:error])return nil;
    // Ownership identity is distinct from BootMedia. Later cleanup must verify
    // it and authoritative guest completion; a desktop frame is not completion.
    if(!Save(@{@"FormatVersion":@1,@"MachineIdentifier":identifier,@"Media":paths[3],@"Size":@(imageSize)},
        [stage URLByAppendingPathComponent:@"Installation/Ownership.plist"],error))return nil;
    NSDictionary *configuration=VIMNetworkConfigurationWithDefaults(@{@"FormatVersion":@1,@"Identifier":identifier,@"Guest":guest,
        @"CPUCount":@4,@"MemoryMiB":@4096,@"Disk":@"Disks/System.qcow2",@"DiskGiB":@(diskGiB),
        @"Firmware":paths[0],@"NVRAM":paths[1],@"BootMedia":paths[2],@"BootMediaKind":efiDisk?@"efi-disk":@"legacy-opencore-iso",
        @"FirmwareSource":@"application",@"InstallerMediaShared":@YES,
        @"InstallerMedia":paths[3],@"InstallerMediaKind":EFIDisk(installer)?@"disk":@"optical",@"InstallState":@"installing"},library,destination);
    if(!configuration){Fail(error,@"No local connection ports are available. Close unused virtual Macs or network services, then try again.");return nil;}
    if(!Save(configuration,[stage URLByAppendingPathComponent:@"Machine.plist"],error)||
       !Save(@{@"Phase":@"awaiting-os-install",@"Identifier":identifier},[stage URLByAppendingPathComponent:@"State/Install.plist"],error)||
       ![fm moveItemAtURL:stage toURL:destination error:error])return nil;
    return [self loadURL:destination error:error];
}
@end
