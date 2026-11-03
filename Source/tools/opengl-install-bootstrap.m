// Apple's imaging extension invokes this compiled executable before OSXInstaller.
// Only metadata uses UART. Payload bytes stay on local read-only support media.
#import <Foundation/Foundation.h>
#import "opengl-system-version.h"
#import <IOKit/IOKitLib.h>
#import <CommonCrypto/CommonDigest.h>
#include "opengl-launchd-state.h"
#import "opengl-guest-machine.h"
#import "opengl-efi-media.h"
#include "opengl-installer-paths.h"
#include "opengl-state-path.h"
#include "opengl-boot-identity.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#include <spawn.h>
#include <sys/wait.h>
#include <crt_externs.h>
#include "opengl-disk-readiness.h"
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
#import <DiskArbitration/DiskArbitration.h>
#endif
static NSString *const Tools=@"/Library/Application Support/Virtual Intel Mac/GuestTools";
static NSString *const State=VIM_OPENGL_STATE_PATH;
static NSString *const MediaTools=@"/System/Installation/Packages/Extras/VirtualIntelMac";
static NSString *const HighSierraInstallerState=@"/Library/Application Support/Virtual Intel Mac/InstallerSetup";
// Dynamically loaded only in High Sierra recovery. Earlier guests keep their
// native installer contract and acquire no new framework/runtime dependency.
@interface NSObject (VIMHighSierraInstallerProduct)
+ (id)defaultProductManager;
- (BOOL)setMutableProductPath:(NSString *)path error:(NSError **)error;
- (NSMutableDictionary *)attributesDictionary;
- (BOOL)flushAttributesDictionary;
- (BOOL)automationFileIsSafeWithError:(NSError **)error;
@end
static NSString *CurrentGuest(void){
    NSString *version=[[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"] objectForKey:@"ProductVersion"];
    return VIMOpenGLGuestForSystemVersion(version);
}
static int Error(const char *stage){fprintf(stderr,"Virtual Intel Mac setup: %s\n",stage);return 1;}
static BOOL WaitForLeopardDisks(void){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    if([CurrentGuest() isEqual:@"leopard"]){
        DASessionRef session=DASessionCreate(NULL);
        if(!session)return NO;
        VIMOpenGLWaitForDisks(session);
        CFRelease(session);
    }
#endif
    return YES;
}
static BOOL PrepareLeopardPowerPolicy(void){
    if(![CurrentGuest() isEqual:@"leopard"])return YES;
    // Darwin9 IOPMrootDomain::setProperties owns this native boolean and
    // userDisabledAllSleep gate. Apply the appliance's existing no-system-
    // sleep policy before long unattended setup, not only after a desktop
    // report. Do not suppress display sleep or poll/tickle the guest.
    io_service_t root=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("IOPMrootDomain"));
    if(!root)return NO;
    kern_return_t result=IORegistryEntrySetCFProperty(root,CFSTR("SleepDisabled"),kCFBooleanTrue);
    CFTypeRef value=IORegistryEntryCreateCFProperty(root,CFSTR("SleepDisabled"),kCFAllocatorDefault,0);
    BOOL verified=result==KERN_SUCCESS&&value&&CFGetTypeID(value)==CFBooleanGetTypeID()&&CFBooleanGetValue((CFBooleanRef)value);
    if(value)CFRelease(value);IOObjectRelease(root);return verified;
}
static BOOL UUID(id value){CFUUIDRef v=[value isKindOfClass:[NSString class]]?CFUUIDCreateFromString(NULL,(CFStringRef)value):NULL;if(v)CFRelease(v);return v!=NULL;}
static BOOL Parents(NSString *path){
    struct stat st;
    if(!lstat([path fileSystemRepresentation],&st))return S_ISDIR(st.st_mode);
    if(errno!=ENOENT||![path length]||[path isEqual:@"/"]||!Parents([path stringByDeletingLastPathComponent]))return NO;
    // umask 077 protects transaction files, but must not turn shared OS/app
    // ancestors into 0700 directories before Apple's installer sees them.
    return !mkdir([path fileSystemRepresentation],0755)&&
        !chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],0755);
}
static BOOL Directory(NSString *path,mode_t mode){
    return Parents(path)&&!chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],mode);
}
static BOOL Write(NSDictionary *value,NSString *path){
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    return data&&[data writeToFile:path options:NSDataWritingAtomic error:NULL]&&!chmod([path fileSystemRepresentation],0600);
}
static BOOL PrepareHighSierraProduct(NSString *root,NSString *uuid){
    if(![CurrentGuest() isEqual:@"highsierra"])return YES;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    // Tiger/Leopard/Snow keep compiling against their actual SDKs. This
    // binary is not a High Sierra support payload.
    (void)root;(void)uuid;return NO;
#else
    // Native17 OSProductManager rejects unattended minstall when no mutable
    // product/IAEndDate was handed off. Let Apple's own PackageKit factory
    // write its secure product index; do not manufacture one or disable the
    // automation-safety, signed-package or target checks. This directory is
    // on the already UUID-authorized new VM disk, never on the host.
    NSString *relative=@"/Library/Application Support/Virtual Intel Mac/InstallerProduct";
    NSString *path=[root stringByAppendingString:relative];
    NSError *error=nil;
    NSBundle *framework=[NSBundle bundleWithPath:@"/System/Library/PrivateFrameworks/OSInstaller.framework"];
    if(!Directory(path,0700)||![framework loadAndReturnError:&error])return NO;
    Class factory=NSClassFromString(@"OSProductManager");
    if(![factory respondsToSelector:@selector(defaultProductManager)])return NO;
    id product=[factory defaultProductManager];
    if(![product setMutableProductPath:path error:&error])return NO;
    [[product attributesDictionary] setObject:[NSDate date] forKey:@"IAEndDate"];
    if(![product flushAttributesDictionary]||![product automationFileIsSafeWithError:&error])return NO;
    // _configuredProductManagerFromIA resolves the URL's host as a volume
    // UUID and its path relative to that volume, independent of its name.
    // Use the native guest NVRAM handoff, not injection into the signed app.
    NSString *url=[NSString stringWithFormat:@"file://%@%@",uuid,
        [relative stringByAddingPercentEscapesUsingEncoding:NSUTF8StringEncoding]];
    io_registry_entry_t options=IORegistryEntryFromPath(kIOMasterPortDefault,"IODeviceTree:/options");
    if(!options)return NO;
    kern_return_t result=IORegistryEntrySetCFProperty(options,CFSTR("install-product-url"),(CFStringRef)url);
    IOObjectRelease(options);return result==KERN_SUCCESS;
#endif
}
static BOOL Copy(NSString *source,NSString *target,mode_t mode){
    struct stat st;if(lstat([source fileSystemRepresentation],&st)||!S_ISREG(st.st_mode)||st.st_size>64*1024*1024)return NO;
    NSData *data=[NSData dataWithContentsOfFile:source];
    return data&&[data writeToFile:target options:NSDataWritingAtomic error:NULL]&&!chown([target fileSystemRepresentation],0,0)&&!chmod([target fileSystemRepresentation],mode);
}
static NSString *Digest(NSString *path){
    int fd=open([path fileSystemRepresentation],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return nil;
    struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size>64*1024*1024){close(fd);return nil;}
    CC_SHA256_CTX hash;CC_SHA256_Init(&hash);unsigned char bytes[32768];ssize_t n;
    do{n=read(fd,bytes,sizeof(bytes));if(n>0)CC_SHA256_Update(&hash,bytes,(CC_LONG)n);}while(n>0||(n<0&&errno==EINTR));close(fd);if(n<0)return nil;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256_Final(digest,&hash);NSMutableString *value=[NSMutableString string];
    for(unsigned i=0;i<sizeof(digest);i++)[value appendFormat:@"%02x",digest[i]];return value;
}
#include "guest-hidpi-policy.h"
static BOOL PrepareLionDisplayPolicy(BOOL *changed){
    *changed=NO;if(![CurrentGuest() isEqual:@"lion"])return YES;
    // Actual 11G63 CGXDisplayDriverInitialize gates generic-machine HiDPI
    // enumeration on this native preference. Its _initServerPreferences
    // reads AnyUser/CurrentHost, not an invented renderer capability. Expose
    // the driver's real 2x modes once; do not force a mode, change SMBIOS,
    // or override a user's later explicit resolution-policy choice.
    return VIMPrepareNativeHiDPIPolicy(changed);
}
static BOOL RegisterEFIMedia(NSString *build){
    if(![CurrentGuest() isEqual:@"leopard"]&&![CurrentGuest() isEqual:@"snowleopard"]&&![CurrentGuest() isEqual:@"mountainlion"]&&![CurrentGuest() isEqual:@"lion"])return YES;
    NSString *path=@"/System/Library/Filesystems/msdos.fs/Contents/Info.plist";
    struct stat st;
    if(lstat([path fileSystemRepresentation],&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||st.st_size>1024*1024)return NO;
    NSDictionary *original=[NSDictionary dictionaryWithContentsOfFile:path];
    NSDictionary *updated=VIMOpenGLEFIMediaInfo(original);
    if(!updated)return NO;
    if(updated==original)return YES;
    NSString *directory=[[State stringByAppendingPathComponent:@"OriginalFilesystemMetadata"] stringByAppendingPathComponent:build];
    NSString *backup=[directory stringByAppendingPathComponent:@"msdos-Info.plist"];
    if(!Directory(directory,0700))return NO;
    if([[NSFileManager defaultManager] fileExistsAtPath:backup]){
        if(![Digest(backup) isEqual:Digest(path)])return NO;
    }else if(!Copy(path,backup,0600))return NO;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:updated format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    return data&&[data writeToFile:path options:NSDataWritingAtomic error:NULL]&&
        !chown([path fileSystemRepresentation],0,0)&&!chmod([path fileSystemRepresentation],0644)&&
        [[NSDictionary dictionaryWithContentsOfFile:path] isEqual:updated];
}
static BOOL CopySupport(NSString *from,NSString *to){
    NSArray *files=[[NSFileManager defaultManager] subpathsAtPath:from];
    if(!files||[files count]>128)return NO;
    for(id name in files){
        if(![name isKindOfClass:[NSString class]]||[name isAbsolutePath]||[[name pathComponents] containsObject:@".."]||[[name pathComponents] containsObject:@"."])return NO;
        NSString *source=[from stringByAppendingPathComponent:name],*destination=[to stringByAppendingPathComponent:name];
        struct stat st;if(lstat([source fileSystemRepresentation],&st))return NO;
        if(S_ISDIR(st.st_mode))continue;
        if(!S_ISREG(st.st_mode)||!Directory([destination stringByDeletingLastPathComponent],0755)||
           !Copy(source,destination,(st.st_mode&0111)?0755:0644))return NO;
    }
    return YES;
}
static NSData *Run(NSString *path,NSArray *arguments){
    NSTask *task=[[[NSTask alloc] init] autorelease];NSPipe *pipe=[NSPipe pipe];
    [task setLaunchPath:path];[task setArguments:arguments];[task setStandardOutput:pipe];
    [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];[task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
    @try{[task launch];NSData *data=[[pipe fileHandleForReading] readDataToEndOfFile];[task waitUntilExit];return [task terminationStatus]==0&&[data length]<1024*1024?data:nil;}
    @catch(NSException *exception){return nil;}
}
static BOOL BSDDeviceIdentifier(id disk){
    if(![disk isKindOfClass:[NSString class]]||[disk length]>63)return NO;
    if([disk rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"disk0123456789"] invertedSet]].location!=NSNotFound)return NO;
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^disk[0-9]+(?:s[0-9]+)*$" options:0 error:NULL];
    return [pattern numberOfMatchesInString:disk options:0 range:NSMakeRange(0,[disk length])]==1;
}
static NSDictionary *DiskInfo(NSString *identifier){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    if([identifier hasPrefix:@"disk"]||[identifier isEqual:@"/"]){
        // 10J3250 diskutil info raises an exception inserting a nil
        // OpticalDeviceType for this real ATAPI medium. Disk Arbitration
        // already has the four authoritative fields our handoff needs.
        DASessionRef session=DASessionCreate(NULL);if(!session)return nil;
        // The root volume uses the same native query as setup-status's
        // successful ownership proof, without cold launchd's diskutil child.
        DADiskRef disk=[identifier isEqual:@"/"]?
            DADiskCreateFromVolumePath(NULL,session,(CFURLRef)[NSURL fileURLWithPath:@"/"]):
            DADiskCreateFromBSDName(NULL,session,[identifier UTF8String]);
        NSDictionary *description=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
        NSMutableDictionary *info=description?[NSMutableDictionary dictionary]:nil;
        if(info){
            const char *bsd=DADiskGetBSDName(disk);
            if(bsd)[info setObject:[NSString stringWithUTF8String:bsd] forKey:@"DeviceIdentifier"];
            id name=[description objectForKey:(NSString *)kDADiskDescriptionVolumeNameKey];
            if([name isKindOfClass:[NSString class]])[info setObject:name forKey:@"VolumeName"];
            id path=[description objectForKey:(NSString *)kDADiskDescriptionVolumePathKey];
            if([path isKindOfClass:[NSURL class]])[info setObject:[path path] forKey:@"MountPoint"];
            CFTypeRef uuid=(CFTypeRef)[description objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
            if(uuid&&CFGetTypeID(uuid)==CFUUIDGetTypeID()){
                NSString *text=(NSString *)CFUUIDCreateString(NULL,(CFUUIDRef)uuid);
                if(text)[info setObject:text forKey:@"VolumeUUID"];[text release];
            }
        }
        [description release];if(disk)CFRelease(disk);CFRelease(session);return info;
    }
#endif
    NSData *data=Run(@"/usr/sbin/diskutil",[NSArray arrayWithObjects:@"info",@"-plist",identifier,nil]);
    id result=data?[NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL]:nil;
    return [result isKindOfClass:[NSDictionary class]]?result:nil;
}
static BOOL SetupPartition(id disk){
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^disk[0-9]+(?:s[0-9]+)+$" options:0 error:NULL];
    return [disk isKindOfClass:[NSString class]]&&[pattern numberOfMatchesInString:disk options:0 range:NSMakeRange(0,[disk length])]==1;
}
static void FindSetup(id object,NSMutableArray *results){
    if([object isKindOfClass:[NSDictionary class]]){
        if([[object objectForKey:@"VolumeName"] isEqual:@"VIM Setup"]){
            NSString *disk=[object objectForKey:@"DeviceIdentifier"];
            // Original HFS CDs expose nested slices, e.g. disk2s1s2, not just
            // disk2s2. Validate that actual structured BSD identifier as-is.
            if(SetupPartition(disk))[results addObject:disk];
        }
        for(id value in [object allValues])if([value isKindOfClass:[NSArray class]]||[value isKindOfClass:[NSDictionary class]])FindSetup(value,results);
    }else if([object isKindOfClass:[NSArray class]])for(id value in object)FindSetup(value,results);
}
static NSDictionary *SetupPolicy(void){
    NSData *list=Run(@"/usr/sbin/diskutil",[NSArray arrayWithObjects:@"list",@"-plist",nil]);
    id disks=list?[NSPropertyListSerialization propertyListWithData:list options:0 format:NULL error:NULL]:nil;
    NSMutableArray *matches=[NSMutableArray array];FindSetup(disks,matches);
    // Actual 10J3250 diskutil has only flat AllDisks/WholeDisks/VolumesFromDisks
    // arrays, not the later AllDisksAndPartitions tree. Query its structured
    // per-partition descriptions once during cold handoff. Keep the exact
    // label, unique-medium and target-UUID authorization checks unchanged.
    if(![matches count]&&[disks isKindOfClass:[NSDictionary class]]&&
       ![disks objectForKey:@"AllDisksAndPartitions"]&&[[disks objectForKey:@"AllDisks"] isKindOfClass:[NSArray class]])
        for(id disk in [disks objectForKey:@"AllDisks"])
            if(SetupPartition(disk))FindSetup(DiskInfo(disk),matches);
    if([matches count]!=1)return nil;
    NSString *disk=[matches objectAtIndex:0];NSDictionary *info=DiskInfo(disk);
    if(![[info objectForKey:@"MountPoint"] length]){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        // Native9J3050 accepts "mount disk...", not the later "mount
        // readOnly disk..." syntax (which fails in its setuid helper).
        // The setup medium is a read-only CD; verify the actual mount below.
        NSArray *arguments=[NSArray arrayWithObjects:@"mount",disk,nil];
#else
        NSArray *arguments=[NSArray arrayWithObjects:@"mount",@"readOnly",disk,nil];
#endif
        if(!Run(@"/usr/sbin/diskutil",arguments))return nil;
        info=DiskInfo(disk);
    }
    NSString *mount=[info objectForKey:@"MountPoint"];
    if(![mount hasPrefix:@"/Volumes/"])return nil;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    struct statfs filesystem;
    if(statfs([mount fileSystemRepresentation],&filesystem)||!(filesystem.f_flags&MNT_RDONLY))return nil;
#endif
    NSDictionary *policy=[NSDictionary dictionaryWithContentsOfFile:[mount stringByAppendingPathComponent:@"VirtualIntelMac.plist"]];
    if(![[policy objectForKey:@"FormatVersion"] isEqual:[NSNumber numberWithInt:1]]||
       ![[policy objectForKey:@"Kind"] isEqual:@"VirtualIntelMacLegacySetup"]||
       !UUID([policy objectForKey:@"MachineIdentifier"])||!UUID([policy objectForKey:@"VolumeUUID"])||
       ![[policy objectForKey:@"Guest"] isEqual:CurrentGuest()])return nil;
    return policy;
}
static BOOL StartRecoveryAgent(void){
    CFStringRef label=CFSTR("com.mac.virtualintelmac.opengl-recovery-agent");
    if(VIMOpenGLSystemJobLoaded(label))return YES;
    NSString *path=@"/var/run/com.mac.virtualintelmac.opengl-recovery-agent.plist";
    NSDictionary *job=[NSDictionary dictionaryWithObjectsAndKeys:(NSString *)label,@"Label",@"root",@"UserName",
        [NSArray arrayWithObjects:[MediaTools stringByAppendingPathComponent:@"opengl-guest-agent"],@"--device",@"/dev/cu.serial2",nil],@"ProgramArguments",
        [NSNumber numberWithBool:YES],@"RunAtLoad",
        [NSDictionary dictionaryWithObject:[NSNumber numberWithBool:NO] forKey:@"SuccessfulExit"],@"KeepAlive",
        [NSNumber numberWithInt:10],@"ThrottleInterval",
        @"/var/log/VirtualIntelMacRecoveryAgent.log",@"StandardErrorPath",nil];
    // launchd owns a separate process group. Never leave the control service
    // as a child of Apple's installer shell/Language Chooser process group.
    // Like the installed service, restart after an unexpected UART/setup
    // failure. A late serial node must not permanently strand this recovery
    // session. launchd throttles failures; a healthy agent blocks in read().
    return Write(job,path)&&Run(@"/bin/launchctl",[NSArray arrayWithObjects:@"load",path,nil])!=nil;
}
static BOOL CanResumeInstaller(NSString *root,NSString *state,NSDictionary *policy,NSString *machine,NSString *volume){
    // An explicit host retry may resume only our own interrupted, unattended
    // target. A SystemVersion file alone is not proof installation finished:
    // Apple's package copier publishes it before the last postflight runs.
    struct stat st;
    NSString *path=[state stringByAppendingPathComponent:@"Ownership.plist"];
    if(![[policy objectForKey:@"Enabled"] boolValue]||
       ![[policy objectForKey:@"MachineIdentifier"] isEqual:machine]||
       ![[policy objectForKey:@"VolumeUUID"] isEqual:volume]||
       lstat([state fileSystemRepresentation],&st)||!S_ISDIR(st.st_mode)||st.st_uid||(st.st_mode&0077)||
       lstat([path fileSystemRepresentation],&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||(st.st_mode&0077))return NO;
    NSDictionary *owner=[NSDictionary dictionaryWithContentsOfFile:path];
    if(![[owner objectForKey:@"MachineIdentifier"] isEqual:machine]||
       ![[owner objectForKey:@"VolumeUUID"] isEqual:volume]||
       ![[owner objectForKey:@"Guest"] isEqual:CurrentGuest()])return NO;
    NSFileManager *fm=[NSFileManager defaultManager];
    for(NSString *name in [NSArray arrayWithObjects:@"Account.plist",@"Graphics.plist",nil])
        if([fm fileExistsAtPath:[state stringByAppendingPathComponent:name]])return NO;
    if([fm fileExistsAtPath:[root stringByAppendingString:@"/private/var/db/.AppleSetupDone"]])return NO;
    NSString *users=[root stringByAppendingString:@"/private/var/db/dslocal/nodes/Default/users"];
    NSArray *records=[fm contentsOfDirectoryAtPath:users error:NULL];
    if(!records)return NO;
    for(NSString *name in records){
        if(![[name pathExtension] isEqual:@"plist"])continue;
        NSDictionary *record=[NSDictionary dictionaryWithContentsOfFile:[users stringByAppendingPathComponent:name]];
        NSArray *uids=[record objectForKey:@"uid"];
        if(![uids isKindOfClass:[NSArray class]]||![uids count])return NO;
        for(id value in uids){
            if(![value isKindOfClass:[NSString class]])return NO;
            NSScanner *scanner=[NSScanner scannerWithString:value];int uid=0;
            if(![scanner scanInt:&uid]||![scanner isAtEnd]||(uid>=500&&uid<60000))return NO;
        }
    }
    NSData *processes=Run(@"/bin/ps",[NSArray arrayWithObjects:@"-axww",@"-o",@"comm=",nil]);
    NSString *text=processes?[[[NSString alloc] initWithData:processes encoding:NSUTF8StringEncoding] autorelease]:nil;
    if(![text length])return NO;
    NSArray *workers=[NSArray arrayWithObjects:@"Mac OS X Installer",@"OS X Installer",@"macOS Installer",@"OSInstaller",@"runner",@"installer",nil];
    for(NSString *line in [text componentsSeparatedByString:@"\n"]){
        NSString *name=[[line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] lastPathComponent];
        if([workers containsObject:name])return NO;
    }
    return YES;
}
static int Recovery(BOOL startAgent,NSString *resumeMachine,NSString *resumeVolume){
    if([CurrentGuest() isEqual:@"lion"]){
        // 11G63's 4-GiB default allows 66,560 vnodes. The native installer
        // exhausts it while copying packages, then receipt lookup can recycle
        // a /private/var/folders RAM-disk vnode and deadlock in HFS metasync.
        // Use XNU 1699's own normal 16-GiB configuration ceiling. This raises
        // an allocation limit, not an upfront reservation; it is recovery-only
        // and disappears on reboot. No disk policy, kernel patch or timer.
        int current=0,minimum=263168;size_t size=sizeof(current);
        if(sysctlbyname("kern.maxvnodes",&current,&size,NULL,0)||size!=sizeof(current)||
           (current<minimum&&sysctlbyname("kern.maxvnodes",NULL,NULL,&minimum,sizeof(minimum))))
            return Error("the Lion installer filesystem budget could not be prepared");
        size=sizeof(current);
        if(sysctlbyname("kern.maxvnodes",&current,&size,NULL,0)||size!=sizeof(current)||current<minimum)
            return Error("the Lion installer filesystem budget could not be confirmed");
    }
    if(startAgent&&!StartRecoveryAgent())return Error("the recovery control service could not start");
    // The restore DVD runs rc.cdrom.local before LCA. Early Disk Arbitration
    // enumeration can omit the attached setup CD, leaving a permanent manual
    // language picker even though the same bootstrap succeeds once discovery
    // settles. Wait for the native disk queue, then retain the exact existing
    // unique-medium, read-only and target-UUID checks. A manual install with
    // no setup medium still falls through normally after discovery.
    if(!WaitForLeopardDisks())return Error("the recovery disk discovery service is unavailable");
    NSDictionary *policy=SetupPolicy();
    if(!policy&&resumeMachine)return Error("the original automatic setup medium is unavailable; the target was retained");
    if(!policy){puts("No automatic setup medium; normal Apple installer retained.");return 0;}
    NSString *uuid=[policy objectForKey:@"VolumeUUID"];NSDictionary *target=DiskInfo(uuid);
    if(![[target objectForKey:@"VolumeUUID"] isEqual:uuid])return Error("the target volume UUID does not match");
    if(![[target objectForKey:@"MountPoint"] length]){
        if(!Run(@"/usr/sbin/diskutil",[NSArray arrayWithObjects:@"mount",uuid,nil]))return Error("the new target could not be mounted");
        target=DiskInfo(uuid);
    }
    NSString *root=[target objectForKey:@"MountPoint"];
    if(![root hasPrefix:@"/Volumes/"]||![[target objectForKey:@"VolumeUUID"] isEqual:uuid])return Error("unsafe installation target");
    BOOL resume=resumeMachine!=nil;
    NSString *state=[root stringByAppendingString:[CurrentGuest() isEqual:@"highsierra"]?HighSierraInstallerState:State];
    if(resume&&(![[target objectForKey:@"VolumeUUID"] isEqual:resumeVolume]||
       !CanResumeInstaller(root,state,policy,resumeMachine,resumeVolume)))return Error("this target is not an inactive, owned, unfinished automatic installation; no existing system was reinstalled");
    if(!resume&&[[NSFileManager defaultManager] fileExistsAtPath:[root stringByAppendingString:@"/System/Library/CoreServices/SystemVersion.plist"]]){
        // A completed/partly completed OS must never be reinstalled simply
        // because firmware selected the still-attached recovery medium again.
        puts("An OS is already present; no automatic reinstall requested.");return 0;
    }
    // Native17 setupMigrationWithTarget rejects a non-system target which
    // already contains /private/var, /usr, /bin or /sbin. Library alone does
    // not mark it as a partial OS. Defer only our journal's normal /private
    // location until first installed boot; keep native validation intact.
    NSString *tools=[root stringByAppendingString:Tools];
    if(!Directory(state,0700)||!Directory([tools stringByDeletingLastPathComponent],0755)||
       !Directory(tools,0755)||!Directory([root stringByAppendingString:@"/Library/LaunchDaemons"],0755)||
       !Directory([root stringByAppendingString:@"/Library/LaunchAgents"],0755))return Error("creating target bootstrap folders");
    NSString *ownershipPath=[state stringByAppendingPathComponent:@"Ownership.plist"];
    NSDictionary *previous=[NSDictionary dictionaryWithContentsOfFile:ownershipPath];
    if(previous&&(![[previous objectForKey:@"MachineIdentifier"] isEqual:[policy objectForKey:@"MachineIdentifier"]]||
       ![[previous objectForKey:@"VolumeUUID"] isEqual:uuid]))return Error("another setup owns this target");
    if(!CopySupport(MediaTools,tools))return Error("verifying and copying compiled guest support");
    NSString *daemon=[root stringByAppendingString:@"/Library/LaunchDaemons/com.mac.virtualintelmac.opengl-agent.plist"];
    if(!Copy([MediaTools stringByAppendingPathComponent:@"com.mac.virtualintelmac.opengl-agent.plist"],daemon,0644)||
       !Copy([MediaTools stringByAppendingPathComponent:@"com.virtualintelmac.opengl-display.plist"],
             [root stringByAppendingString:@"/Library/LaunchAgents/com.virtualintelmac.opengl-display.plist"],0644)||
       !Write(policy,ownershipPath))return Error("publishing root-owned bootstrap authorization");
    if([[policy objectForKey:@"Enabled"] boolValue]){
        // A non-admin host can prepare the image without manufacturing root
        // catalog owners. Stage the verified adapter into recovery's own RAM
        // as guest root instead; the exec wrapper refuses an unowned library.
        NSString *adapter=@"opengl-installer-automation.dylib";
        if(!Directory(@"/var/run/VirtualIntelMac",0755)||
           !Copy([MediaTools stringByAppendingPathComponent:adapter],[@"/var/run/VirtualIntelMac" stringByAppendingPathComponent:adapter],0755))return Error("staging the root-owned native installer adapter");
        NSMutableDictionary *configuration=[NSMutableDictionary dictionaryWithObjectsAndKeys:
            @"/System/Installation/Packages/OSInstall.mpkg",@"Package",@"automated",@"InstallType",@"English",@"Language",
            uuid,@"TargetUUID",[NSNumber numberWithBool:NO],@"ShouldErase",[NSNumber numberWithBool:NO],@"ShouldArchiveAndInstall",
            // The original LCA-supervised launch quits back to its native
            // supervisor. An explicit resumed launch has no such parent;
            // let OSInstaller's own termination path restart after its real
            // success pane instead of leaving completed Recovery on screen.
            [NSNumber numberWithBool:!resume],@"QuitWhenDone",nil];
        if([CurrentGuest() isEqual:@"highsierra"])[configuration setObject:@"/InstallInfo.plist" forKey:@"Package"];
        // Actual 10J3250 _automationTargetVolumeHandling returns success
        // immediately when installVolumePath is nil, BEFORE reading its UUID.
        // Supply the already mounted/UUID-verified path, retaining the UUID
        // guard and Apple's own checks. Later installers keep their interface.
        if([CurrentGuest() isEqual:@"leopard"]||[CurrentGuest() isEqual:@"snowleopard"])[configuration setObject:root forKey:@"Target"];
        // Native17G66 OSInstallOptions::initWithFile: reads this boolean;
        // absence selects APFSMachineSupport's default. Request Apple's
        // native conversion explicitly: High Sierra must finish on APFS,
        // regardless of the virtual disk's rotational/SSD classification.
        if([CurrentGuest() isEqual:@"highsierra"])[configuration setObject:[NSNumber numberWithBool:YES] forKey:@"ConvertToAPFS"];
        if(!PrepareHighSierraProduct(root,uuid)||
           !Write(policy,@"/var/tmp/VirtualIntelMac-InstallerAuthorization.plist")||
           !Write(configuration,@"/var/tmp/VirtualIntelMac-minstallconfig.plist"))return Error("preparing native OSInstaller automation");
    }
    if(resume){
        // Re-enter exactly the original native installer wrapper. No erasure,
        // synthetic clicks, account replacement, or global DYLD environment.
        NSTask *task=[[[NSTask alloc] init] autorelease];
        [task setLaunchPath:[MediaTools stringByAppendingPathComponent:@"opengl-installer-launch"]];
        [task setArguments:[NSArray arrayWithObjects:[NSString stringWithUTF8String:VIMOpenGLRecoveryInstallerForGuest([CurrentGuest() UTF8String])],
            @"-f",@"/System/Installation/Packages/Extras/minstallconfig.xml",nil]];
        [task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
        [task setStandardOutput:[NSFileHandle fileHandleWithNullDevice]];
        [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];
        @try{[task launch];}@catch(NSException *exception){return Error("the native installer could not resume; the target was retained");}
        puts("The owned interrupted installation was resumed without erasing its target.");return 0;
    }
    puts("The new target is authorized and native installer setup is prepared.");return 0;
}
static int Installed(void){
    NSString *ownershipPath=[State stringByAppendingPathComponent:@"Ownership.plist"];
    NSDictionary *ownership=[NSDictionary dictionaryWithContentsOfFile:ownershipPath];
    if(!ownership&&[CurrentGuest() isEqual:@"highsierra"]){
        NSString *prepared=[HighSierraInstallerState stringByAppendingPathComponent:@"Ownership.plist"];
        struct stat st;
        if(!lstat([prepared fileSystemRepresentation],&st)&&S_ISREG(st.st_mode)&&!st.st_uid&&st.st_nlink==1&&!(st.st_mode&0077)){
            NSDictionary *candidate=[NSDictionary dictionaryWithContentsOfFile:prepared],*disk=DiskInfo(@"/");
            if(UUID([candidate objectForKey:@"MachineIdentifier"])&&[[candidate objectForKey:@"Guest"] isEqual:@"highsierra"]&&
               [[candidate objectForKey:@"VolumeUUID"] isEqual:[disk objectForKey:@"VolumeUUID"]]){
                if(!Directory(State,0700)||!Write(candidate,ownershipPath))return Error("adopting the installed High Sierra setup journal");
                ownership=candidate;
                // Only retire the private staging copy after the normal
                // root-owned journal has been atomically published.
                if(unlink([prepared fileSystemRepresentation]))return Error("retiring the prepared setup journal");
                rmdir([HighSierraInstallerState fileSystemRepresentation]);
            }
        }
    }
    // The installed launchd job can also run before Darwin 9 publishes the
    // root volume's UUID. This is a cold readiness dependency, not authorization
    // to install against a different disk. Keep the exact match below.
    if(!WaitForLeopardDisks())return Error("the installed disk discovery service is unavailable");
    NSDictionary *root=DiskInfo(@"/");NSString *volume=[ownership objectForKey:@"VolumeUUID"];
    if(!UUID(volume)||![[root objectForKey:@"VolumeUUID"] isEqual:volume]||!UUID([ownership objectForKey:@"MachineIdentifier"]))return Error("the installed volume has no matching bootstrap authorization");
    // Root-only transaction state is separate from user-readable executables.
    // Repair only the exact root-owned 0700 ancestors an older bootstrap could
    // have created under umask 077. Never recursively chmod the installed OS,
    // and never relax the root-only transaction/credential directories.
    for(NSString *path in [NSArray arrayWithObjects:@"/Library",@"/Library/Application Support",@"/private",@"/private/var",@"/private/var/db",nil]){
        struct stat st;if(lstat([path fileSystemRepresentation],&st)||!S_ISDIR(st.st_mode)||st.st_uid!=0)return Error("guest bootstrap parent ownership");
        if((st.st_mode&07777)==0700&&chmod([path fileSystemRepresentation],0755))return Error("guest bootstrap parent traversal");
    }
    if(!Directory([Tools stringByDeletingLastPathComponent],0755)||!Directory(Tools,0755))return Error("guest support folder permissions");
    BOOL displayPolicyChanged=NO;
    if(!PrepareLionDisplayPolicy(&displayPolicyChanged))return Error("preparing native Lion display resolution policy");
    NSDictionary *system=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *build=[system objectForKey:@"ProductBuildVersion"];
    if(![build isKindOfClass:[NSString class]]||![build length]||[build length]>40||
       [build rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound)return Error("unknown installed OS build");
    if(!RegisterEFIMedia(build))return Error("registering the native non-mounting EFI filesystem probe");
    NSMutableDictionary *files=[NSMutableDictionary dictionary];
    for(NSString *name in [NSArray arrayWithObjects:@"GLEngine",@"libVIMPassthroughOpenGL.dylib",@"VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA",@"VMsvga2GA.plugin/Contents/Info.plist",nil])
    {
        NSString *digest=Digest([Tools stringByAppendingPathComponent:name]);
        if(!digest)return Error("a guest graphics payload file is missing");
        [files setObject:digest forKey:name];
    }
    NSString *bundle=@"/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle";
    NSString *engine=[bundle stringByAppendingPathComponent:@"GLEngine"],*backend=[bundle stringByAppendingPathComponent:@"libVIMPassthroughOpenGL.dylib"];
    NSString *backupDirectory=[[State stringByAppendingPathComponent:@"OriginalGraphics"] stringByAppendingPathComponent:build];
    NSString *backup=[backupDirectory stringByAppendingPathComponent:@"GLEngine"],*originalBuild=build;
    NSString *current=Digest(engine);BOOL changed=![current isEqual:[files objectForKey:@"GLEngine"]];
    if(!current)return Error("the original GLEngine is missing");
    NSDictionary *prior=[NSDictionary dictionaryWithContentsOfFile:[State stringByAppendingPathComponent:@"Graphics.plist"]];
    BOOL priorEngine=[[prior objectForKey:@"EngineSHA256"] isEqual:current]&&
        [[prior objectForKey:@"MachineIdentifier"] isEqual:[ownership objectForKey:@"MachineIdentifier"]]&&
        [[prior objectForKey:@"VolumeUUID"] isEqual:volume];
    // A developer replacement, or interruption after publishing the engine
    // but before its receipt, need not match the last installed pair. The
    // original recovery source is independent of that pair: an owned receipt
    // for this exact volume/build plus its unchanged backup is sufficient to
    // reinstall support. Never replace the original with our modified engine.
    BOOL priorRecovery=[[prior objectForKey:@"MachineIdentifier"] isEqual:[ownership objectForKey:@"MachineIdentifier"]]&&
        [[prior objectForKey:@"VolumeUUID"] isEqual:volume]&&
        [[prior objectForKey:@"OriginalSourceBuild"] isEqual:build]&&
        [[prior objectForKey:@"OriginalSHA256"] isEqual:Digest(backup)];
    if(changed&&!priorEngine&&!priorRecovery){
        if(!Directory(backupDirectory,0700))return Error("creating the original graphics backup");
        if([[NSFileManager defaultManager] fileExistsAtPath:backup]){
            if(![Digest(backup) isEqual:current])return Error("an unexpected graphics implementation occupies this OS build");
        }else if(!Copy(engine,backup,0600))return Error("saving the actual original Apple GLEngine");
    }
    if((!changed||priorEngine)&&!Digest(backup)) {
        // Some security updates change the build but not OpenGL. Retain the
        // already verified original source build instead of inventing a new
        // stock image or treating our own engine as the new backup.
        NSString *saved=[prior objectForKey:@"OriginalSourceBuild"];
        if(![saved isKindOfClass:[NSString class]]||![saved length]||[saved length]>40||
           [saved rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location!=NSNotFound||
           ![[prior objectForKey:@"MachineIdentifier"] isEqual:[ownership objectForKey:@"MachineIdentifier"]]||
           ![[prior objectForKey:@"VolumeUUID"] isEqual:volume])return Error("the previous original graphics identity is unavailable");
        backup=[[[State stringByAppendingPathComponent:@"OriginalGraphics"] stringByAppendingPathComponent:saved] stringByAppendingPathComponent:@"GLEngine"];
        if(![[prior objectForKey:@"OriginalSHA256"] isEqual:Digest(backup)])return Error("the previous original graphics copy changed");
        originalBuild=saved;
    }
    if(!Digest(backup))return Error("the original graphics recovery copy is unavailable");
    NSString *stock=@"/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngineStock.bundle";
    NSString *ga=@"/System/Library/Extensions/VMsvga2GA.plugin/Contents";
    if(!Directory(stock,0755)||!Directory([ga stringByDeletingLastPathComponent],0755)||
       !Directory(ga,0755)||!Directory([ga stringByAppendingPathComponent:@"MacOS"],0755))return Error("preparing the software-recovery and 2D driver bundles");
    // Apple's original engine remains installed for real software recovery.
    // Main OpenGL and all of its i386/x64 slices remain Apple's own files.
    NSString *stockFile=[stock stringByAppendingPathComponent:@"GLEngineStock"];
    if(![Digest(stockFile) isEqual:Digest(backup)]&&!Copy(backup,stockFile,0755))return Error("installing the original recovery engine");
    for(NSString *leaf in [NSArray arrayWithObjects:@"Info.plist",@"MacOS/VMsvga2GA",nil]) {
        NSString *source=[Tools stringByAppendingPathComponent:[@"VMsvga2GA.plugin/Contents/" stringByAppendingString:leaf]],*destination=[ga stringByAppendingPathComponent:leaf];
        if(![Digest(source) isEqual:Digest(destination)]&&!Copy(source,destination,[leaf hasSuffix:@".plist"]?0644:0755))return Error("installing the compiled 2D plugin");
    }
    BOOL backendChanged=![Digest(backend) isEqual:[files objectForKey:@"libVIMPassthroughOpenGL.dylib"]];
    // Dependency first, public engine last. An interrupted transaction can be
    // resumed against the saved original; it never backs up our own GLI.
    if((backendChanged&&!Copy([Tools stringByAppendingPathComponent:@"libVIMPassthroughOpenGL.dylib"],backend,0755))||
       (changed&&!Copy([Tools stringByAppendingPathComponent:@"GLEngine"],engine,0755)))return Error("publishing the PassthroughOpenGL engine");
    if(![Digest(engine) isEqual:[files objectForKey:@"GLEngine"]]||![Digest(backend) isEqual:[files objectForKey:@"libVIMPassthroughOpenGL.dylib"]])return Error("graphics read-back verification");
    NSString *boot=VIMOpenGLBootSessionUUID(NULL);
    BOOL pendingSameBoot=[[prior objectForKey:@"ChangedThisBoot"] boolValue]&&boot&&
        [[prior objectForKey:@"InstalledBootSessionUUID"] isEqual:boot]&&
        [[prior objectForKey:@"EngineSHA256"] isEqual:[files objectForKey:@"GLEngine"]]&&
        [[prior objectForKey:@"BackendSHA256"] isEqual:[files objectForKey:@"libVIMPassthroughOpenGL.dylib"]]&&
        [[prior objectForKey:@"ProductBuildVersion"] isEqual:build];
    NSMutableDictionary *receipt=[NSMutableDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithInt:1],@"FormatVersion",volume,@"VolumeUUID",
        [ownership objectForKey:@"MachineIdentifier"],@"MachineIdentifier",build,@"ProductBuildVersion",@"installed",@"Phase",
        [files objectForKey:@"GLEngine"],@"EngineSHA256",[files objectForKey:@"libVIMPassthroughOpenGL.dylib"],@"BackendSHA256",
        originalBuild,@"OriginalSourceBuild",Digest(backup),@"OriginalSHA256",
        [NSNumber numberWithBool:changed||backendChanged||displayPolicyChanged||pendingSameBoot],@"ChangedThisBoot",nil];
    // Live maintenance has a current kernel/control epoch. An early startup
    // before the old-OS agent exists can lack one; the host's acknowledged
    // exact-pair restart journal fences those older/unbound receipts.
    if(boot)[receipt setObject:boot forKey:@"InstalledBootSessionUUID"];
    if(!Write(receipt,[State stringByAppendingPathComponent:@"Graphics.plist"]))return Error("saving graphics installation evidence");
    return 0;
}
int main(int argc,char **argv){@autoreleasepool{
    BOOL agent=argc==2&&!strcmp(argv[1],"--agent");
    BOOL earlyRecovery=argc==2&&!strcmp(argv[1],"--start-recovery-agent");
    BOOL refresh=argc==3&&!strcmp(argv[1],"--refresh-support");
    BOOL resume=argc==4&&!strcmp(argv[1],"--resume-recovery");
    BOOL diskInfo=argc==3&&!strcmp(argv[1],"--disk-info");
    if((argc>2&&!refresh&&!resume&&!diskInfo)||(argc==2&&!agent&&!earlyRecovery&&strcmp(argv[1],"--recovery")&&strcmp(argv[1],"--prepare-recovery")&&strcmp(argv[1],"--install-support")))return 64;
    if(geteuid()!=0||!CurrentGuest())return 77;
    if(!VIMOpenGLGuestMachine())return 77;
    if(diskInfo){
        // Read-only cold lookup. Snow's diskutil crashes on optical media;
        // reuse native Disk Arbitration without entering setup/power policy.
        NSString *identifier=[NSString stringWithUTF8String:argv[2]];
        if(!BSDDeviceIdentifier(identifier))return 64;
        NSDictionary *info=DiskInfo(identifier);
        if(![[info objectForKey:@"DeviceIdentifier"] isEqual:identifier])return Error("the requested guest disk is unavailable");
        NSData *data=[NSPropertyListSerialization dataWithPropertyList:info format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
        if(!data)return Error("the guest disk information could not be encoded");
        return fwrite([data bytes],1,[data length],stdout)==[data length]?0:Error("the guest disk information could not be returned");
    }
    umask(0077);
    if(refresh){
        // Maintenance only: atomically publish our compiled utilities on an
        // already authorized installed VM. Never truncate a live agent's
        // mapped executable, mutate OS packages or replace an account.
        NSString *path=[State stringByAppendingPathComponent:@"Ownership.plist"];
        struct stat st;struct statfs source;
        if(lstat([State fileSystemRepresentation],&st)||!S_ISDIR(st.st_mode)||st.st_uid||(st.st_mode&0077)||
           lstat([path fileSystemRepresentation],&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||(st.st_mode&0077))return 77;
        NSDictionary *owner=[NSDictionary dictionaryWithContentsOfFile:path],*disk=DiskInfo(@"/");
        NSString *from=[NSString stringWithUTF8String:argv[2]];
        if(!UUID([owner objectForKey:@"MachineIdentifier"])||![[owner objectForKey:@"Guest"] isEqual:CurrentGuest()]||
           ![[owner objectForKey:@"VolumeUUID"] isEqual:[disk objectForKey:@"VolumeUUID"]]||
           ![from hasPrefix:@"/Volumes/"]||statfs([from fileSystemRepresentation],&source)||!(source.f_flags&MNT_RDONLY))return 77;
        return CopySupport(from,Tools)?0:Error("atomically refreshing matching compiled guest support");
    }
    BOOL powerPolicy=PrepareLeopardPowerPolicy();
    if(!powerPolicy)Error("the native system-sleep policy could not be confirmed");
    BOOL recovery=[[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:VIMOpenGLRecoveryInstallerForGuest([CurrentGuest() UTF8String])]];
    // Native recovery's keychain/Launch Services startup may wait before its
    // later imaging hook. Expose the same cold-control service beforehand;
    // target provisioning still belongs exclusively to rc.imaging below.
    if(earlyRecovery){
        if(!recovery||!StartRecoveryAgent())return 77;
        // Actual16G29/17G66 no longer invoke Extras/rc.imaging. Their rc.install
        // calls this native pre-UI hook before checking minstallconfig.
        // Prepare the exact authorized target here, once, rather than
        // waiting for a desktop, polling or injecting the signed installer.
        if([CurrentGuest() isEqual:@"sierra"]||[CurrentGuest() isEqual:@"highsierra"])return Recovery(NO,nil,nil);
        return 0;
    }
    if(resume&&(!recovery||!UUID([NSString stringWithUTF8String:argv[2]])||!UUID([NSString stringWithUTF8String:argv[3]])))return 77;
    if(recovery)return Recovery(resume?NO:(argc==1||strcmp(argv[1],"--prepare-recovery")),resume?[NSString stringWithUTF8String:argv[2]]:nil,resume?[NSString stringWithUTF8String:argv[3]]:nil);
    int result=Installed();
    if(!powerPolicy&&!result)result=1;
    if(agent){
        // Even a definite graphics/setup failure must leave the cold control
        // service available for diagnosis and safe repair, rather than looping
        // launchd or preventing a user from reaching the original desktop.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        // Darwin9 __mac_execve returns ENOTSUP for a multithreaded caller.
        // Disk Arbitration/Foundation above can already own helper threads.
        // Native posix_spawn creates the agent in a new task; waitpid keeps
        // launchd's existing exit/restart policy and single UART owner, with
        // no detached orphan, polling reaper or dependency on a logged-in user.
        const char *path=[[Tools stringByAppendingPathComponent:@"opengl-guest-agent"] fileSystemRepresentation];
        char *arguments[]={(char *)path,"--device","/dev/cu.serial2",NULL};pid_t child=0;
        int issue=posix_spawn(&child,path,NULL,NULL,arguments,*_NSGetEnviron());
        if(issue){errno=issue;perror("Virtual Intel Mac: spawning installed control service");return Error("starting the installed control service");}
        int status=0;pid_t waited;
        do{waited=waitpid(child,&status,0);}while(waited<0&&errno==EINTR);
        if(waited!=child){perror("Virtual Intel Mac: waiting for installed control service");return 1;}
        return WIFEXITED(status)?WEXITSTATUS(status):WIFSIGNALED(status)?128+WTERMSIG(status):1;
#else
        execl([[Tools stringByAppendingPathComponent:@"opengl-guest-agent"] fileSystemRepresentation],"opengl-guest-agent","--device","/dev/cu.serial2",NULL);
        perror("Virtual Intel Mac: executing installed control service");
        return Error("starting the installed control service");
#endif
    }
    return result;
}}
