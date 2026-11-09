#import "VIMInstallerAssembly.h"
#include "../tools/opengl-installer-paths.h"
#import <signal.h>
#import <fcntl.h>
#import <unistd.h>
#import <sys/stat.h>
#import <errno.h>
#import <math.h>
#import "VIMLibraryLocation.h"
#import "VIMInstallerSource.h"
#import "VIMInstallerCatalog.h"
#import "VIMToolPropertyList.h"
#import "VIMTigerRestoreProfile.h"
#import "VIMTigerInstallerSupport.h"
#include "VIMHFSInstallerDisk.h"
#include <sys/file.h>
#import <CommonCrypto/CommonDigest.h>
#import <Security/Security.h>

static BOOL AssemblyError(NSError **error, NSString *message) {
    if (error) *error=[NSError errorWithDomain:@"VirtualIntelMac.InstallerAssembly" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
static BOOL NativeAutomatedRecovery(NSString *guest) {
    // Genuine14F27,15G31,16G29 and17G66 rc.install launch Language Chooser with
    // TARGET_APP and -f minstall configuration. Keep the signed installer.
    return [@[@"yosemite",@"elcapitan",@"sierra",@"highsierra"] containsObject:guest];
}
static BOOL SerialRecovery(NSString *guest) {
    return [@[@"mojave",@"catalina"] containsObject:guest];
}
static NSInteger OpenGLBootstrapMediaVersion(NSString *guest) {
    // Earlier Leopard images can fall through to the manual language picker
    // before Disk Arbitration publishes the already attached setup medium.
    // Sierra's updated helpers recover a lost native first-login handoff.
    // Lion's installer RAM disks now journal native HFS+ catalog writes.
    return [@[@"leopard",@"lion",@"sierra"] containsObject:guest]?5:4;
}
static BOOL RegularFile(NSURL *URL) {
    NSNumber *regular=nil;
    return URL.isFileURL && [URL getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil] && regular.boolValue;
}
static BOOL NonemptyFile(NSURL *URL) {
    NSNumber *size=nil;
    return RegularFile(URL)&&[URL getResourceValue:&size forKey:NSURLFileSizeKey error:nil]&&size.unsignedLongLongValue>0;
}
static BOOL FileHasApplePartitionMap(int fd) {
    // Old BaseSystem images use a driver-descriptor block followed by APM.
    // Its block size can be 2048, not the 512-byte sectors used by GPT.
    uint8_t header[4],partition[2];
    BOOL found=pread(fd,header,sizeof(header),0)==sizeof(header)&&header[0]=='E'&&header[1]=='R';
    unsigned block=found?((unsigned)header[2]<<8)|header[3]:0;
    found=found&&block>=512&&block<=65536&&!(block&(block-1))&&
        pread(fd,partition,sizeof(partition),block)==sizeof(partition)&&partition[0]=='P'&&partition[1]=='M';
    return found;
}
static BOOL RawApplePartitionMap(NSURL *image) {
    int fd=open(image.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return NO;
    BOOL found=FileHasApplePartitionMap(fd);close(fd);return found;
}
// Only recognize the disk containers we publish. A matching receipt/length
// cannot detect a damaged primary header (observed on generated Catalina
// media). This reads a few boot sectors, not a whole-image hash or a seal on
// replacement user installers; those still follow normal installer detection.
static BOOL PreparedImageHasBootBlocks(NSURL *URL) {
    int fd=open(URL.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return NO;
    unsigned char blocks[1536]={0};ssize_t count;
    do{count=pread(fd,blocks,sizeof(blocks),0);}while(count<0&&errno==EINTR);
    BOOL recognizable=count==sizeof(blocks)&&
        (!memcmp(blocks+512,"EFI PART",8)||
         (!memcmp(blocks,"ER",2)&&FileHasApplePartitionMap(fd))||
         !memcmp(blocks+1024,"H+",2)||!memcmp(blocks+1024,"HX",2)||!memcmp(blocks+1024,"BD",2));
    if(!recognizable) {
        unsigned char descriptor[6]={0};
        do{count=pread(fd,descriptor,sizeof(descriptor),32768);}while(count<0&&errno==EINTR);
        recognizable=count==sizeof(descriptor)&&
            (!memcmp(descriptor+1,"CD001",5)||!memcmp(descriptor+1,"BEA01",5));
    }
    close(fd);return recognizable;
}
// Only for newly generated output, never an original installer or VM disk.
// A successful image-tool exit and mounted readback can both see cached data.
// Commit the output before retiring its working files, and use a fresh POSIX
// size rather than an NSURL resource-value cache for the completion receipt.
static NSNumber *FlushGeneratedImage(NSURL *URL,NSError **error) {
    int fd=open(URL.fileSystemRepresentation,O_RDWR|O_NOFOLLOW|O_CLOEXEC);
    struct stat before,after,path;
    BOOL valid=fd>=0&&!fstat(fd,&before)&&S_ISREG(before.st_mode)&&before.st_nlink==1&&before.st_size>0;
    int result=-1;
    if(valid){do{result=fsync(fd);}while(result<0&&errno==EINTR);}
    if(valid&&result==0) {
        do{result=fcntl(fd,F_FULLFSYNC);}while(result<0&&errno==EINTR);
        // Some external filesystems do not implement the stronger drive-cache
        // operation. Their successful fsync is still required, not ignored.
        if(result<0&&(errno==EINVAL||errno==ENOTSUP))result=0;
    }
    valid=valid&&result==0&&!fstat(fd,&after)&&after.st_size==before.st_size&&
        !lstat(URL.fileSystemRepresentation,&path)&&S_ISREG(path.st_mode)&&
        path.st_dev==after.st_dev&&path.st_ino==after.st_ino&&path.st_size==after.st_size;
    if(fd>=0&&close(fd))valid=NO;
    if(!valid){AssemblyError(error,@"The installer image couldn’t be saved reliably. Its working files were retained. Check that the library drive is connected, then try preparing the installer again.");return nil;}
    return @(after.st_size);
}
static BOOL VerifyOpenGLSupport(NSURL *directory,NSString *guest,NSError **error) {
    NSArray *files=[NSFileManager.defaultManager subpathsAtPath:directory.path];
    for(NSString *name in @[@"opengl-guest-agent",@"opengl-install-bootstrap",@"opengl-create-account",@"guest-ssh",@"guest-host-connection"])
        if(!NonemptyFile([directory URLByAppendingPathComponent:name]))return AssemblyError(error,@"This app is missing its OS X guest support. Rebuild or reinstall the complete app.");
    for(NSString *name in files) {
        NSURL *file=[directory URLByAppendingPathComponent:name];struct stat st;
        if(!lstat(file.fileSystemRepresentation,&st)&&S_ISDIR(st.st_mode))continue;
        if(![file.URLByResolvingSymlinksInPath.path hasPrefix:[directory.URLByResolvingSymlinksInPath.path stringByAppendingString:@"/"]]||
           lstat(file.fileSystemRepresentation,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>64*1024*1024)
            return AssemblyError(error,@"A bundled guest support file is missing or invalid.");
    }
    return YES;
}
static BOOL InstallerPackageImage(NSURL *URL) {
    int fd=open(URL.fileSystemRepresentation,O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    if(fd<0)return NO;
    char magic[4];ssize_t count=pread(fd,magic,sizeof(magic),0);close(fd);
    return count==sizeof(magic)&&!memcmp(magic,"xar!",sizeof(magic));
}
static NSDictionary<NSString *,NSString *> *InstallerIdentifiers(void) {
    // These identify the OS being installed, unlike CFBundleShortVersionString
    // (the InstallAssistant app version). Big Sur includes "macOS" in its ID.
    return @{@"lion":@"com.apple.InstallAssistant.Lion",@"mountainlion":@"com.apple.InstallAssistant.MountainLion",@"mavericks":@"com.apple.InstallAssistant.Mavericks",
             @"yosemite":@"com.apple.InstallAssistant.Yosemite",@"elcapitan":@"com.apple.InstallAssistant.ElCapitan",
             @"sierra":@"com.apple.InstallAssistant.Sierra",@"highsierra":@"com.apple.InstallAssistant.HighSierra",@"mojave":@"com.apple.InstallAssistant.Mojave",
             @"catalina":@"com.apple.InstallAssistant.Catalina",
             @"bigsur":@"com.apple.InstallAssistant.macOSBigSur"};
}
static BOOL InstallerVersionMatches(id version,NSString *guest) {
    if(![version isKindOfClass:NSString.class])return NO;
    NSString *family=@{@"tiger":@"10.4",@"leopard":@"10.5",@"snowleopard":@"10.6",@"lion":@"10.7",@"mountainlion":@"10.8",@"mavericks":@"10.9",@"yosemite":@"10.10",@"elcapitan":@"10.11",@"sierra":@"10.12",@"highsierra":@"10.13",@"mojave":@"10.14",@"catalina":@"10.15",@"bigsur":@"11"}[guest];
    if(!family)return NO;
    NSArray *components=[version componentsSeparatedByString:@"."];
    for(NSString *component in components)
        if(!component.length||[component rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789"] invertedSet]].location!=NSNotFound)return NO;
    if([guest isEqual:@"bigsur"]&&([version isEqual:@"10.16"]||[version hasPrefix:@"10.16."]))return YES;
    return [version isEqual:family]||[version hasPrefix:[family stringByAppendingString:@"."]];
}
NSString *const VIMInstallerReleaseErrorDomain=@"VirtualIntelMac.InstallerRelease";
NSString *const VIMSupportedReleaseURLKey=@"VIMSupportedReleaseURL";
// Restore DVDs for Tiger through Snow Leopard are qualified per dot release.
// This checks the install image only: an installed guest may still apply
// Apple's later updates. Tiger's install-time automation (setup policy and
// preboot graphics staging) recognizes only the 8R4088 disc; any other disc
// would silently skip it. The catalog keeps Leopard/Snow in step with the
// image this app downloads.
static BOOL ClassicReleaseSupported(NSString *guest,NSDictionary *version,NSError **error) {
    NSDictionary *entry=nil;
    for(NSDictionary *candidate in [VIMInstallerCatalog currentCatalogWithError:NULL].supportedInstallers)
        if([candidate[@"Guest"] isEqual:guest]){entry=candidate;break;}
    NSString *supported=[entry[@"Version"] isKindOfClass:NSString.class]?entry[@"Version"]:
        @{@"tiger":@"10.4.10",@"leopard":@"10.5.7",@"snowleopard":@"10.6.7"}[guest];
    NSString *build=[guest isEqual:@"tiger"]?@"8R4088":nil;
    NSString *found=[version[@"ProductVersion"] isKindOfClass:NSString.class]?version[@"ProductVersion"]:nil;
    NSString *foundBuild=[version[@"ProductBuildVersion"] isKindOfClass:NSString.class]?version[@"ProductBuildVersion"]:nil;
    if(!supported||([found isEqual:supported]&&(!build||[foundBuild isEqual:build])))return YES;
    NSString *name=@{@"tiger":@"Mac OS X Tiger",@"leopard":@"Mac OS X Leopard",@"snowleopard":@"Mac OS X Snow Leopard"}[guest];
    NSString *required=build?[NSString stringWithFormat:@"%@ (%@)",supported,build]:supported;
    NSString *chosen=found.length?(foundBuild.length?[NSString stringWithFormat:@"%@ (%@)",found,foundBuild]:found):@"an unknown version";
    // Link the catalog's release page for the supported image, else the
    // catalog's own image URL. No location is built into the app.
    NSURL *download=[entry[@"ReleaseURL"] isKindOfClass:NSString.class]?[NSURL URLWithString:entry[@"ReleaseURL"]]:nil;
    for(NSDictionary *file in entry[@"Files"])if(!download&&[file[@"Role"] isEqual:@"diskimage"])download=[NSURL URLWithString:file[@"URL"]];
    NSMutableDictionary *info=[@{NSLocalizedDescriptionKey:[NSString stringWithFormat:@"This %@ disc isn’t supported",name],
        NSLocalizedRecoverySuggestionErrorKey:[NSString stringWithFormat:
            @"This image is %@ %@. Virtual Intel Mac installs %@ only from the %@ image.\n\nDownload the supported image, or choose %@ without an installer and Virtual Intel Mac downloads it for you.",
            name,chosen,name,required,name]} mutableCopy];
    if(download)info[VIMSupportedReleaseURLKey]=download;
    info[@"Guest"]=guest;
    if(error)*error=[NSError errorWithDomain:VIMInstallerReleaseErrorDomain code:1 userInfo:info];
    return NO;
}
static BOOL UnlinkedDirectory(NSURL *URL) {
    struct stat st;return !lstat(URL.fileSystemRepresentation,&st)&&S_ISDIR(st.st_mode);
}
static BOOL LeopardRestoreProfile(NSURL *mount,NSError **error){
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[mount URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
    BOOL matches=InstallerVersionMatches(version[@"ProductVersion"],@"leopard")&&
        RegularFile([mount URLByAppendingPathComponent:@"System/Library/CoreServices/boot.efi"])&&
        (InstallerPackageImage([mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg"])||
         RegularFile([mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg/Contents/Info.plist"]));
    return matches||AssemblyError(error,@"Choose a complete Intel Leopard installation or restore disc with boot.efi and its installation packages.");
}
static NSURL *MountDirectory(NSError **error) {
    // macOS rejected our mount point beneath the external exFAT volume.
    // Only this empty mount point is local; all image bytes stay on scratch.
    NSURL *URL=[[NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES]
        URLByAppendingPathComponent:[@"vim-installer-mount-" stringByAppendingString:NSUUID.UUID.UUIDString]];
    if(![NSFileManager.defaultManager createDirectoryAtURL:URL withIntermediateDirectories:NO
        attributes:@{NSFilePosixPermissions:@0700} error:error])return nil;
    return URL;
}
@interface VIMInstallerAssembly ()
@property NSURL *job;
@property NSUInteger step;
@property NSURL *lastLog;
@property double workFraction;
@property double workEnd;
@property NSTimeInterval workEstimate;
@property NSString *stage;
@property NSString *progressGuest;
- (NSURL *)assembleMavericks:(NSURL *)esd jobDirectory:(NSURL *)directory error:(NSError **)error;
- (NSURL *)assembleClassicDiskImage:(NSURL *)image guest:(NSString *)guest jobDirectory:(NSURL *)directory error:(NSError **)error;
- (BOOL)removeRestoreModelRestrictionAtMount:(NSURL *)mount error:(NSError **)error;
- (BOOL)prepareLeopardRecoveryMemoryAtMount:(NSURL *)mount error:(NSError **)error;
- (BOOL)prepareLionRecoveryMemoryAtMount:(NSURL *)mount error:(NSError **)error;
- (BOOL)prepareSerialRecoveryAtMount:(NSURL *)mount error:(NSError **)error;
- (void)completedWork:(double)fraction;
- (BOOL)run:(NSString *)path arguments:(NSArray<NSString *> *)arguments workingDirectory:(nullable NSURL *)workingDirectory timeout:(NSTimeInterval)timeout cleanup:(BOOL)cleanup error:(NSError **)error;
@end
static bool HFSInstallerProgress(uint64_t completed,uint64_t total,void *context) {
    VIMInstallerAssembly *assembly=(__bridge VIMInstallerAssembly *)context;
    double fraction=.96+.03*(double)completed/(double)total;
    // Check cancellation every transfer; publish at most 30 visible updates.
    if(fraction-assembly.workFraction>=.001)[assembly completedWork:fraction];
    return !assembly.cancelled;
}
static bool HFSExtractionProgress(uint64_t completed,uint64_t total,void *context){
    VIMInstallerAssembly *assembly=(__bridge VIMInstallerAssembly *)context;
    double fraction=.45+.11*(double)completed/(double)total;
    if(fraction-assembly.workFraction>=.001)[assembly completedWork:fraction];
    return !assembly.cancelled;
}
@implementation VIMInstallerAssembly
- (BOOL)extractWorkingHFSVolume:(NSURL *)working error:(NSError **)error {
    NSURL *mounted=MountDirectory(error);if(!mounted)return NO;
    NSURL *volume=[working.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"Recovery-volume.img"];
    BOOL attached=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-owners",@"off",@"-nobrowse",@"-noautoopen",@"-mountpoint",mounted.path,working.path] timeout:180 cleanup:NO error:error];
    BOOL copied=NO;
    if(attached&&[self run:@"/usr/sbin/diskutil" arguments:@[@"info",@"-plist",mounted.path] timeout:60 cleanup:NO error:error]) {
        NSDictionary *info=VIMToolDictionary(self.lastLog);NSString *device=info[@"DeviceNode"];
        NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^/dev/disk[0-9]+s[0-9]+$" options:0 error:NULL];
        if([info[@"FilesystemType"] isEqual:@"hfs"]&&[device isKindOfClass:NSString.class]&&
           [pattern numberOfMatchesInString:device options:0 range:NSMakeRange(0,device.length)]==1) {
            NSString *raw=[device stringByReplacingCharactersInRange:NSMakeRange(5,0) withString:@"r"];
            copied=VIMCopyHFSInstallerVolume(raw.fileSystemRepresentation,volume.fileSystemRepresentation,HFSExtractionProgress,(__bridge void *)self)==0;
            if(!copied)AssemblyError(error,self.cancelled?@"Installer preparation was cancelled. Your download is saved.":@"The installer’s HFS volume couldn’t be copied. Its original image was not changed.");
        }else AssemblyError(error,@"The installer’s HFS partition couldn’t be identified. Its original image was not changed.");
    }
    BOOL detached=YES;
    if(attached){NSError *issue=nil;detached=[self run:@"/usr/bin/hdiutil" arguments:@[@"detach",mounted.path] timeout:60 cleanup:YES error:&issue];if(!detached&&error)*error=issue;}
    if(detached)rmdir(mounted.fileSystemRepresentation);
    if(!copied||!detached)return NO;
    // Only replace our unmounted generated working image. HFS catalog bytes,
    // UUID, system owners and blessing are preserved by the native block copy.
    if(rename(volume.fileSystemRepresentation,working.fileSystemRepresentation))return AssemblyError(error,@"The installer’s working volume couldn’t be saved. Its diagnostic files were retained.");
    return YES;
}
- (BOOL)prepareSerialRecoveryAtMount:(NSURL *)mount error:(NSError **)error {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *tool=self.recoveryControlTool?:[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:@"Runtime/GuestTools/guest-recovery-agent"];
    if(!NonemptyFile(tool))return AssemblyError(error,@"This app is missing its installer support. Reinstall the complete app and try again.");
    NSURL *rc=[mount URLByAppendingPathComponent:@"private/etc/rc.install"];
    NSString *original=[NSString stringWithContentsOfURL:rc encoding:NSUTF8StringEncoding error:error];
    if(!original)return NO;
    // com.apple.install.cd launches rc.install, not the retained rc.cdrom.
    // Both genuine Mojave/Catalina rc.install versions call this native hook
    // before Language Chooser. No modification of Apple's startup script/UI.
    NSString *anchor=@"\t/etc/rc.cdrom.local\n";
    if([original componentsSeparatedByString:anchor].count!=2)
        return AssemblyError(error,@"This installer’s startup files could not be prepared. Its original files have not been changed.");
    NSURL *destination=[mount URLByAppendingPathComponent:@"usr/libexec/guest-recovery-agent"];
    if(![fm copyItemAtURL:tool toURL:destination error:error]||
       ![fm setAttributes:@{NSFilePosixPermissions:@0755} ofItemAtPath:destination.path error:error])return NO;
    NSURL *hook=[mount URLByAppendingPathComponent:@"private/etc/rc.cdrom.local"];
    NSString *start=@"#!/bin/sh\nexec /usr/libexec/guest-recovery-agent --start-recovery-service >> /var/log/virtualintelmac-recovery.log 2>&1\n";
    return ![fm fileExistsAtPath:hook.path]&&[start writeToURL:hook atomically:NO encoding:NSUTF8StringEncoding error:error]&&
        [fm setAttributes:@{NSFilePosixPermissions:@0755} ofItemAtPath:hook.path error:error];
}
- (BOOL)prepareLeopardRecoveryMemoryAtMount:(NSURL *)mount error:(NSError **)error {
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *model=[mount URLByAppendingPathComponent:@"usr/libexec/cpu_check"];
    if([fm fileExistsAtPath:model.path]){
        NSString *original=[NSString stringWithContentsOfURL:model encoding:NSUTF8StringEncoding error:NULL];
        // Some restore discs run this script before the installer. Retail
        // media may not contain it. Bypass only a recognizable script, never
        // overwrite an unknown executable or insist on a particular model list.
        if([original hasPrefix:@"#!"]){
            NSString *line=[[original componentsSeparatedByString:@"\n"] firstObject];
            if([line containsString:@"perl"]||[line hasSuffix:@"/sh"]||[line hasSuffix:@"/bash"]){
                NSString *replacement=[line stringByAppendingString:[line containsString:@"perl"]?@"\nexit 0;\n":@"\nexit 0\n"];
                NSNumber *mode=[fm attributesOfItemAtPath:model.path error:error][NSFilePosixPermissions];
                if(!mode||![fm setAttributes:@{NSFilePosixPermissions:@(mode.unsignedIntValue|0200)} ofItemAtPath:model.path error:error])return NO;
                BOOL saved=[replacement writeToURL:model atomically:NO encoding:NSUTF8StringEncoding error:error];
                NSError *restore=nil;
                if(![fm setAttributes:@{NSFilePosixPermissions:mode} ofItemAtPath:model.path error:&restore]){if(error)*error=restore;return NO;}
                if(!saved)return NO;
            }
        }
    }
    NSURL *script=[mount URLByAppendingPathComponent:@"etc/rc.cdrom"];
    if(![fm fileExistsAtPath:script.path])return YES;
    NSString *text=[NSString stringWithContentsOfURL:script encoding:NSUTF8StringEncoding error:error];
    if(!text)return NO;
    // Measured 9J3050 recovery uses 512-byte sectors: 128 KB /var/run
    // cannot hold the adapter plus a status tool, and 512 KB /var/tmp
    // cannot stage the compiled setup controls. Change only this generated
    // DVD copy's native RAMDisk budgets; never remount live socket paths.
    NSDictionary *budgets=@{@"/var/tmp":@262144,@"/var/run":@16384};
    for(NSString *path in budgets){
        NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:
            [NSString stringWithFormat:@"(?m)^(\\s*RAMDisk[ \\t]+%@[ \\t]+)([0-9]+)",[NSRegularExpression escapedPatternForString:path]] options:0 error:NULL];
        for(NSTextCheckingResult *match in [[pattern matchesInString:text options:0 range:NSMakeRange(0,text.length)] reverseObjectEnumerator]){
            NSRange number=[match rangeAtIndex:2];
            if([[text substringWithRange:number] longLongValue]<[budgets[path] longLongValue])
                text=[text stringByReplacingCharactersInRange:number withString:[budgets[path] stringValue]];
        }
    }
    NSNumber *mode=[NSFileManager.defaultManager attributesOfItemAtPath:script.path error:error][NSFilePosixPermissions];
    if(!mode)return NO;
    BOOL written=[NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:@(mode.unsignedIntValue|0200)} ofItemAtPath:script.path error:error]&&
        [text writeToURL:script atomically:NO encoding:NSUTF8StringEncoding error:error];
    NSError *restore=nil;
    if(![NSFileManager.defaultManager setAttributes:@{NSFilePosixPermissions:mode} ofItemAtPath:script.path error:&restore]){if(error)*error=restore;return NO;}
    return written;
}
- (BOOL)prepareLionRecoveryMemoryAtMount:(NSURL *)mount error:(NSError **)error {
    NSURL *script=[mount URLByAppendingPathComponent:@"etc/rc.cdrom"];
    NSString *original=[NSString stringWithContentsOfURL:script encoding:NSUTF8StringEncoding error:NULL];
    if(![original containsString:@"\nRAMDisk()\n{"])return YES;
    NSString *size=@"\n  rdsize=$2\n",*format=@"\n    newfs_hfs $dev\n";
    if([original componentsSeparatedByString:size].count!=2||
       [original componentsSeparatedByString:format].count!=2)return YES;
    // Native 11G63 can block in nonjournaled async HFS metadata I/O on
    // /var/run before Language Chooser can start Installer. Journal only
    // the recovery environment's disposable RAM disks; keep the guest's
    // configured CPUs, mount layout, and installation target unchanged.
    // The 4 MiB minimum accommodates a 512 KiB journal on tiny volumes.
    NSString *updated=[original stringByReplacingOccurrencesOfString:size
        withString:@"\n  rdsize=$2\n  if [ \"$rdsize\" -lt 8192 ]; then\n    rdsize=8192\n  fi\n"];
    updated=[updated stringByReplacingOccurrencesOfString:format
        withString:@"\n    newfs_hfs -J 512k $dev || newfs_hfs $dev || return\n"];
    NSFileManager *fm=NSFileManager.defaultManager;
    NSNumber *mode=[fm attributesOfItemAtPath:script.path error:error][NSFilePosixPermissions];
    if(!mode||![fm setAttributes:@{NSFilePosixPermissions:@(mode.unsignedIntValue|0200)} ofItemAtPath:script.path error:error])return NO;
    BOOL written=[updated writeToURL:script atomically:NO encoding:NSUTF8StringEncoding error:error];
    NSError *restore=nil;
    if(![fm setAttributes:@{NSFilePosixPermissions:mode} ofItemAtPath:script.path error:&restore]){if(error)*error=restore;return NO;}
    return written;
}
+ (BOOL)validatePreparedMedia:(NSURL *)URL guest:(NSString *)guest error:(NSError **)error {
    struct stat image,record;
    if(!URL.isFileURL||lstat(URL.fileSystemRepresentation,&image)||!S_ISREG(image.st_mode)||image.st_size<=0)
        return AssemblyError(error,@"The installer image is unavailable. Reconnect its drive or choose the installer again.");
    NSURL *receipt=[URL.URLByDeletingLastPathComponent URLByAppendingPathComponent:@"Media.plist"];
    if(lstat(receipt.fileSystemRepresentation,&record)) {
        if(errno==ENOENT)return YES; // An ordinary image, not our prepared output.
        return AssemblyError(error,@"The installer’s preparation record couldn’t be read. Reconnect its drive and try again.");
    }
    NSDictionary *info=S_ISREG(record.st_mode)&&record.st_size>0&&record.st_size<=65536?
        [NSDictionary dictionaryWithContentsOfURL:receipt]:nil;
    if(![info[@"FormatVersion"] isEqual:@1]||![info[@"Stage"] isEqual:@"media-created"]||
       ![info[@"Guest"] isEqual:guest]||![info[@"Media"] isEqual:URL.lastPathComponent]||
       ![info[@"Size"] isKindOfClass:NSNumber.class]||[info[@"Size"] longLongValue]!=image.st_size||
       ([@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]&&![info[@"PartitionedInstaller"] isEqual:@YES])||
       ([guest isEqual:@"tiger"]&&(![info[@"TigerRestorePolicyVersion"] isEqual:@1]||![info[@"TigerControlBootstrapVersion"] isEqual:@8]||![info[@"DiscTwoExcluded"] isEqual:@YES]||![info[@"RestoreModelRestrictionRemoved"] isEqual:@YES])))
        return AssemblyError(error,@"The prepared installer is incomplete or has changed. Prepare it again from the original installer. Your virtual Mac and downloaded installer haven’t been changed.");
    if(!PreparedImageHasBootBlocks(URL))
        return AssemblyError(error,@"The prepared installer’s disk header is unreadable. Prepare it again from the original installer. Your virtual Mac and original installer haven’t been changed.");
    return YES;
}
+ (void)retireCompletedDownloads:(NSDictionary<NSString *,NSURL *> *)files {
    NSFileManager *fm=NSFileManager.defaultManager;
    for(NSURL *file in files.allValues) {
        NSURL *directory=file.URLByDeletingLastPathComponent;
        // Downloads are SHA-256 keyed, with a fixed payload basename. Do not
        // follow symlinks or remove arbitrary caller-provided sources.
        NSString *key=directory.lastPathComponent;
        if(![file.lastPathComponent isEqual:@"Payload.pkg"]||![directory.URLByDeletingLastPathComponent.lastPathComponent isEqual:@"Downloads"]||key.length!=64||
           [key rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet]].location!=NSNotFound||
           !UnlinkedDirectory(directory))continue;
        int fd=open([directory URLByAppendingPathComponent:@"Download.lock"].fileSystemRepresentation,O_RDWR|O_NOFOLLOW|O_CLOEXEC);
        struct stat st;
        if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||flock(fd,LOCK_EX|LOCK_NB)){if(fd>=0)close(fd);continue;}
        NSDictionary *record=[NSDictionary dictionaryWithContentsOfURL:[directory URLByAppendingPathComponent:@"Download.plist"]];
        NSURL *source=[NSURL URLWithString:[record[@"SourceURL"] isKindOfClass:NSString.class]?record[@"SourceURL"]:@""];
        if([record[@"FormatVersion"] isEqual:@1]&&[record[@"Complete"] isEqual:@YES]&&[record[@"Length"] isKindOfClass:NSNumber.class]&&[VIMInstallerSource isApprovedURL:source]&&
           !lstat(file.fileSystemRepresentation,&st)&&S_ISREG(st.st_mode)&&st.st_nlink==1&&st.st_size>0&&st.st_size==[record[@"Length"] longLongValue]) {
            // Payload first, then the completion record: interruption leaves a
            // stale record, which beginTransfer can safely restart below.
            if([fm removeItemAtURL:file error:nil])[fm removeItemAtURL:[directory URLByAppendingPathComponent:@"Download.plist"] error:nil];
        }
        close(fd);
    }
}
+ (NSDictionary<NSString *,NSURL *> *)retainedInstallerForGuest:(NSString *)guest library:(NSURL *)library {
    if(![NSUserDefaults.standardUserDefaults boolForKey:@"VIMKeepInstallers"])return @{};
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *root=[VIMLibraryLocation installersURLForMachines:library].URLByResolvingSymlinksInPath;
    NSArray *attempts=[[fm contentsOfDirectoryAtURL:root includingPropertiesForKeys:nil options:0 error:nil] sortedArrayUsingComparator:^NSComparisonResult(NSURL *a,NSURL *b){
        NSDate *ad=nil,*bd=nil;[a getResourceValue:&ad forKey:NSURLContentModificationDateKey error:nil];[b getResourceValue:&bd forKey:NSURLContentModificationDateKey error:nil];return [bd compare:ad];
    }];
    NSMutableDictionary *found=[NSMutableDictionary new];
    for(NSURL *attempt in attempts) {
        if(![[NSUUID alloc] initWithUUIDString:attempt.lastPathComponent]||!UnlinkedDirectory(attempt))continue;
        NSURL *mediaDir=[attempt URLByAppendingPathComponent:@"Media"];
        NSDictionary *media=[NSDictionary dictionaryWithContentsOfURL:[mediaDir URLByAppendingPathComponent:@"Media.plist"]];
        if(!found[@"Media"]&&[media[@"FormatVersion"] isEqual:@1]&&[media[@"Guest"] isEqual:guest]&&[media[@"Stage"] isEqual:@"media-created"]&&
           (![@[@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra"] containsObject:guest]||([media[@"LegacyBootstrapVersion"] isEqual:@(OpenGLBootstrapMediaVersion(guest))]&&[media[@"ReadbackVerified"] isEqual:@YES]&&
             [media[@"OpenGLSupportVersion"] isEqual:@1]))&&
           (![guest isEqual:@"bigsur"]||[media[@"SignedSharedSupportPackage"] isEqual:@YES])&&
           (!SerialRecovery(guest)||[media[@"SerialRecoveryVersion"] isEqual:@2])&&
           (![guest isEqual:@"leopard"]||[media[@"LeopardRecoveryMemoryVersion"] isEqual:@1])&&
           (!NativeAutomatedRecovery(guest)||[media[@"NativeRecoverySystemVersion"] isEqual:([guest isEqual:@"highsierra"]?@6:@1)])&&
           (![@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]||[media[@"PartitionedInstaller"] isEqual:@YES])&&
           (![guest isEqual:@"tiger"]||([media[@"TigerRestorePolicyVersion"] isEqual:@1]&&[media[@"TigerControlBootstrapVersion"] isEqual:@8]&&
             [media[@"DiscTwoExcluded"] isEqual:@YES]&&[media[@"RestoreModelRestrictionRemoved"] isEqual:@YES]&&[media[@"ReadbackVerified"] isEqual:@YES]))&&
           [@[@"Installer.iso",@"Installer.img"] containsObject:media[@"Media"]]&&[media[@"Size"] isKindOfClass:NSNumber.class]) {
            NSURL *url=[mediaDir URLByAppendingPathComponent:media[@"Media"]];struct stat st;
            if(UnlinkedDirectory(mediaDir)&&!lstat(url.fileSystemRepresentation,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size==[media[@"Size"] longLongValue])found[@"Media"]=url;
        }
        NSURL *assembly=[attempt URLByAppendingPathComponent:@"Assembly"];
        NSDictionary *record=[NSDictionary dictionaryWithContentsOfURL:[assembly URLByAppendingPathComponent:@"Assembly.plist"]];
        NSString *name=record[@"Installer"];
        if(!found[@"SourceInstaller"]&&[record[@"FormatVersion"] isEqual:@1]&&[record[@"Guest"] isEqual:guest]&&[record[@"Stage"] isEqual:@"assembled"]&&
           [name isKindOfClass:NSString.class]&&[name isEqual:name.lastPathComponent]) {
            NSURL *url=[assembly URLByAppendingPathComponent:name];
            if(UnlinkedDirectory(assembly)&&UnlinkedDirectory(url)&&[self validateInstallerApplication:url guest:guest error:nil])found[@"SourceInstaller"]=url;
        }
        if(found.count==2)break;
    }
    return found;
}
+ (NSString *)guestForInstallerApplication:(NSURL *)URL error:(NSError **)error {
    if(RegularFile(URL)&&![URL.pathExtension.lowercaseString isEqual:@"app"]) {
        // Selection is a worker operation: inspect the actual read-only DVD,
        // never infer its OS from a filename or the selected creation row.
        if(![self validateInstallerApplication:URL guest:@"leopard" error:error])return nil;
        NSURL *mount=MountDirectory(error),*logs=MountDirectory(error);
        if(!mount||!logs){if(mount)rmdir(mount.fileSystemRepresentation);if(logs)rmdir(logs.fileSystemRepresentation);return nil;}
        VIMInstallerAssembly *worker=[self new];worker.job=logs;
        BOOL attached=[worker run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",mount.path,URL.path] timeout:180 cleanup:NO error:error];
        NSString *guest=nil;
        if(attached) {
            NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[mount URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
            if(InstallerVersionMatches(version[@"ProductVersion"],@"tiger")&&VIMTigerValidateRestoreDVD(mount,error))guest=@"tiger";
            else if(InstallerVersionMatches(version[@"ProductVersion"],@"leopard")&&LeopardRestoreProfile(mount,error))guest=@"leopard";
            else if(InstallerVersionMatches(version[@"ProductVersion"],@"snowleopard")&&
                RegularFile([mount URLByAppendingPathComponent:@"System/Library/CoreServices/boot.efi"])&&
                (InstallerPackageImage([mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg"])||
                 RegularFile([mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg/Contents/Info.plist"])))guest=@"snowleopard";
            else if(!guest)AssemblyError(error,@"Choose a supported Tiger, Leopard or Snow Leopard restore DVD, or a complete installer app for a newer OS.");
            if(guest&&!ClassicReleaseSupported(guest,version,error))guest=nil;
        }
        NSError *detachError=nil;
        BOOL detached=!attached||[worker run:@"/usr/bin/hdiutil" arguments:@[@"detach",mount.path] timeout:60 cleanup:YES error:&detachError];
        if(detached){rmdir(mount.fileSystemRepresentation);[NSFileManager.defaultManager removeItemAtURL:logs error:nil];}
        else {if(error)*error=detachError;guest=nil;}
        return guest;
    }
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfURL:[URL URLByAppendingPathComponent:@"Contents/Info.plist"]];
    NSString *identifier=info[@"CFBundleIdentifier"];
    NSString *guest=nil;
    if([identifier isKindOfClass:NSString.class])for(NSString *candidate in InstallerIdentifiers())
        if([identifier isEqual:InstallerIdentifiers()[candidate]]){guest=candidate;break;}
    if(!guest){AssemblyError(error,@"Choose a complete installer app for a supported version from Lion through Big Sur, or a supported Mac OS X restore DVD disk image.");return nil;}
    return [self validateInstallerApplication:URL guest:guest error:error]?guest:nil;
}
+ (BOOL)validateInstallerApplication:(NSURL *)URL guest:(NSString *)guest error:(NSError **)error {
    if([@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]) {
        // Resume gate only: media creation mounts the original read-only and
        // verifies 10.6, boot.efi, packages and the measured restore policy.
        // Snow's complete source is a DVD, not an InstallAssistant bundle.
        int fd=open(URL.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        struct stat st;
        // A retail DVD may be raw ISO/CDR rather than a UDIF DMG. Actual
        // mounted boot/package contents determine acceptance below; filename,
        // original disc model and a particular container trailer do not.
        BOOL valid=fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>512;
        if(fd>=0)close(fd);
        return valid||AssemblyError(error,@"Choose the complete Mac OS X restore DVD disk image.");
    }
    NSString *identifier=InstallerIdentifiers()[guest];
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfURL:[URL URLByAppendingPathComponent:@"Contents/Info.plist"]];
    NSURL *support=[URL URLByAppendingPathComponent:@"Contents/SharedSupport"];
    NSURL *installURL=[support URLByAppendingPathComponent:@"InstallInfo.plist"];
    NSDictionary *install=[NSDictionary dictionaryWithContentsOfURL:installURL];
    NSDictionary *system=[install[@"System Image Info"] isKindOfClass:NSDictionary.class]?install[@"System Image Info"]:nil;
    id version=system[@"version"];
    BOOL splitInstaller=![guest isEqual:@"bigsur"];
    BOOL classic=[@[@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra"] containsObject:guest];
    BOOL complete=(classic||NonemptyFile([URL URLByAppendingPathComponent:@"Contents/Resources/createinstallmedia"]))&&
        NonemptyFile([support URLByAppendingPathComponent:splitInstaller?@"InstallESD.dmg":@"SharedSupport.dmg"])&&
        (!splitInstaller||classic||NonemptyFile([support URLByAppendingPathComponent:@"BaseSystem.dmg"]));
    // Big Sur ships only SharedSupport.dmg, with OSVersion inside its asset
    // manifest. Media preparation checks that manifest before using boot files.
    // Do not reject Apple's complete app merely because InstallInfo is absent.
    BOOL versionMatches=((!splitInstaller||classic)&&![NSFileManager.defaultManager fileExistsAtPath:installURL.path])||InstallerVersionMatches(version,guest);
    BOOL matches=identifier&&URL.isFileURL&&[URL.pathExtension.lowercaseString isEqual:@"app"]&&[info[@"CFBundleIdentifier"] isEqual:identifier]&&versionMatches;
    if(!matches||!complete)return AssemblyError(error,@"Choose the complete installer app for the selected Mac OS version. This file is a different version or is missing installation files.");
    if(!splitInstaller&&!InstallerPackageImage([support URLByAppendingPathComponent:@"SharedSupport.dmg"]))
        return AssemblyError(error,@"This Big Sur installer is missing Apple’s signed installation package. Choose a complete installer app, or download a new copy using Virtual Intel Mac.");
    return YES;
}
- (void)report:(NSString *)stage {
    self.stage=stage;
    if(self.progress)dispatch_async(dispatch_get_main_queue(), ^{ if(self.progress)self.progress(stage); });
}
- (void)completedWork:(double)fraction {
    self.workFraction=MAX(self.workFraction,fraction);
    fraction=self.workFraction;
    if(self.workProgress)dispatch_async(dispatch_get_main_queue(), ^{if(self.workProgress)self.workProgress(fraction);});
}
- (void)nextWorkTo:(double)end seconds:(NSTimeInterval)seconds {self.workEnd=end;self.workEstimate=seconds;}
// Apple's recovery installer is CS_RESTRICT on Mavericks, so dyld removes
// DYLD_INSERT_LIBRARIES even though the root-only authorization is valid.
// Preserve the original app for manual installation. The separate automatic
// app is the same native code/resources, ad-hoc signed for this generated image.
// Its ONLY omitted entitlement permits securityd credential stashing for an
// existing encrypted installation; our authorization accepts only a new empty
// HFS target. Unknown entitlements fail closed instead of silently stripping.
- (BOOL)removeRestoreModelRestrictionAtMount:(NSURL *)mount error:(NSError **)error {
    // Retail and model-specific restore DVDs have different eligibility
    // scripts. Change only the generated copy's native installation-check
    // hook, not a hardcoded model list or arbitrary JavaScript body.
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *package=[mount URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg"];
    BOOL flat=InstallerPackageImage(package);
    NSURL *expanded=[self.job URLByAppendingPathComponent:@"RestoreDistribution"];
    if(flat&&(![fm createDirectoryAtURL:expanded withIntermediateDirectories:NO attributes:nil error:error]||
       ![self run:@"/usr/bin/xar" arguments:@[@"-xf",package.path,@"-C",expanded.path] timeout:180 cleanup:NO error:error]))return NO;
    NSURL *distribution=flat?[expanded URLByAppendingPathComponent:@"Distribution"]:[package URLByAppendingPathComponent:@"Contents/OSInstall.dist"];
    // Some retail discs express no hardware gate as a Distribution at all.
    // Let their native package installer try rather than require restore logic.
    if(!flat&&![fm fileExistsAtPath:distribution.path])return YES;
    NSString *text=[NSString stringWithContentsOfURL:distribution encoding:NSUTF8StringEncoding error:error];
    NSXMLDocument *plan=text?[[NSXMLDocument alloc] initWithXMLString:text options:NSXMLNodePreserveAll|NSXMLNodeLoadExternalEntitiesNever error:error]:nil;
    if(![plan.rootElement.name isEqual:@"installer-gui-script"])
        return AssemblyError(error,@"This disc’s installation plan could not be read. Its original image was retained.");
    for(NSXMLElement *check in [plan.rootElement elementsForName:@"installation-check"])
        [check attributeForName:@"script"].stringValue=@"true";
    if([fm fileExistsAtPath:[mount URLByAppendingPathComponent:@"System/Installation/Packages/Rosetta.pkg"].path])
        for(NSXMLElement *choice in [plan.rootElement elementsForName:@"choice"])
            if([[choice attributeForName:@"id"].stringValue isEqual:@"Rosetta"]){
                NSXMLNode *selected=[choice attributeForName:@"start_selected"];
                if(selected)selected.stringValue=@"true";
                else [choice addAttribute:[NSXMLNode attributeWithName:@"start_selected" stringValue:@"true"]];
            }
    NSString *patched=plan.XMLString;
    if(!flat){
        NSNumber *mode=[fm attributesOfItemAtPath:distribution.path error:error][NSFilePosixPermissions];
        if(!mode||![fm setAttributes:@{NSFilePosixPermissions:@(mode.unsignedIntValue|0200)} ofItemAtPath:distribution.path error:error])return NO;
        BOOL saved=patched&&[patched writeToURL:distribution atomically:NO encoding:NSUTF8StringEncoding error:error];
        NSError *restore=nil;
        if(![fm setAttributes:@{NSFilePosixPermissions:mode} ofItemAtPath:distribution.path error:&restore]){if(error)*error=restore;return NO;}
        return saved;
    }
    if(!patched||
       ![patched writeToURL:distribution atomically:NO encoding:NSUTF8StringEncoding error:error])return NO;
    NSURL *output=[self.job URLByAppendingPathComponent:@"OSInstall-virtual.mpkg"];
    // xar's -C applies ONLY to extraction. During creation it silently logs
    // missing relative files yet exits zero, producing an empty archive.
    // Give the child a real cwd, then extract/read back its actual members.
    NSMutableArray *args=[@[@"-cf",output.path] mutableCopy];
    NSArray *entries=[fm contentsOfDirectoryAtPath:expanded.path error:error];
    if(!entries.count)return NO;
    [args addObjectsFromArray:[entries sortedArrayUsingSelector:@selector(compare:)]];
    if(![self run:@"/usr/bin/xar" arguments:args workingDirectory:expanded timeout:180 cleanup:NO error:error])return NO;
    NSURL *verify=[self.job URLByAppendingPathComponent:@"RestoreDistributionReadback"];
    if(![fm createDirectoryAtURL:verify withIntermediateDirectories:NO attributes:nil error:error]||
       ![self run:@"/usr/bin/xar" arguments:@[@"-xf",output.path,@"-C",verify.path] timeout:180 cleanup:NO error:error]||
       ![[NSString stringWithContentsOfURL:[verify URLByAppendingPathComponent:@"Distribution"] encoding:NSUTF8StringEncoding error:error] isEqual:patched])
        return AssemblyError(error,@"The Snow Leopard installation packages couldn’t be verified. The original DVD image was retained.");
    NSData *bytes=[NSData dataWithContentsOfURL:output options:0 error:error];
    NSNumber *mode=[fm attributesOfItemAtPath:package.path error:error][NSFilePosixPermissions];
    if(!bytes.length||bytes.length>8*1024*1024||!mode||
       ![fm setAttributes:@{NSFilePosixPermissions:@(mode.unsignedIntValue|0200)} ofItemAtPath:package.path error:error])return NO;
    BOOL saved=[bytes writeToURL:package options:0 error:error];
    NSError *restore=nil;
    if(![fm setAttributes:@{NSFilePosixPermissions:mode} ofItemAtPath:package.path error:&restore]){if(error)*error=restore;return NO;}
    return saved;
}
- (BOOL)prepareAutomaticInstallerAtMount:(NSURL *)mount guest:(NSString *)guest error:(NSError **)error {
    NSString *executable=[NSString stringWithUTF8String:VIMOpenGLRecoveryInstallerForGuest(guest.UTF8String)];
    NSURL *binary=[mount URLByAppendingPathComponent:[executable substringFromIndex:1]];
    NSURL *original=binary.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent;
    NSURL *automatic=[mount URLByAppendingPathComponent:@"System/Installation/CDIS/VirtualIntelMac Installer.app"];
    SecStaticCodeRef code=NULL;CFDictionaryRef information=NULL;
    OSStatus status=SecStaticCodeCreateWithPath((__bridge CFURLRef)binary,kSecCSDefaultFlags,&code);
    if(status==errSecSuccess)status=SecCodeCopySigningInformation(code,kSecCSSigningInformation,&information);
    if(code)CFRelease(code);
    NSDictionary *details=CFBridgingRelease(information),*entitlements=details[(__bridge NSString *)kSecCodeInfoEntitlementsDict];
    if(status!=errSecSuccess||!details||
       (entitlements&&(![entitlements isKindOfClass:NSDictionary.class]||entitlements.count>1||
        (entitlements.count&&![entitlements[@"com.apple.private.securityd.stash"] isEqual:@YES]))))
        return AssemblyError(error,@"This OS X installer’s automatic launch policy is not recognized. Its original installer was not changed.");
    NSFileManager *fm=NSFileManager.defaultManager;
    if([fm fileExistsAtPath:automatic.path])return AssemblyError(error,@"Automatic installer preparation found an unexpected existing copy.");
    NSURL *policy=[self.job URLByAppendingPathComponent:@"AutomaticInstallerEntitlements.plist"];
    NSData *empty=[NSPropertyListSerialization dataWithPropertyList:@{} format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    if(!empty||![empty writeToURL:policy options:NSDataWritingAtomic error:error])return NO;
    return [self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",original.path,automatic.path] timeout:180 cleanup:NO error:error]&&
        [self run:@"/usr/bin/codesign" arguments:@[@"--force",@"--sign",@"-",@"--digest-algorithm=sha1,sha256",@"--entitlements",policy.path,automatic.path] timeout:60 cleanup:NO error:error]&&
        [self run:@"/usr/bin/codesign" arguments:@[@"--verify",@"--strict",automatic.path] timeout:60 cleanup:NO error:error];
}
// No shell, package scripts, privileged installer invocation, or output pipes.
// Logs stay in the job; a child producing much output cannot deadlock its parent.
- (BOOL)run:(NSString *)path arguments:(NSArray<NSString *> *)arguments timeout:(NSTimeInterval)timeout
    cleanup:(BOOL)cleanup error:(NSError **)error {
    return [self run:path arguments:arguments workingDirectory:nil timeout:timeout cleanup:cleanup error:error];
}
- (BOOL)run:(NSString *)path arguments:(NSArray<NSString *> *)arguments workingDirectory:(NSURL *)workingDirectory
    timeout:(NSTimeInterval)timeout cleanup:(BOOL)cleanup error:(NSError **)error {
    if(self.cancelled&&!cleanup)return AssemblyError(error,@"Installer preparation was cancelled. Downloads were retained.");
    NSURL *log=[self.job URLByAppendingPathComponent:[NSString stringWithFormat:@"%02lu-%@.log",++self.step,path.lastPathComponent]];
    self.lastLog=log;
    int fd=open(log.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)return AssemblyError(error,@"Cannot create the installer preparation log.");
    NSFileHandle *output=[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:path];
    if(workingDirectory)task.currentDirectoryURL=workingDirectory;
    NSMutableArray *taskArguments=[arguments mutableCopy];
    // makehybrid does not implement -puppetstrings (and rejects it outright).
    // Its progress uses the learned/estimated duration instead of percentages.
    BOOL percentOutput=[path.lastPathComponent isEqual:@"hdiutil"]&&[arguments.firstObject isEqual:@"convert"];
    if(percentOutput)[taskArguments addObject:@"-puppetstrings"];
    task.arguments=taskArguments;
    NSMutableDictionary *environment=[NSProcessInfo.processInfo.environment mutableCopy];
    environment[@"LC_ALL"]=@"C";environment[@"LANG"]=@"C";task.environment=environment;
    task.standardOutput=output;task.standardError=output;task.standardInput=NSFileHandle.fileHandleWithNullDevice;
    dispatch_semaphore_t ended=dispatch_semaphore_create(0);
    task.terminationHandler=^(NSTask *finished){dispatch_semaphore_signal(ended);};
    if(![task launchAndReturnError:error])return NO;
    NSTimeInterval start=NSProcessInfo.processInfo.systemUptime;
    double begin=self.workFraction,end=MAX(begin,self.workEnd);
    NSString *timingKey=[NSString stringWithFormat:@"VIMPreparationDuration-%@-%@-%@-%@",self.progressGuest?:@"",self.stage?:@"",path.lastPathComponent,arguments.firstObject];
    double learned=[NSUserDefaults.standardUserDefaults doubleForKey:timingKey];
    double estimate=learned>0?learned:MAX(1,self.workEstimate);
    NSFileHandle *reader=percentOutput?[NSFileHandle fileHandleForReadingFromURL:log error:nil]:nil;
    NSMutableData *pending=[NSMutableData data];double toolFraction=-1;NSTimeInterval lastUpdate=0;
    BOOL interrupted=NO;
    while(dispatch_semaphore_wait(ended,dispatch_time(DISPATCH_TIME_NOW,100*NSEC_PER_MSEC))) {
        NSTimeInterval elapsed=NSProcessInfo.processInfo.systemUptime-start;
        if(end>begin&&elapsed-lastUpdate>=.5) {
            lastUpdate=elapsed;
            if(reader) {
                [pending appendData:[reader readDataOfLength:65536]];
                NSString *lines=[[NSString alloc] initWithData:pending encoding:NSUTF8StringEncoding];
                NSRange newline=[lines rangeOfString:@"\n" options:NSBackwardsSearch];
                if(newline.location!=NSNotFound) {
                    NSString *complete=[lines substringToIndex:newline.location+1];
                    for(NSString *line in [complete componentsSeparatedByString:@"\n"])
                        if([line hasPrefix:@"PERCENT:"])toolFraction=MAX(toolFraction,MIN(1,[[line substringFromIndex:8] doubleValue]/100));
                    NSUInteger consumed=[complete lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
                    [pending replaceBytesInRange:NSMakeRange(0,consumed) withBytes:NULL length:0];
                }
            }
            // Tool output is authoritative. Otherwise use a measured duration,
            // slowing near the end instead of claiming completion prematurely.
            double fraction=toolFraction>=0?MIN(.99,toolFraction):MIN(.98,elapsed<estimate*.9?elapsed/estimate:.9+.08*(1-exp(-(elapsed-estimate*.9)/estimate)));
            [self completedWork:begin+(end-begin)*fraction];
            double operationRemaining=toolFraction>.01&&elapsed>1?elapsed*(1-MIN(.999,toolFraction))/toolFraction:MAX(1,estimate-elapsed);
            double seconds=operationRemaining+(1-end)*240;
            if(self.timeRemaining)dispatch_async(dispatch_get_main_queue(),^{if(self.timeRemaining)self.timeRemaining(seconds);});
        }
        if((self.cancelled&&!cleanup)||NSProcessInfo.processInfo.systemUptime-start>timeout) {
            interrupted=YES;[task terminate];
            if(dispatch_semaphore_wait(ended,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC))) {
                if(task.running)kill(task.processIdentifier,SIGKILL);
                [task waitUntilExit];
            }
            break;
        }
    }
    [reader closeFile];
    [output closeFile];
    if(interrupted)return AssemblyError(error,@"Preparing the installer took longer than expected or was cancelled. Your downloads are saved. Try again to resume setup.");
    if(task.terminationStatus==0) {
        if(end>begin) {
            double duration=NSProcessInfo.processInfo.systemUptime-start;
            [NSUserDefaults.standardUserDefaults setDouble:learned>0?.6*learned+.4*duration:duration forKey:timingKey];
            [self completedWork:end];
        }
        return YES;
    }
    if(error)*error=[NSError errorWithDomain:@"VirtualIntelMac.InstallerAssembly" code:task.terminationStatus userInfo:@{
        NSLocalizedDescriptionKey:@"The OS installer couldn’t be prepared.",
        NSLocalizedRecoverySuggestionErrorKey:@"Your downloads are saved. Try again to resume setup. If it fails again, check that your library’s drive is connected and has enough free space.",
        @"PreparationLog":log.path}];
    return NO;
}
- (NSURL *)assembleClassicDiskImage:(NSURL *)image guest:(NSString *)guest jobDirectory:(NSURL *)directory error:(NSError **)error {
    NSString *name=@{@"lion":@"Install Mac OS X Lion.app",@"mountainlion":@"Install OS X Mountain Lion.app",@"yosemite":@"Install OS X Yosemite.app",@"elcapitan":@"Install OS X El Capitan.app",@"sierra":@"Install macOS Sierra.app"}[guest];
    NSFileManager *fm=NSFileManager.defaultManager;
    if(!name||!RegularFile(image)||!directory.isFileURL||[fm fileExistsAtPath:directory.path]){AssemblyError(error,@"Choose a complete Apple download and a new preparation directory.");return nil;}
    NSDictionary *space=[fm attributesOfFileSystemForPath:directory.URLByDeletingLastPathComponent.path error:error];
    if(!space)return nil;
    if([space[NSFileSystemFreeSize] unsignedLongLongValue]<(16ULL<<30)){AssemblyError(error,@"Installer preparation needs at least 16 GB of available space.");return nil;}
    if(![fm createDirectoryAtURL:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:error])return nil;
    self.job=directory;self.step=0;self.progressGuest=guest;
    NSString *source=[VIMInstallerCatalog fileForGuest:guest role:@"diskimage"][@"URL"];
    if(!source){AssemblyError(error,@"This installer has no download record in the installer catalog.");return nil;}
    [self report:@"Verifying the original Apple download…"];
    // Historical packages have expired Apple certificates. Authenticate the
    // exact approved image bytes instead of weakening global signature policy.
    if(![VIMInstallerSource verifyFile:image source:[NSURL URLWithString:source] error:error])return nil;
    NSURL *mount=MountDirectory(error);if(!mount)return nil;
    BOOL mounted=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",mount.path,image.path] timeout:180 cleanup:NO error:error];
    BOOL unpacked=NO;NSURL *expanded=[directory URLByAppendingPathComponent:@"Expanded"];
    NSString *package=[guest isEqual:@"sierra"]?@"InstallOS.pkg":@"InstallMacOSX.pkg";
    if(mounted) {
        [self report:@"Unpacking the original Mac OS X installer…"];[self nextWorkTo:.35 seconds:90];
        unpacked=[self run:@"/usr/sbin/pkgutil" arguments:@[@"--expand-full",[mount URLByAppendingPathComponent:package].path,expanded.path] timeout:1800 cleanup:NO error:error];
    }
    NSError *issue=nil;BOOL detached=[self run:@"/usr/bin/hdiutil" arguments:@[@"detach",mount.path] timeout:60 cleanup:YES error:&issue];
    if(detached)rmdir(mount.fileSystemRepresentation);
    if(!unpacked||!detached){if(unpacked&&error)*error=issue;return nil;}
    // Both pinned Apple products place their app in Payload and their ESD as
    // a sibling. This is the package script's data-placement step, not execution
    // of that script on a modern host (and does not require administrator access).
    NSURL *component=[expanded URLByAppendingPathComponent:package];
    NSURL *app=[directory URLByAppendingPathComponent:name];
    if(![fm moveItemAtURL:[component URLByAppendingPathComponent:[@"Payload" stringByAppendingPathComponent:name]] toURL:app error:error])return nil;
    NSURL *support=[app URLByAppendingPathComponent:@"Contents/SharedSupport"];
    if(![fm createDirectoryAtURL:support withIntermediateDirectories:YES attributes:nil error:error]||
       ![fm moveItemAtURL:[component URLByAppendingPathComponent:@"InstallESD.dmg"] toURL:[support URLByAppendingPathComponent:@"InstallESD.dmg"] error:error]||
       ![self.class validateInstallerApplication:app guest:guest error:error])return nil;
    if(![fm removeItemAtURL:expanded error:error])return nil;
    NSDictionary *receipt=@{@"FormatVersion":@1,@"Guest":guest,@"Stage":@"assembled",@"Installer":name,@"OriginalImagePinned":@YES};
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:receipt format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    if(!data||![data writeToURL:[directory URLByAppendingPathComponent:@"Assembly.plist"] options:NSDataWritingAtomic error:error])return nil;
    [self completedWork:.45];return app;
}
- (NSURL *)assembleMavericks:(NSURL *)esd jobDirectory:(NSURL *)directory error:(NSError **)error {
    NSFileManager *fm=NSFileManager.defaultManager;
    if(!RegularFile(esd)||!directory.isFileURL||[fm fileExistsAtPath:directory.path]){
        AssemblyError(error,@"Choose a complete Mavericks download and a new preparation directory.");return nil;
    }
    NSDictionary *space=[fm attributesOfFileSystemForPath:directory.URLByDeletingLastPathComponent.path error:error];
    if(!space)return nil;
    if([space[NSFileSystemFreeSize] unsignedLongLongValue]<(16ULL<<30)){AssemblyError(error,@"Installer preparation needs at least 16 GB of available space.");return nil;}
    if(![fm createDirectoryAtURL:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:error])return nil;
    self.job=directory;self.step=0;self.progressGuest=@"mavericks";
    [self report:@"Verifying the Mavericks installer…"];
    NSString *location=[VIMInstallerCatalog fileForGuest:@"mavericks" role:@"esd"][@"URL"];
    if(!location){AssemblyError(error,@"Mavericks has no download record in the installer catalog.");return nil;}
    NSURL *source=[NSURL URLWithString:location];
    if(![VIMInstallerSource verifyFile:esd source:source error:error])return nil;
    NSURL *outer=MountDirectory(error),*base=nil,*app=nil;
    if(!outer)return nil;
    BOOL outerMounted=NO,baseMounted=NO,success=NO;NSDictionary *completedReceipt=nil;
    @try {
        outerMounted=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",outer.path,esd.path] timeout:180 cleanup:NO error:error];
        if(!outerMounted)return nil;
        if(!RegularFile([outer URLByAppendingPathComponent:@"BaseSystem.dmg"])||
           !RegularFile([outer URLByAppendingPathComponent:@"Packages/OSInstall.mpkg"])) {
            AssemblyError(error,@"The Mavericks download is missing its recovery system or installation packages.");return nil;
        }
        base=MountDirectory(error);if(!base)return nil;
        baseMounted=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",base.path,[outer URLByAppendingPathComponent:@"BaseSystem.dmg"].path] timeout:180 cleanup:NO error:error];
        if(!baseMounted)return nil;
        NSDictionary *version=[NSDictionary dictionaryWithContentsOfURL:[base URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
        if(!InstallerVersionMatches(version[@"ProductVersion"],@"mavericks")) {
            AssemblyError(error,@"The downloaded recovery system is not OS X Mavericks.");return nil;
        }
        app=[directory URLByAppendingPathComponent:@"Install OS X Mavericks.app"];
        [self report:@"Preparing the original Mavericks installer…"];[self nextWorkTo:.2 seconds:10];
        if(![self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",[base URLByAppendingPathComponent:app.lastPathComponent].path,app.path] timeout:300 cleanup:NO error:error])return nil;
        NSURL *support=[app URLByAppendingPathComponent:@"Contents/SharedSupport"];
        if(![fm createDirectoryAtURL:support withIntermediateDirectories:YES attributes:nil error:error])return nil;
        [self nextWorkTo:.43 seconds:90];
        if(![self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",esd.path,[support URLByAppendingPathComponent:@"InstallESD.dmg"].path] timeout:1800 cleanup:NO error:error])return nil;
        for(NSString *file in @[@"BaseSystem.dmg",@"BaseSystem.chunklist"])
            if(![fm copyItemAtURL:[outer URLByAppendingPathComponent:file] toURL:[support URLByAppendingPathComponent:file] error:error])return nil;
        NSDictionary *metadata=@{@"System Image Info":@{@"version":version[@"ProductVersion"]}};
        NSData *data=[NSPropertyListSerialization dataWithPropertyList:metadata format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
        if(!data||![data writeToURL:[support URLByAppendingPathComponent:@"InstallInfo.plist"] options:NSDataWritingAtomic error:error]||
           ![self.class validateInstallerApplication:app guest:@"mavericks" error:error])return nil;
        completedReceipt=@{@"FormatVersion":@1,@"Guest":@"mavericks",@"Stage":@"assembled",@"Installer":app.lastPathComponent,
            @"ProductVersion":version[@"ProductVersion"],@"ProductBuildVersion":version[@"ProductBuildVersion"],@"RecoveryPayloadPinned":@YES};
        success=YES;
    } @finally {
        for(NSURL *mount in base?@[base,outer]:@[outer]) {
            // Each successfully attached image is detached exactly once.
            BOOL mounted=mount==base?baseMounted:outerMounted;
            if(mounted){
                NSError *issue=nil;
                if(![self run:@"/usr/bin/hdiutil" arguments:@[@"detach",mount.path] timeout:60 cleanup:YES error:&issue]){success=NO;if(error&&!*error)*error=issue;}
                else rmdir(mount.fileSystemRepresentation);
            } else rmdir(mount.fileSystemRepresentation);
            if(mount==outer)outerMounted=NO;
        }
    }
    if(success){
        NSData *data=[NSPropertyListSerialization dataWithPropertyList:completedReceipt format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
        if(!data||![data writeToURL:[directory URLByAppendingPathComponent:@"Assembly.plist"] options:NSDataWritingAtomic error:error])return nil;
        [self completedWork:.45];return app;
    }return nil;
}
- (NSURL *)assembleGuest:(NSString *)guest files:(NSDictionary<NSString *,NSURL *> *)files
    jobDirectory:(NSURL *)directory error:(NSError **)error {
    if(NSThread.isMainThread) { AssemblyError(error,@"Installer assembly must run on a background worker.");return nil; }
    // A restore DVD is already a complete bootable installation system. It
    // has no InstallAssistant to manufacture or extract. Keep the completed
    // original until its generated replacement has been verified.
    if([@[@"tiger",@"leopard",@"snowleopard"] containsObject:guest]) {
        if(!NonemptyFile(files[@"diskimage"])) {AssemblyError(error,@"Choose the complete Mac OS X restore DVD image.");return nil;}
        return files[@"diskimage"];
    }
    if([guest isEqual:@"mavericks"])return [self assembleMavericks:files[@"esd"] jobDirectory:directory error:error];
    if([@[@"lion",@"mountainlion",@"yosemite",@"elcapitan",@"sierra"] containsObject:guest])return [self assembleClassicDiskImage:files[@"diskimage"] guest:guest jobDirectory:directory error:error];
    NSDictionary *names=@{@"highsierra":@"Install macOS High Sierra.app",@"mojave":@"Install macOS Mojave.app",@"catalina":@"Install macOS Catalina.app",@"bigsur":@"Install macOS Big Sur.app"};
    NSString *name=names[guest];BOOL splitInstaller=![guest isEqual:@"bigsur"];
    if(!name||!directory.isFileURL||!RegularFile(files[@"assistant"])||
       (splitInstaller&&(!RegularFile(files[@"recovery"])||!RegularFile(files[@"esd"])))) {
        AssemblyError(error,@"The installer inputs are missing or unsupported.");return nil;
    }
    NSFileManager *fm=NSFileManager.defaultManager;
    NSDictionary *space=[fm attributesOfFileSystemForPath:directory.URLByDeletingLastPathComponent.path error:error];
    if(!space)return nil;
    if([space[NSFileSystemFreeSize] unsignedLongLongValue]<16ULL*1024*1024*1024) {
        AssemblyError(error,@"Installer preparation needs at least 16 GB of available scratch space.");return nil;
    }
    // A fresh directory is the ownership boundary; never reuse or clear user paths.
    if([fm fileExistsAtPath:directory.path]||![fm createDirectoryAtURL:directory withIntermediateDirectories:NO
        attributes:@{NSFilePosixPermissions:@0700} error:error]) {
        if(error&&!*error)AssemblyError(error,@"Choose a new installer preparation job directory.");return nil;
    }
    self.job=directory;self.step=0;self.progressGuest=guest;
    [self report:@"Checking the Apple installer package…"];
    [self nextWorkTo:.05 seconds:5];
    if(![self run:@"/usr/sbin/pkgutil" arguments:@[@"--check-signature",files[@"assistant"].path] timeout:300 cleanup:NO error:error])return nil;
    // A valid third-party Developer ID is not an Apple OS installer signature.
    // pkgutil is pinned to the system binary and its output locale above is fixed.
    NSString *signature=[NSString stringWithContentsOfURL:self.lastLog encoding:NSUTF8StringEncoding error:error];
    BOOL apple=NO;
    for(NSString *line in [signature componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet])
        if([[line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] isEqual:@"Status: signed Apple Software"])apple=YES;
    if(!apple){AssemblyError(error,@"The package is not identified as signed Apple Software. It was not extracted.");return nil;}
    NSURL *expanded=[directory URLByAppendingPathComponent:@"Expanded"];
    [self completedWork:.05];
    [self report:@"Unpacking the installer…"];
    [self nextWorkTo:.30 seconds:splitInstaller?45:180];
    if(![self run:@"/usr/sbin/pkgutil" arguments:@[@"--expand-full",files[@"assistant"].path,expanded.path] timeout:1800 cleanup:NO error:error])return nil;
    // Apple packages may be a single component or a product archive containing components.
    NSMutableArray<NSURL *> *candidates=[NSMutableArray array];
    NSDirectoryEnumerator *enumerator=[fm enumeratorAtURL:expanded includingPropertiesForKeys:@[NSURLIsSymbolicLinkKey]
        options:0 errorHandler:^BOOL(NSURL *url,NSError *issue){return NO;}];
    for(NSURL *URL in enumerator) {
        NSNumber *link=nil;[URL getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:nil];
        if(link.boolValue){[enumerator skipDescendants];continue;}
        if([URL.lastPathComponent isEqual:name]){[candidates addObject:URL];[enumerator skipDescendants];}
    }
    if(candidates.count!=1){AssemblyError(error,@"The package does not contain exactly one expected OS installer.");return nil;}
    NSURL *app=[directory URLByAppendingPathComponent:name];
    if(![fm moveItemAtURL:candidates.firstObject toURL:app error:error])return nil;
    NSURL *support=[app URLByAppendingPathComponent:@"Contents/SharedSupport" isDirectory:YES];
    if(![fm createDirectoryAtURL:support withIntermediateDirectories:YES attributes:nil error:error])return nil;
    // All package members we need now live in the app. This may itself contain
    // several GB of unused payload; never carry it through media conversion.
    if(![fm removeItemAtURL:expanded error:error])return nil;
    if(!splitInstaller) {
        // Apple's link_shared_support.zsh links PACKAGE_PATH itself here, not
        // the inner SharedSupport.dmg archive member. The outer file is a
        // signed XAR/UDIF hybrid: recovery can boot the inner image, but the
        // installer cannot authenticate it and reports a damaged installer.
        // Copy the already verified original byte-for-byte, without running
        // package scripts or linking it to a download we subsequently retire.
        NSURL *destination=[support URLByAppendingPathComponent:@"SharedSupport.dmg"];
        if([fm fileExistsAtPath:destination.path]&&![fm removeItemAtURL:destination error:error])return nil;
        [self report:@"Adding Apple’s signed installation package…"];
        [self completedWork:.30];[self nextWorkTo:.43 seconds:120];
        if(![self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",files[@"assistant"].path,destination.path]
            timeout:1800 cleanup:NO error:error])return nil;
    }
    if(splitInstaller) {
        [self completedWork:.30];
        [self report:@"Adding recovery and installation files…"];
        [self nextWorkTo:.32 seconds:10];
        NSURL *mount=MountDirectory(error);
        if(!mount)return nil;
        BOOL attached=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",mount.path,files[@"recovery"].path]
            timeout:180 cleanup:NO error:error];
        BOOL copied=attached;
        if(attached) {
            for(NSString *file in @[@"BaseSystem.dmg",@"BaseSystem.chunklist",@"AppleDiagnostics.dmg",@"AppleDiagnostics.chunklist"]) {
                NSURL *source=[mount URLByAppendingPathComponent:file];
                if(self.cancelled||!RegularFile(source)){copied=AssemblyError(error,@"Recovery files are missing, or preparation was cancelled.");break;}
                if(![fm copyItemAtURL:source toURL:[support URLByAppendingPathComponent:file] error:error]){copied=NO;break;}
            }
        }
        // Also attempt cleanup on a timed-out attach, which can have mounted before exiting.
        NSError *detachError=nil;
        BOOL detached=[self run:@"/usr/bin/hdiutil" arguments:@[@"detach",mount.path] timeout:60 cleanup:YES error:&detachError];
        if(detached)rmdir(mount.fileSystemRepresentation);
        if(!copied)return nil;
        if(!detached){if(error)*error=detachError;return nil;}
        if(self.cancelled){AssemblyError(error,@"Installer preparation was cancelled. Downloads were retained.");return nil;}
        // This multi-GB copy must honor Cancel/Quit, unlike a blocking
        // NSFileManager copy. Use the same bounded child lifecycle as unpacking.
        NSURL *esdDestination=[support URLByAppendingPathComponent:@"InstallESD.dmg"];
        if([fm fileExistsAtPath:esdDestination.path]) {
            AssemblyError(error,@"The assembled installer already contains an unexpected ESD image.");return nil;
        }
        [self report:@"Copying macOS installation data…"];
        [self completedWork:.36];
        [self nextWorkTo:.43 seconds:75];
        if(![self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",files[@"esd"].path,esdDestination.path]
            timeout:1800 cleanup:NO error:error])return nil;
    }
    NSString *payload=splitInstaller?@"InstallESD.dmg":@"SharedSupport.dmg";
    if(![self.class validateInstallerApplication:app guest:guest error:error])return nil;
    [self nextWorkTo:.45 seconds:3];
    if(![self run:@"/usr/bin/hdiutil" arguments:@[@"imageinfo",@"-plist",[support URLByAppendingPathComponent:payload].path]
        timeout:60 cleanup:NO error:error])return nil;
    NSDictionary *receipt=@{@"FormatVersion":@1,@"Guest":guest,@"Stage":@"assembled",@"Installer":name};
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:receipt format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    if(!data||![data writeToURL:[directory URLByAppendingPathComponent:@"Assembly.plist"] options:NSDataWritingAtomic error:error])return nil;
    [self report:@"Installer assembled. Bootable media preparation is next."];
    [self completedWork:.45];
    return app;
}
- (NSURL *)createRecoveryMediaFromInstaller:(NSURL *)installer guest:(NSString *)guest jobDirectory:(NSURL *)directory error:(NSError **)error {
    if(NSThread.isMainThread){AssemblyError(error,@"Media preparation must run on a background worker.");return nil;}
    BOOL tiger=[guest isEqual:@"tiger"],snow=[guest isEqual:@"snowleopard"],leopard=[guest isEqual:@"leopard"],restoreDVD=tiger||snow||leopard;
    if(!restoreDVD&&![self.class validateInstallerApplication:installer guest:guest error:error])return nil;
    BOOL classic=restoreDVD||[@[@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan",@"sierra",@"highsierra"] containsObject:guest];
    // Tiger uses its measured native Darwin8 rc.cdrom/minstall handoff, not
    // the Darwin9/x64 bootstrap or scoped Installer injection below.
    BOOL bootstrap=classic&&!tiger;
    NSURL *openGLSupport=self.openGLSupportDirectory?:[NSBundle.mainBundle.resourceURL URLByAppendingPathComponent:
        [@"Runtime/OpenGLGuests/" stringByAppendingString:guest]];
    if(bootstrap&&!VerifyOpenGLSupport(openGLSupport,guest,error))return nil;
    if(tiger&&(!VIMTigerControlPayload(openGLSupport,error)||!VIMTigerInstallerGraphics(openGLSupport,error)||!VIMTigerInstallerAudio(openGLSupport,error)))return nil;
    NSString *name=@{@"tiger":@"Install Mac OS X.app",@"leopard":@"Install Mac OS X.app",@"snowleopard":@"Install Mac OS X.app",@"lion":@"Install Mac OS X Lion.app",@"mountainlion":@"Install OS X Mountain Lion.app",@"mavericks":@"Install OS X Mavericks.app",@"yosemite":@"Install OS X Yosemite.app",@"elcapitan":@"Install OS X El Capitan.app",@"sierra":@"Install macOS Sierra.app",@"highsierra":@"Install macOS High Sierra.app",@"mojave":@"Install macOS Mojave.app",@"catalina":@"Install macOS Catalina.app"}[guest];
    NSURL *base=restoreDVD?installer:[installer URLByAppendingPathComponent:@"Contents/SharedSupport/BaseSystem.dmg"];
    NSURL *esd=restoreDVD?installer:[installer URLByAppendingPathComponent:@"Contents/SharedSupport/InstallESD.dmg"];
    if(!name||(!restoreDVD&&![installer.pathExtension.lowercaseString isEqual:@"app"])||!directory.isFileURL||(!classic&&!RegularFile(base))||!RegularFile(esd)) {
        AssemblyError(error,@"Choose a complete supported installer and a new media job directory.");return nil;
    }
    NSFileManager *fm=NSFileManager.defaultManager;
    NSDictionary *space=[fm attributesOfFileSystemForPath:directory.URLByDeletingLastPathComponent.path error:error];
    if(!space)return nil;
    if([space[NSFileSystemFreeSize] unsignedLongLongValue]<24ULL*1024*1024*1024) {
        AssemblyError(error,@"Media preparation needs at least 24 GB of available scratch space.");return nil;
    }
    if([fm fileExistsAtPath:directory.path]){AssemblyError(error,@"Media preparation requires a new job directory.");return nil;}
    if(![fm createDirectoryAtURL:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:error])return nil;
    self.job=directory;self.step=0;self.progressGuest=guest;
    if(classic&&!RegularFile(base)) {
        // Original old installers keep BaseSystem inside InstallESD. Extract
        // into OUR job, never add files to a user-selected installer app.
        NSURL *sourceMount=MountDirectory(error);if(!sourceMount)return nil;
        BOOL mounted=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",sourceMount.path,esd.path] timeout:180 cleanup:NO error:error];
        BOOL copied=mounted;
        base=[directory URLByAppendingPathComponent:@"Original-BaseSystem.dmg"];
        if(mounted)copied=[fm copyItemAtURL:[sourceMount URLByAppendingPathComponent:@"BaseSystem.dmg"] toURL:base error:error];
        NSError *issue=nil;BOOL detached=[self run:@"/usr/bin/hdiutil" arguments:@[@"detach",sourceMount.path] timeout:60 cleanup:YES error:&issue];
        if(detached)rmdir(sourceMount.fileSystemRepresentation);
        if(!copied||!detached){if(copied&&error)*error=issue;return nil;}
    }
    NSURL *mount=MountDirectory(error);if(!mount)return nil;
    // Keep HFS metadata and symlinks inside an image even when the library is
    // on exFAT. A directory copy on that filesystem cannot represent BaseSystem.
    BOOL modernImages=NSProcessInfo.processInfo.operatingSystemVersion.majorVersion>=27;
    // UDRW conversion fails with ftruncate(EINVAL) on exFAT. Banded UDSB
    // avoids that host filesystem limitation, preserving the exact HFS catalog.
    NSURL *working=[directory URLByAppendingPathComponent:modernImages?@"Recovery-working.img":@"Recovery-working.sparsebundle"];
    [self report:@"Reading installer recovery system…"];
    NSString *imageTool=modernImages?@"/usr/sbin/diskutil":@"/usr/bin/hdiutil";
    NSArray *convert=modernImages?@[@"image",@"create",@"from",@"--format",@"RAW",base.path,working.path]:
        @[@"convert",@"-format",@"UDSB",@"-o",working.path,base.path];
    NSString *capacity=restoreDVD?@"10g":classic?@"8g":@"16g";
    NSArray *resize=modernImages?@[@"image",@"resize",@"--size",capacity,working.path]:@[@"resize",@"-size",capacity,working.path];
    [self completedWork:.45];[self nextWorkTo:.56 seconds:25];
    BOOL converted=NO;
    if(leopard){
        // 9J3050 is an APM/ISO hybrid, unlike Snow's naked HFS volume. Copy
        // its actual HFS partition, not Boot Camp/partition-map bytes. Source
        // devices of our own mounted image are readable by this app's UID.
        NSURL *original=MountDirectory(error);if(!original)return nil;
        BOOL sourceAttached=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-owners",@"off",@"-nobrowse",@"-noautoopen",@"-mountpoint",original.path,base.path] timeout:180 cleanup:NO error:error];
        if(sourceAttached&&LeopardRestoreProfile(original,error)&&
           [self run:@"/usr/sbin/diskutil" arguments:@[@"info",@"-plist",original.path] timeout:60 cleanup:NO error:error]){
            NSDictionary *info=VIMToolDictionary(self.lastLog);NSString *device=info[@"DeviceNode"];
            NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"^/dev/disk[0-9]+s[0-9]+$" options:0 error:nil];
            if([device isKindOfClass:NSString.class]&&[pattern numberOfMatchesInString:device options:0 range:NSMakeRange(0,device.length)]==1){
                if(modernImages){
                    // The block node is exclusively owned by the mounted
                    // filesystem. Use that same image partition's raw node
                    // for read-only copying, never unmount a user's disk.
                    NSString *raw=[device stringByReplacingCharactersInRange:NSMakeRange(5,0) withString:@"r"];
                    converted=VIMCopyHFSInstallerVolume(raw.fileSystemRepresentation,working.fileSystemRepresentation,HFSExtractionProgress,(__bridge void *)self)==0;
                    if(!converted)AssemblyError(error,self.cancelled?@"Installer preparation was cancelled. Your download is saved.":@"The Leopard installation volume couldn’t be copied. Check the library drive and try again; the original DVD is unchanged.");
                }else converted=[self run:@"/usr/bin/hdiutil" arguments:@[@"create",@"-srcdevice",device,@"-format",@"UDSB",@"-o",working.path] timeout:600 cleanup:NO error:error];
            }else AssemblyError(error,@"The Leopard DVD’s installation partition couldn’t be identified safely. Its original image was not changed.");
        }
        if(sourceAttached){NSError *issue=nil;if(![self run:@"/usr/bin/hdiutil" arguments:@[@"detach",original.path] timeout:60 cleanup:YES error:&issue]){converted=NO;if(error)*error=issue;}}
        rmdir(original.fileSystemRepresentation);
    }else converted=[self run:imageTool arguments:convert timeout:600 cleanup:NO error:error];
    if(!converted){rmdir(mount.fileSystemRepresentation);return nil;}
    BOOL volumeOnly=leopard&&modernImages;
    if(modernImages&&RawApplePartitionMap(working)) {
        // macOS27's image resize supports GPT or a naked filesystem, not APM.
        // Resizing APM can grow the container, then fail with the HFS volume
        // unchanged. Normalize only that format, not a particular OS/model.
        if(![self extractWorkingHFSVolume:working error:error]){rmdir(mount.fileSystemRepresentation);return nil;}
        volumeOnly=YES;
    }
    BOOL partitionedInstaller=restoreDVD||volumeOnly;
    [self nextWorkTo:.60 seconds:8];
    if(![self run:imageTool arguments:resize timeout:180 cleanup:NO error:error]){rmdir(mount.fileSystemRepresentation);return nil;}
    [self nextWorkTo:.65 seconds:10];
    NSMutableArray *attachArguments=[@[@"attach",@"-owners",@"off",@"-nobrowse",@"-noautoopen",@"-mountpoint",mount.path] mutableCopy];
    // A source-device volume copy is intentionally plain HFS, not UDIF.
    // Supply its known raw class rather than guess it from a filename suffix.
    if(volumeOnly)[attachArguments addObjectsFromArray:@[@"-imagekey",@"diskimage-class=CRawDiskImage"]];
    [attachArguments addObject:working.path];
    BOOL attached=[self run:@"/usr/bin/hdiutil" arguments:attachArguments timeout:180 cleanup:NO error:error];
    BOOL copied=NO;NSDictionary *version=nil,*tigerPolicy=nil,*tigerControls=nil;
    if(attached) {
        version=[NSDictionary dictionaryWithContentsOfURL:[mount URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
        if(InstallerVersionMatches(version[@"ProductVersion"],guest)&&
           RegularFile([mount URLByAppendingPathComponent:@"System/Library/CoreServices/boot.efi"]))
        {
            if(classic) {
                // This generation boots the genuine CDIS installer against a
                // local Packages tree. A modern InstallAssistant app copy does
                // not supply that tree and cannot replace this native layout.
                NSURL *packages=[mount URLByAppendingPathComponent:@"System/Installation/Packages"];
                struct stat info;
                BOOL removed=restoreDVD||lstat(packages.fileSystemRepresentation,&info)!=0||[fm removeItemAtURL:packages error:error];
                BOOL imageInstaller=[guest isEqual:@"highsierra"];
                NSURL *sourceMount=restoreDVD||imageInstaller?nil:MountDirectory(error);
                BOOL sourceAttached=sourceMount&&[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",sourceMount.path,esd.path] timeout:180 cleanup:NO error:error];
                if(snow)copied=[fm fileExistsAtPath:[packages URLByAppendingPathComponent:@"OSInstall.mpkg"].path]&&[self removeRestoreModelRestrictionAtMount:mount error:error];
                if(leopard)copied=LeopardRestoreProfile(mount,error)&&[self prepareLeopardRecoveryMemoryAtMount:mount error:error]&&[self removeRestoreModelRestrictionAtMount:mount error:error];
                if(tiger&&VIMTigerValidateRestoreDVD(mount,error)) {
                    NSURL *distribution=[packages URLByAppendingPathComponent:@"OSInstall.mpkg/Contents/OSInstall.dist"];
                    NSData *prepared=VIMTigerPrepareDiscOneDistribution([NSData dataWithContentsOfURL:distribution],&tigerPolicy,error);
                    copied=prepared&&[prepared writeToURL:distribution options:NSDataWritingAtomic error:error];
                    if(copied)copied=VIMTigerStageInstallerControls(mount,openGLSupport,&tigerControls,error);
                }
                if(removed&&imageInstaller){
                    // Native17's InstallInfo reader resolves the original
                    // payload/recovery images together. A flattened Packages
                    // tree loads the distribution but leaves image paths nil.
                    // Preserve the genuine metadata and images, not a donor
                    // app or an edited/synthetic Apple package distribution.
                    [self report:@"Adding macOS installation images…"];[self nextWorkTo:.87 seconds:100];
                    copied=[self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",
                        [installer URLByAppendingPathComponent:@"Contents/SharedSupport"].path,mount.path] timeout:1800 cleanup:NO error:error];
                }else if(removed&&sourceAttached) {
                    [self report:@"Adding OS X installation packages…"];[self nextWorkTo:.87 seconds:100];
                    copied=[self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",[sourceMount URLByAppendingPathComponent:@"Packages"].path,packages.path] timeout:1800 cleanup:NO error:error];
                    // Yosemite660's recovery element resolves BaseSystem from
                    // the enclosing distribution volume's ROOT on flat media.
                    // A file only under System/Installation yields false with
                    // errno0, shown by the installer as "Undefined error:0".
                    NSString *recoveryDirectory=NativeAutomatedRecovery(guest)?@"":@"System/Installation";
                    for(NSString *file in @[@"BaseSystem.dmg",@"BaseSystem.chunklist"])if(copied) {
                        // High Sierra's genuine three-package installer keeps
                        // recovery beside InstallESD in SharedSupport, not
                        // inside InstallESD as earlier releases do. Use those
                        // native siblings rather than omit the recovery image.
                        NSURL *recovery=[sourceMount URLByAppendingPathComponent:file];
                        if(!RegularFile(recovery))recovery=[[installer URLByAppendingPathComponent:@"Contents/SharedSupport"] URLByAppendingPathComponent:file];
                        if(RegularFile(recovery))copied=[fm copyItemAtURL:recovery toURL:[mount URLByAppendingPathComponent:[recoveryDirectory stringByAppendingPathComponent:file]] error:error];
                    }
                }
                if(copied&&bootstrap) {
                    if([guest isEqual:@"lion"])copied=[self prepareLionRecoveryMemoryAtMount:mount error:error];
                    NSURL *extras=[packages URLByAppendingPathComponent:@"Extras"],*support=[extras URLByAppendingPathComponent:@"VirtualIntelMac"];
                    copied=copied&&[fm createDirectoryAtURL:extras withIntermediateDirectories:YES attributes:nil error:error]&&
                        [self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",openGLSupport.path,support.path] timeout:180 cleanup:NO error:error];
                    if(copied&&NativeAutomatedRecovery(guest)){
                        NSURL *early=[mount URLByAppendingPathComponent:@"etc/rc.cdrom.local"];
                        NSString *launcher=@"#!/bin/sh\nexec /System/Installation/Packages/Extras/VirtualIntelMac/opengl-install-bootstrap --start-recovery-agent\n";
                        copied=![fm fileExistsAtPath:early.path]&&[launcher writeToURL:early atomically:NO encoding:NSUTF8StringEncoding error:error]&&
                            [fm setAttributes:@{NSFilePosixPermissions:@0755} ofItemAtPath:early.path error:error];
                    }
                    if(copied&&[guest isEqual:@"highsierra"]){
                        // Native17G66 reaches disk discovery but never opens
                        // the -f minstall document. Its AppKit finishLaunching
                        // explicitly handles -NSOpen. Our compiled wrapper
                        // adds it only to LCA's child, retaining -f for LCA and
                        // preserving the signed installer/PID supervision.
                        NSURL *rc=[mount URLByAppendingPathComponent:@"etc/rc.install"];
                        NSString *original=[NSString stringWithContentsOfURL:rc encoding:NSUTF8StringEncoding error:error];
                        NSString *argument=@"\"${LAUNCH}\" \"${TARGET_APP}\"";
                        NSString *package=@"INSTALL_PKG=/System/Installation/Packages/OSInstall.mpkg";
                        if([original componentsSeparatedByString:argument].count!=2||[original componentsSeparatedByString:package].count!=2){
                            AssemblyError(error,@"This High Sierra installer’s startup interface is not recognized. Its original files were retained.");copied=NO;
                        }else{
                            NSString *patched=[original stringByReplacingOccurrencesOfString:argument withString:@"\"${LAUNCH}\" /System/Installation/Packages/Extras/VirtualIntelMac/opengl-installer-launch \"${TARGET_APP}\""];
                            patched=[patched stringByReplacingOccurrencesOfString:package withString:@"INSTALL_PKG=/InstallInfo.plist"];
                            NSNumber *mode=[fm attributesOfItemAtPath:rc.path error:error][NSFilePosixPermissions];
                            copied=mode&&[fm setAttributes:@{NSFilePosixPermissions:@(mode.unsignedIntValue|0200)} ofItemAtPath:rc.path error:error]&&
                                [patched writeToURL:rc atomically:NO encoding:NSUTF8StringEncoding error:error];
                            NSError *restore=nil;
                            if(mode&&![fm setAttributes:@{NSFilePosixPermissions:mode} ofItemAtPath:rc.path error:&restore]){
                                copied=NO;if(error)*error=restore;
                            }
                        }
                    }
                    if(copied)copied=[fm createSymbolicLinkAtURL:[extras URLByAppendingPathComponent:@"rc.imaging"]
                        withDestinationURL:[NSURL fileURLWithPath:@"/System/Installation/Packages/Extras/VirtualIntelMac/opengl-install-bootstrap"] error:error]&&
                        [fm createSymbolicLinkAtURL:[extras URLByAppendingPathComponent:@"minstallconfig.xml"]
                        withDestinationURL:[NSURL fileURLWithPath:@"/var/tmp/VirtualIntelMac-minstallconfig.plist"] error:error];
                    if(copied&&restoreDVD) {
                        // The measured restore DVD predates rc.imaging. Its
                        // native pre-WindowServer hook runs once as guest root.
                        copied=[fm createSymbolicLinkAtURL:[mount URLByAppendingPathComponent:@"etc/rc.cdrom.local"]
                            withDestinationURL:[NSURL fileURLWithPath:@"/System/Installation/Packages/Extras/VirtualIntelMac/opengl-install-bootstrap"] error:error]&&
                            [fm createSymbolicLinkAtURL:[mount URLByAppendingPathComponent:@"etc/minstallconfig.xml"]
                            withDestinationURL:[NSURL fileURLWithPath:@"/var/tmp/VirtualIntelMac-minstallconfig.plist"] error:error];
                        if(copied)copied=
                            // Both actual LanguageChooser-31 (Leopard) and
                            // LanguageChooser-62 (Snow) initialize check this
                            // path, selects its first (English) row and calls
                            // finalizeLanguage:. The target only exists after
                            // our root bootstrap authorizes automatic setup;
                            // manual installs retain the normal language picker.
                            [fm createSymbolicLinkAtURL:[mount URLByAppendingPathComponent:@"System/Installation/CDIS/auto-pilot"]
                            withDestinationURL:[NSURL fileURLWithPath:@"/var/tmp/VirtualIntelMac-InstallerAuthorization.plist"] error:error];
                    }
                    // Yosemite660's OSIWindowController::start natively
                    // selects options.target and enters the installing pane
                    // when isAutomated is true (confirmed by decompilation).
                    // Its stock rc.install already launches LCA with -f.
                    // Keep the signed installer and all original entitlements;
                    // the earlier OSIPreInstallPanel adapter is unnecessary.
                    if(copied&&!NativeAutomatedRecovery(guest)){
                        NSURL *rc=[mount URLByAppendingPathComponent:@"etc/rc.install"];
                        NSString *original=[NSString stringWithContentsOfURL:rc encoding:NSUTF8StringEncoding error:error];
                        NSString *invocation=@"    \"${OSINSTALLER}\"                                ";
                        NSArray *pieces=[original componentsSeparatedByString:invocation];
                        if(pieces.count!=2){AssemblyError(error,@"This installer’s automatic launch interface is not recognized. Its original files were retained.");copied=NO;}
                        else {
                            // Apple's minstall branch bypasses Language
                            // Chooser, which owns the native gray background.
                            // Restore that original launcher around our exec
                            // wrapper. The app-owned EFI supplies prev-lang:kbd;
                            // LCA's observed hidden mode therefore chooses that
                            // language without a picker and supervises its child.
                            // The wrapper, not LCA, sets the scoped DYLD variable.
                            NSString *launch=@"    \"${LAUNCH}\" /System/Installation/Packages/Extras/VirtualIntelMac/opengl-installer-launch \"${OSINSTALLER}\" ";
                            NSString *patched=[original stringByReplacingOccurrencesOfString:invocation withString:launch];
                            NSNumber *mode=[fm attributesOfItemAtPath:rc.path error:error][NSFilePosixPermissions];
                            // This is the generated, ownership-disabled recovery
                            // copy, not the source installer/host. Keep its inode
                            // and catalog owner; only temporarily add write.
                            copied=mode&&[fm setAttributes:@{NSFilePosixPermissions:@(mode.unsignedIntValue|0200)} ofItemAtPath:rc.path error:error]&&
                                [patched writeToURL:rc atomically:NO encoding:NSUTF8StringEncoding error:error];
                            NSError *restore=nil;
                            if(mode&&![fm setAttributes:@{NSFilePosixPermissions:mode} ofItemAtPath:rc.path error:&restore]){
                                copied=NO;if(error)*error=restore;
                            }
                        }
                    }
                    if(copied&&!NativeAutomatedRecovery(guest))copied=[self prepareAutomaticInstallerAtMount:mount guest:guest error:error];
                }
                if(sourceMount) {
                    NSError *issue=nil;
                    BOOL clean=[self run:@"/usr/bin/hdiutil" arguments:@[@"detach",sourceMount.path] timeout:60 cleanup:YES error:&issue];
                    if(clean)rmdir(sourceMount.fileSystemRepresentation);
                    if(sourceAttached&&!clean){copied=NO;if(error)*error=issue;}
                }
            } else {
            NSURL *stub=[mount URLByAppendingPathComponent:name];
            BOOL removed=![fm fileExistsAtPath:stub.path]||[fm removeItemAtURL:stub error:error];
            [self report:@"Adding the complete OS installer…"];
            [self completedWork:.65];
            [self nextWorkTo:.87 seconds:100];
            if(removed)copied=[self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",installer.path,stub.path] timeout:1800 cleanup:NO error:error];
            if(copied&&SerialRecovery(guest))copied=[self prepareSerialRecoveryAtMount:mount error:error];
            }
        }
        else AssemblyError(error,@"Recovery system version or boot files do not match the selected macOS.");
    }
    NSError *detachError=nil;
    [self nextWorkTo:.90 seconds:12];
    BOOL detached=[self run:@"/usr/bin/hdiutil" arguments:@[@"detach",mount.path] timeout:60 cleanup:YES error:&detachError];
    if(detached)rmdir(mount.fileSystemRepresentation);
    if(!copied)return nil;
    if(!detached){if(error)*error=detachError;return nil;}
    NSURL *image=[directory URLByAppendingPathComponent:@"Installer.iso"];
    NSURL *volumeImage=partitionedInstaller?[directory URLByAppendingPathComponent:@"Installer-volume.raw"]:image;
    [self report:@"Creating the bootable installer image…"];
    [self completedWork:.9];
    [self nextWorkTo:partitionedInstaller?.96:.99 seconds:80];
    // Preserve BaseSystem's filesystem catalog, including root ownership,
    // hardlinks, ACLs and blessing. makehybrid on an ownership-disabled mount
    // stamps host UID 501 onto launchd jobs, which require system ownership.
    // The historical .iso name is a raw bootable disk image, not an ISO9660 FS.
    if(modernImages) {
        // diskutil created this working image with --format RAW already.
        // Resizing/mounting edited that same disk. QEMU can use it directly:
        // publish by a same-directory rename, not a second 16 GB conversion.
        if(![fm moveItemAtURL:working toURL:volumeImage error:error])return nil;
        [self completedWork:partitionedInstaller?.96:.99];
    } else {
        NSURL *converted=[directory URLByAppendingPathComponent:@"Installer-blocks.cdr"];
        if(![self run:@"/usr/bin/hdiutil" arguments:@[@"convert",working.path,@"-format",@"UDTO",@"-o",converted.path]
            timeout:1800 cleanup:NO error:error]||![fm moveItemAtURL:converted toURL:volumeImage error:error])return nil;
    }
    if(partitionedInstaller) {
        // 10.6 AppleFileSystemDriver matches boot UUIDs only on media with an
        // HFS partition Content Hint. A naked HFS volume boots in EFI but the
        // kernel waits forever for boot-uuid-media. Add real GPT metadata;
        // preserve all volume bytes/UUID and use the existing disk boot path.
        [self report:@"Preparing the installation disk…"];
        if(VIMWrapHFSInstallerDisk(volumeImage.fileSystemRepresentation,image.fileSystemRepresentation,
            HFSInstallerProgress,(__bridge void *)self)) {
            AssemblyError(error,errno==ECANCELED?@"Installer preparation was cancelled. Your download is saved.":
                @"The Mac OS X installation disk couldn’t be prepared. Check available storage and try again; your download is saved.");return nil;
        }
    }
    NSNumber *imageSize=FlushGeneratedImage(image,error);if(!imageSize)return nil;
    if(!PreparedImageHasBootBlocks(image)) {
        AssemblyError(error,@"The installer image couldn’t be saved as a readable disk. Its working files were retained. Check that the library drive is connected, then try preparing it again.");return nil;
    }
    // A successful conversion/expected length is not proof of usable media.
    // Re-open the final blocks before deleting our working copy. In particular,
    // fail closed if a host image-tool/filesystem combination emitted zeros.
    NSURL *readback=MountDirectory(error);if(!readback)return nil;
    BOOL readbackMounted=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",readback.path,image.path] timeout:180 cleanup:NO error:error];
    BOOL readbackValid=NO;
    if(readbackMounted) {
        NSDictionary *actual=[NSDictionary dictionaryWithContentsOfURL:[readback URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
        readbackValid=[actual isEqual:version]&&RegularFile([readback URLByAppendingPathComponent:@"System/Library/CoreServices/boot.efi"])&&
            (!classic||([guest isEqual:@"highsierra"]?
                (RegularFile([readback URLByAppendingPathComponent:@"InstallInfo.plist"])&&RegularFile([readback URLByAppendingPathComponent:@"InstallESD.dmg"])):
                (RegularFile([readback URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg/Contents/Info.plist"])||
                 RegularFile([readback URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg"]))));
        if(readbackValid&&tiger) {
            NSData *distribution=[NSData dataWithContentsOfURL:[readback URLByAppendingPathComponent:@"System/Installation/Packages/OSInstall.mpkg/Contents/OSInstall.dist"]];
            readbackValid=[VIMTigerDistributionSHA256(distribution) isEqual:tigerPolicy[@"PreparedDistributionSHA256"]]&&
                RegularFile([readback URLByAppendingPathComponent:@"Applications/Utilities/Installer.app/Contents/MacOS/Installer"])&&
                VIMTigerVerifyInstallerControls(readback,openGLSupport,tigerControls,error);
        }
        if(readbackValid&&bootstrap)
            readbackValid=RegularFile([readback URLByAppendingPathComponent:[[NSString stringWithUTF8String:VIMOpenGLRecoveryInstallerForGuest(guest.UTF8String)] substringFromIndex:1]])&&
                (NativeAutomatedRecovery(guest)||RegularFile([readback URLByAppendingPathComponent:[[NSString stringWithUTF8String:VIMOpenGLAutomaticInstaller(restoreDVD||[guest isEqual:@"lion"])] substringFromIndex:1]]));
        if(readbackValid&&NativeAutomatedRecovery(guest))
            readbackValid=RegularFile([readback URLByAppendingPathComponent:@"BaseSystem.dmg"])&&RegularFile([readback URLByAppendingPathComponent:@"BaseSystem.chunklist"]);
        if(readbackValid&&SerialRecovery(guest))
            readbackValid=NonemptyFile([readback URLByAppendingPathComponent:@"usr/libexec/guest-recovery-agent"])&&
                [[NSString stringWithContentsOfURL:[readback URLByAppendingPathComponent:@"private/etc/rc.cdrom.local"] encoding:NSUTF8StringEncoding error:error]
                    containsString:@"/usr/libexec/guest-recovery-agent --start-recovery-service"];
        if(readbackValid&&[guest isEqual:@"highsierra"])
            readbackValid=[[NSString stringWithContentsOfURL:[readback URLByAppendingPathComponent:@"etc/rc.install"] encoding:NSUTF8StringEncoding error:error]
                containsString:@"\"${LAUNCH}\" /System/Installation/Packages/Extras/VirtualIntelMac/opengl-installer-launch \"${TARGET_APP}\""];
        NSError *issue=nil;
        BOOL clean=[self run:@"/usr/bin/hdiutil" arguments:@[@"detach",readback.path] timeout:60 cleanup:YES error:&issue];
        if(!clean){if(error)*error=issue;return nil;}
    }
    rmdir(readback.fileSystemRepresentation);
    if(!readbackMounted)return nil;
    if(!readbackValid){AssemblyError(error,@"The prepared installer could not be verified. Its working files were retained; try preparing it again.");return nil;}
    if(partitionedInstaller&&![fm removeItemAtURL:volumeImage error:error])return nil;
    // Only a verified final image can retire the generated working copy.
    if(!modernImages)[fm removeItemAtURL:working error:nil];
    if([base.URLByDeletingLastPathComponent isEqual:directory]&&[base.lastPathComponent isEqual:@"Original-BaseSystem.dmg"]&&![fm removeItemAtURL:base error:error])return nil;
    NSMutableDictionary *receipt=[@{@"FormatVersion":@1,@"Guest":guest,@"Stage":@"media-created",@"Media":image.lastPathComponent,@"Size":imageSize,@"ProductVersion":version[@"ProductVersion"],@"BootValidated":@NO,@"FilesystemMetadataPreserved":@YES,@"ReadbackVerified":@YES,@"LegacyBootstrapVersion":@(bootstrap?OpenGLBootstrapMediaVersion(guest):0)} mutableCopy];
    if(bootstrap)receipt[@"OpenGLSupportVersion"]=@1;
    if(SerialRecovery(guest))receipt[@"SerialRecoveryVersion"]=@2;
    if(NativeAutomatedRecovery(guest))receipt[@"NativeRecoverySystemVersion"]=[guest isEqual:@"highsierra"]?@6:@1;
    if(partitionedInstaller)receipt[@"PartitionedInstaller"]=@YES;
    if(leopard){receipt[@"RestoreHardwareProfile"]=@"MacBookPro5,3";receipt[@"LeopardRecoveryMemoryVersion"]=@1;}
    if(tiger&&tigerPolicy)[receipt addEntriesFromDictionary:tigerPolicy];
    if(tiger&&tigerControls)[receipt addEntriesFromDictionary:tigerControls];
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:receipt format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    if(!data||![data writeToURL:[directory URLByAppendingPathComponent:@"Media.plist"] options:NSDataWritingAtomic error:error])return nil;
    [self completedWork:1];return image;
}
- (NSURL *)createBigSurMediaFromInstaller:(NSURL *)installer jobDirectory:(NSURL *)directory error:(NSError **)error {
    if(NSThread.isMainThread){AssemblyError(error,@"Media preparation must run on a background worker.");return nil;}
    if(![self.class validateInstallerApplication:installer guest:@"bigsur" error:error])return nil;
    NSFileManager *fm=NSFileManager.defaultManager;
    NSURL *support=[installer URLByAppendingPathComponent:@"Contents/SharedSupport/SharedSupport.dmg"];
    if(!directory.isFileURL||![installer.pathExtension.lowercaseString isEqual:@"app"]||!RegularFile(support)) {
        AssemblyError(error,@"Choose an assembled Big Sur installer and a new media job directory.");return nil;
    }
    NSDictionary *space=[fm attributesOfFileSystemForPath:directory.URLByDeletingLastPathComponent.path error:error];
    if(!space)return nil;
    if([space[NSFileSystemFreeSize] unsignedLongLongValue]<32ULL*1024*1024*1024) {
        AssemblyError(error,@"Media preparation needs at least 32 GB of available scratch space.");return nil;
    }
    if([fm fileExistsAtPath:directory.path]){AssemblyError(error,@"Media preparation requires a new job directory.");return nil;}
    if(![fm createDirectoryAtURL:directory withIntermediateDirectories:NO attributes:@{NSFilePosixPermissions:@0700} error:error])return nil;
    self.job=directory;self.step=0;self.progressGuest=@"bigsur";
    [self report:@"Checking Apple’s signed installation package…"];
    if(![self run:@"/usr/sbin/pkgutil" arguments:@[@"--check-signature",support.path] timeout:300 cleanup:NO error:error])return nil;
    NSString *signature=[NSString stringWithContentsOfURL:self.lastLog encoding:NSUTF8StringEncoding error:error];
    BOOL apple=NO;
    for(NSString *line in [signature componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet])
        if([[line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] isEqual:@"Status: signed Apple Software"])apple=YES;
    if(!apple){AssemblyError(error,@"The Big Sur installation package is not identified as signed Apple Software. Choose a complete installer or download it again.");return nil;}
    NSURL *mount=MountDirectory(error);
    if(!mount)return nil;
    NSURL *extracted=[directory URLByAppendingPathComponent:@"BootAssets"];
    NSURL *stage=[directory URLByAppendingPathComponent:@"MediaRoot"];
    for(NSURL *URL in @[extracted,stage])
        if(![fm createDirectoryAtURL:URL withIntermediateDirectories:NO attributes:nil error:error])return nil;
    [self report:@"Reading Big Sur installer boot files…"];
    [self completedWork:.45];[self nextWorkTo:.48 seconds:12];
    BOOL attached=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-readonly",@"-nobrowse",@"-noautoopen",@"-mountpoint",mount.path,support.path]
        timeout:180 cleanup:NO error:error];
    BOOL extractedOK=NO;NSDictionary *asset=nil;
    if(attached) {
        NSURL *assetDirectory=[mount URLByAppendingPathComponent:@"com_apple_MobileAsset_MacSoftwareUpdate"];
        NSDictionary *metadata=[NSDictionary dictionaryWithContentsOfURL:[assetDirectory URLByAppendingPathComponent:@"com_apple_MobileAsset_MacSoftwareUpdate.xml"]];
        NSArray *assets=metadata[@"Assets"];
        if([assets isKindOfClass:NSArray.class]&&assets.count==1&&[assets.firstObject isKindOfClass:NSDictionary.class])asset=assets.firstObject;
        NSMutableArray *archives=[NSMutableArray array];
        for(NSURL *URL in [fm contentsOfDirectoryAtURL:assetDirectory includingPropertiesForKeys:nil options:0 error:nil])
            if([URL.pathExtension isEqual:@"zip"]&&RegularFile(URL))[archives addObject:URL];
        if(archives.count==1&&InstallerVersionMatches(asset[@"OSVersion"],@"bigsur")&&
           [asset[@"Build"] isKindOfClass:NSString.class]) {
            // Exact entries used by Big Sur CIMUtilities, observed in its shipped
            // createinstallmedia.dylib. No OS-wide restore or package scripts.
            NSArray *members=@[@"AssetData/Restore/BaseSystem.dmg",@"AssetData/Restore/BaseSystem.chunklist",
                @"AssetData/boot/Firmware/Manifests/InstallerBoot/*",
                @"AssetData/boot/Firmware/System/Library/CoreServices/bootbase.efi",
                @"AssetData/boot/System/Library/KernelCollections/BootKernelExtensions.kc",
                @"AssetData/boot/BridgeVersion.plist",@"AssetData/boot/BridgeVersion.bin",
                @"AssetData/boot/PlatformSupport.plist",@"AssetData/boot/SystemVersion.plist"];
            NSMutableArray *arguments=[NSMutableArray arrayWithObjects:@"-q",[archives.firstObject path],nil];
            [arguments addObjectsFromArray:members];[arguments addObjectsFromArray:@[@"-d",extracted.path]];
            [self nextWorkTo:.60 seconds:50];
            extractedOK=[self run:@"/usr/bin/unzip" arguments:arguments timeout:600 cleanup:NO error:error];
        } else AssemblyError(error,@"The Big Sur update archive or version metadata is ambiguous or missing.");
    }
    NSError *detachError=nil;
    BOOL detached=[self run:@"/usr/bin/hdiutil" arguments:@[@"detach",mount.path] timeout:60 cleanup:YES error:&detachError];
    if(detached)rmdir(mount.fileSystemRepresentation);
    if(!extractedOK)return nil;
    if(!detached){if(error)*error=detachError;return nil;}
    NSDictionary *mapping=@{
        @"AssetData/Restore/BaseSystem.dmg":@"BaseSystem/BaseSystem.dmg",
        @"AssetData/Restore/BaseSystem.chunklist":@"BaseSystem/BaseSystem.chunklist",
        @"AssetData/boot/Firmware/System/Library/CoreServices/bootbase.efi":@"System/Library/CoreServices/boot.efi",
        @"AssetData/boot/System/Library/KernelCollections/BootKernelExtensions.kc":@"System/Library/KernelCollections/BootKernelExtensions.kc",
        @"AssetData/boot/BridgeVersion.plist":@"System/Library/CoreServices/BridgeVersion.plist",
        @"AssetData/boot/BridgeVersion.bin":@"System/Library/CoreServices/BridgeVersion.bin",
        @"AssetData/boot/PlatformSupport.plist":@"System/Library/CoreServices/PlatformSupport.plist",
        @"AssetData/boot/SystemVersion.plist":@"System/Library/CoreServices/SystemVersion.plist"};
    for(NSString *sourcePath in mapping) {
        if(self.cancelled){AssemblyError(error,@"Media preparation was cancelled. Downloads were retained.");return nil;}
        NSURL *source=[extracted URLByAppendingPathComponent:sourcePath];
        NSURL *target=[stage URLByAppendingPathComponent:mapping[sourcePath]];
        if(!RegularFile(source)){AssemblyError(error,@"A required Big Sur boot file is missing.");return nil;}
        if(![fm createDirectoryAtURL:target.URLByDeletingLastPathComponent withIntermediateDirectories:YES attributes:nil error:error]||
           ![fm moveItemAtURL:source toURL:target error:error])return nil;
    }
    NSURL *manifests=[extracted URLByAppendingPathComponent:@"AssetData/boot/Firmware/Manifests/InstallerBoot"];
    if(![self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",manifests.path,stage.path] timeout:120 cleanup:NO error:error])return nil;
    NSURL *preferences=[stage URLByAppendingPathComponent:@"Library/Preferences/SystemConfiguration"];
    if(![fm createDirectoryAtURL:preferences withIntermediateDirectories:YES attributes:nil error:error])return nil;
    NSDictionary *boot=@{@"Kernel Flags":@"root-dmg=file:///BaseSystem/BaseSystem.dmg"};
    NSDictionary *cookie=@{@"AppName":installer.lastPathComponent,@"ProductVersion":asset[@"OSVersion"],@"ProductBuildVersion":asset[@"Build"]};
    for(NSArray *entry in @[@[boot,[preferences URLByAppendingPathComponent:@"com.apple.Boot.plist"]],@[cookie,[stage URLByAppendingPathComponent:@".IAPhysicalMedia"]]]) {
        NSData *plist=[NSPropertyListSerialization dataWithPropertyList:entry[0] format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
        if(!plist||![plist writeToURL:entry[1] options:NSDataWritingAtomic error:error])return nil;
    }
    [self report:@"Copying macOS installation files into the media…"];
    [self completedWork:.65];
    [self nextWorkTo:.90 seconds:120];
    if(![self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",installer.path,[stage URLByAppendingPathComponent:installer.lastPathComponent].path]
        timeout:1800 cleanup:NO error:error])return nil;
    if(![fm removeItemAtURL:extracted error:error])return nil;
    [self report:@"Creating the bootable installer image…"];
    [self completedWork:.9];
    NSURL *image=[directory URLByAppendingPathComponent:@"Installer.iso"];
    [self nextWorkTo:.99 seconds:120];
    if(![self run:@"/usr/bin/hdiutil" arguments:@[@"makehybrid",@"-hfs",@"-udf",@"-hfs-volume-name",@"Install macOS Big Sur",
        @"-hfs-blessed-directory",[stage URLByAppendingPathComponent:@"System/Library/CoreServices"].path,@"-o",image.path,stage.path]
        timeout:1800 cleanup:NO error:error])return nil;
    NSNumber *imageSize=FlushGeneratedImage(image,error);if(!imageSize)return nil;
    if(!PreparedImageHasBootBlocks(image)) {
        AssemblyError(error,@"The installer image couldn’t be saved as a readable disk. Its working files were retained. Check that the library drive is connected, then try preparing it again.");return nil;
    }
    // Staged files are now inside the image and are never reused.
    if(![fm removeItemAtURL:stage error:error])return nil;
    NSDictionary *receipt=@{@"FormatVersion":@1,@"Guest":@"bigsur",@"Stage":@"media-created",@"Media":@"Installer.iso",
        @"Size":imageSize,@"ProductVersion":asset[@"OSVersion"],@"ProductBuildVersion":asset[@"Build"],@"BootValidated":@NO,@"SignedSharedSupportPackage":@YES};
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:receipt format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    if(!data||![data writeToURL:[directory URLByAppendingPathComponent:@"Media.plist"] options:NSDataWritingAtomic error:error])return nil;
    [self completedWork:1];return image;
}
@end
