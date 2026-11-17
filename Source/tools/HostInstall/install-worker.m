#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonDigest.h>
#import <CommonCrypto/CommonKeyDerivation.h>
#import <Security/Security.h>
#import "VIMHFSCatalog.h"
#import "VIMTigerAccount.h"
#import "VIMTigerKeychain.h"
#import "VIMSMBKeyset.h"
#import "VIMApplePeriodUpdate.h"
#import "VIMPeriodKextcache.h"
#import "VIMCPIOPayload.h"
#import "VIMKextHelperCache.h"
#import "../../compat/VIMHostInstallAccountPolicy.h"
#import "../../compat/VIMFlatHostUpdateRecipes.h"
#import "VIMFlatReceipts.h"
#import "VIMGuestPath.h"
#import "VIMAppleDouble.h"
#import "VIMAPFSFirmlinks.h"
#import "VIMAPFSJumpstart.h"
#import "VIMCatalinaDataLayout.h"
#import "VIMBigSurArchive.h"
#import "VIMBigSurFinalize.h"
#import "VIMBigSurFirmlinks.h"
#import "VIMBigSurFixups.h"
#import "VIMBigSurSealInputs.h"
#import "VIMAssistantCryptex.h"
#import "VIMAssistantSource.h"
#import "../../VirtualIntelMac/VIMGuestFamilies.h"
#import "../../compat/VIMHostInstallSourcePolicy.h"
#import "../../compat/VIMGuestLocale.h"
#import "../../compat/VIMGuestComputerName.h"
#import "../../compat/VIMDiskImagesPolicy.h"
#include "../host-install-pbzx.h"
#include "../guest-opengl-selection.h"
#include "../opengl-support-revision.h"
#include <errno.h>
#include <copyfile.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <sqlite3.h>

static NSString *const HDI=@"/usr/bin/hdiutil";
static NSString *const Support=@"Library/Application Support/Virtual Intel Mac";
static NSString *const Engine=@"System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle/GLEngine";
static NSString *const CoreServices=@"System/Library/CoreServices";
static NSString *const BootEFI=@"System/Library/CoreServices/boot.efi";
static NSString *const StartupCache=@"System/Library/Caches/com.apple.kext.caches/Startup/kernelcache";
static NSString *const PrelinkedCache=@"System/Library/PrelinkedKernels/prelinkedkernel";
// Empty sparse GPT preparation is short and independent of virtual capacity.
// Extraction and compression retain their byte-driven progress fractions.
static const double AssistantDiskStart=.025,AssistantDiskEnd=.04,AssistantPayloadStart=.09;
static const double BigSurCompressionStart=.45,BigSurCompressionEnd=.875;

static BOOL UsesCorePackage(NSString *guest) {
    return [@[@"highsierra",@"mojave",@"catalina"] containsObject:guest];
}

static NSString *SetupState(NSString *guest) {
    if([@[@"tiger",@"leopard"] containsObject:guest])
        return [Support stringByAppendingPathComponent:@"State/LegacySetup"];
    return @"private/var/db/VirtualIntelMac/LegacySetup";
}
static NSArray<NSString *> *Packages(NSString *guest) {
    if([guest isEqual:@"lion"])return @[@"BaseSystemBinaries",@"BaseSystemResources",@"Essentials",@"BootCamp",@"BSD",
        @"PodcastCapture",@"JavaTools",@"RemoteDesktop",@"SIUResources",@"AdditionalEssentials",@"AdditionalSpeechVoices",
        @"AsianLanguagesSupport",@"MediaFiles",@"MigrationAssistant",@"Mail",@"AddressBook",@"iCal",@"Automator",
        @"DVDPlayer",@"iTunes",@"iChat",@"JavaEssentials",@"Safari",@"OxfordDictionaries",@"Japanese",@"German",@"French",
        @"Spanish",@"Italian",@"Dutch",@"Danish",@"Finnish",@"Korean",@"Norwegian",@"Russian",@"Swedish",
        @"BrazilianPortuguese",@"SimplifiedChinese",@"TraditionalChinese",@"Portuguese",@"Polish",@"Arabic",@"Hungarian",
        @"Turkish",@"Czech",@"Thai",@"Catalan",@"Croatian",@"Greek",@"Hebrew",@"Romanian",@"Slovak",@"Ukrainian",@"X11User"];
    if([guest isEqual:@"mountainlion"])return @[@"BaseSystemBinaries",@"BaseSystemResources",@"Essentials",@"BSD",@"JavaTools",
        @"RemoteDesktop",@"AdditionalEssentials",@"AdditionalSpeechVoices",@"AsianLanguagesSupport",@"MediaFiles",
        @"JavaEssentials",@"OxfordDictionaries",@"X11redirect"];
    if([@[@"mavericks",@"yosemite"] containsObject:guest])return @[@"BaseSystemBinaries",@"BaseSystemResources",@"Essentials",@"BSD",@"JavaTools",
        @"AdditionalEssentials",@"AdditionalSpeechVoices",@"AsianLanguagesSupport",@"MediaFiles",
        @"JavaEssentials",@"OxfordDictionaries",@"X11redirect"];
    if([guest isEqual:@"elcapitan"])return @[@"BaseSystemResources",@"Essentials",@"X11redirect"];
    if([guest isEqual:@"sierra"])return @[@"BaseSystemResources",@"Essentials",@"FirmwareUpdate",@"EmbeddedOSFirmware"];
    if(UsesCorePackage(guest))return @[@"Core",@"EmbeddedOSFirmware",@"SecureBoot"];
    if(VIMIsAssistantGuest(guest))return @[@"OriginalSystemPayload"];
    if([guest isEqual:@"snowleopard"])return @[@"BaseSystem",@"Essentials",@"BootCamp",@"BSD",@"iPodSupport",
        @"PodcastCapture",@"JavaTools",@"AdditionalEssentials",@"AdditionalSpeechVoices",
        @"AsianLanguagesSupport",@"MediaFiles",@"MigrationAssistant",@"Mail",@"AddressBook",@"iCal",
        @"Automator",@"DVDPlayer",@"iTunes",@"iChat",@"Java",@"Safari",@"OxfordDictionaries",@"AdditionalFonts",
        @"Japanese",@"German",@"French",@"Spanish",@"Italian",@"Dutch",@"Danish",@"Finnish",@"Korean",
        @"Norwegian",@"Russian",@"Swedish",@"BrazilianPortuguese",@"SimplifiedChinese",@"TraditionalChinese",
        @"Portuguese",@"Polish",@"X11User",@"Rosetta"];
    if([guest isEqual:@"leopard"])return @[@"BaseSystem",@"Essentials",@"BootCamp",@"BSD",@"iPodSupport",@"PodcastCapture",
        @"Directory",@"JavaTools",@"AdditionalEssentials",@"AdditionalSpeechVoices",@"AsianLanguagesSupport",
        @"MediaFiles",@"MigrationAssistant",@"Mail",@"AddressBook",@"iCal",@"Automator",@"DVDPlayer",
        @"iTunes",@"iChat",@"Java",@"Safari",@"OxfordDictionaries",@"BrotherPrinterDrivers",
        @"CanonPrinterDrivers",@"EpsonPrinterDrivers",
        @"HewlettPackardPrinterDrivers",@"LexmarkPrinterDrivers",@"GutenprintPrinterDrivers",
        @"RicohPrinterDrivers",@"XeroxPrinterDrivers",@"FujiXeroxPrinterDrivers",@"SamsungPrinterDrivers",
        @"AdditionalFonts",@"Japanese",@"German",@"French",@"Spanish",@"Italian",@"Dutch",@"Danish",
        @"Finnish",@"Korean",@"Norwegian",@"Russian",@"Swedish",@"BrazilianPortuguese",@"SimplifiedChinese",
        @"TraditionalChinese",@"Portuguese",@"Polish",@"X11User",@"ACL"];
    return @[@"BaseSystem",@"Essentials",@"BSD",@"AdditionalEssentials",@"AdditionalSpeechVoices",
      @"AsianLanguagesSupport",@"MediaFiles",@"MigrationAssistant",@"Mail",@"AddressBook",@"iCal",
      @"Automator",@"iTunes",@"iChat",@"Java",@"Safari",@"OxfordDictionaries",@"BrotherPrinterDrivers",
      @"CanonPrinterDrivers",@"ElectronicsForImagingPrinterDrivers",@"EpsonPrinterDrivers",
      @"HewlettPackardPrinterDrivers",@"LexmarkPrinterDrivers",@"GimpPrintPrinterDrivers",
      @"RicohPrinterDrivers",@"XeroxPrinterDrivers",@"AdditionalFonts",@"Japanese",@"German",@"French",
      @"Spanish",@"Italian",@"Dutch",@"Danish",@"Finnish",@"Korean",@"Norwegian",@"Russian",@"Swedish",
      @"BrazilianPortuguese",@"SimplifiedChinese",@"TraditionalChinese"];
}
static NSURL *At(NSURL *base,NSString *name) {return [base URLByAppendingPathComponent:name];}
static void Stop(NSString *reason) {
    @throw [NSException exceptionWithName:@"HostInstall" reason:reason userInfo:nil];
}
static void StopOutOfSpace(NSString *reason) {
    @throw [NSException exceptionWithName:@"HostInstall" reason:reason userInfo:@{@"ExitStatus":@(ENOSPC)}];
}
static void StopWriteFailure(NSError *error,NSString *fallback) {
    NSString *reason=error.localizedDescription?:fallback;
    for(NSError *cause=error;[cause isKindOfClass:NSError.class];cause=cause.userInfo[NSUnderlyingErrorKey]) {
        if(([cause.domain isEqual:NSPOSIXErrorDomain]&&cause.code==ENOSPC)||
           ([cause.domain isEqual:NSCocoaErrorDomain]&&cause.code==NSFileWriteOutOfSpaceError))StopOutOfSpace(reason);
    }
    Stop(reason);
}
static void Require(BOOL ok,NSString *reason) {if(!ok)Stop(reason);}
#define Check Require
#include "../guest-install-permissions.h"
#undef Check
static NSDictionary *Plist(NSURL *url) {return [NSDictionary dictionaryWithContentsOfURL:url];}
static NSData *Read(NSURL *url) {
    NSError *error=nil;NSData *data=[NSData dataWithContentsOfURL:url options:0 error:&error];
    if(!data)Stop(error.localizedDescription);
    return data;
}
static NSString *HexDigest(const unsigned char hash[CC_SHA256_DIGEST_LENGTH]) {
    NSMutableString *hex=[NSMutableString string];
    for(unsigned i=0;i<CC_SHA256_DIGEST_LENGTH;i++)[hex appendFormat:@"%02x",hash[i]];
    return hex;
}
static NSString *Digest(NSData *data) {
    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.bytes,(CC_LONG)data.length,hash);return HexDigest(hash);
}
static void Directory(NSURL *url) {
    NSError *error=nil;
    BOOL ok=[NSFileManager.defaultManager createDirectoryAtURL:url withIntermediateDirectories:YES attributes:nil error:&error];
    if(!ok)StopWriteFailure(error,@"Could not create an installation folder.");
}
static void Write(NSURL *url,NSData *data) {
    Directory(url.URLByDeletingLastPathComponent);
    NSError *error=nil;BOOL ok=data&&[data writeToURL:url options:NSDataWritingAtomic error:&error];
    if(!ok)StopWriteFailure(error,@"Could not write an installation file.");
}
static void WritePlist(NSURL *url,NSDictionary *plist) {
    NSError *error=nil;
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:plist format:NSPropertyListXMLFormat_v1_0 options:0 error:&error];
    Require(data!=nil,error.localizedDescription);Write(url,data);
}
static void Remove(NSURL *url) {
    NSError *error=nil;BOOL ok=[NSFileManager.defaultManager removeItemAtURL:url error:&error];
    Require(ok,error.localizedDescription);
}
static void RemoveRecoveryWork(NSURL *attempt) {
    // These are optional work files: a base install or unavailable update
    // has no updated Recovery image, and a selected app needs no unpacked one.
    for(NSString *name in @[@"RecoveryHDMeta.dmg",@"UpdatedRecoveryHDMeta.dmg"]) {
        NSError *error=nil;
        BOOL removed=[NSFileManager.defaultManager removeItemAtURL:At(attempt,name) error:&error];
        Require(removed||([error.domain isEqual:NSCocoaErrorDomain]&&error.code==NSFileNoSuchFileError),
                error.localizedDescription);
    }
}
static void Checkpoint(NSURL *attempt) {
    if([NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path])Stop(@"Installation cancelled.");
}
static void Progress(NSURL *attempt,NSString *stage,NSString *title,double start,double end,
                     double fraction,double estimate) {
    NSMutableDictionary *record=[@{@"Stage":stage,@"Title":title,@"Start":@(start),@"End":@(end)} mutableCopy];
    if(fraction>=0)record[@"Fraction"]=@(fraction);
    if(estimate>0)record[@"EstimatedSeconds"]=@(estimate);
    WritePlist(At(attempt,@"Progress.plist"),record);
}
static NSDictionary *RestoreProfile(NSString *guest) {
    return @{
        @"tiger":@{@"Name":@"Tiger",@"Build":@"8R4088"},
        @"leopard":@{@"Name":@"Leopard",@"Build":@"9J3050"},
        @"snowleopard":@{@"Name":@"Snow Leopard",@"Build":@"10J3250"},
        @"lion":@{@"Name":@"Lion",@"Build":@"11G63",@"WrappedESD":@YES},
        @"mountainlion":@{@"Name":@"Mountain Lion",@"Build":@"12F45",@"WrappedESD":@YES},
        @"mavericks":@{@"Name":@"Mavericks",@"Build":@"13F34",@"NestedSystem":@YES},
        @"yosemite":@{@"Name":@"Yosemite",@"Build":@"14F27",@"WrappedESD":@YES,@"NestedSystem":@YES},
        @"elcapitan":@{@"Name":@"El Capitan",@"Build":@"15G31",@"WrappedESD":@YES,@"NestedSystem":@YES,
            @"RecoveryCache":PrelinkedCache,@"ProvidedStartupCache":@YES},
        @"sierra":@{@"Name":@"Sierra",@"Build":@"16G29",@"WrappedESD":@YES,@"NestedSystem":@YES,
            @"InstallerPackage":@"InstallOS.pkg",@"RecoveryCache":PrelinkedCache,@"ProvidedStartupCache":@YES},
        @"highsierra":@{@"Name":@"High Sierra",@"Build":@"17G66",@"APFS":@YES,@"NestedSystem":@YES},
        @"mojave":@{@"Name":@"Mojave",@"Build":@"18G103",@"APFS":@YES,@"NestedSystem":@YES,@"PVG":@YES},
        @"catalina":@{@"Name":@"Catalina",@"Build":@"19H15",@"APFS":@YES,@"NestedSystem":@YES,@"PVG":@YES,@"APFSVolumeGroup":@YES},
        @"bigsur":@{@"Name":@"Big Sur",@"Build":@"20G1443",@"APFS":@YES,@"PVG":@YES},
        @"monterey":@{@"Name":@"Monterey",@"APFS":@YES,@"PVG":@YES},
        @"ventura":@{@"Name":@"Ventura",@"APFS":@YES,@"PVG":@YES},
        @"sonoma":@{@"Name":@"Sonoma",@"APFS":@YES,@"PVG":@YES},
        @"sequoia":@{@"Name":@"Sequoia",@"APFS":@YES,@"PVG":@YES},
        // Development admission only until the original-media first boot passes.
        @"tahoe":@{@"Name":@"Tahoe",@"APFS":@YES,@"PVG":@YES}
    }[guest];
}
static void CheckMedia(NSURL *image,NSURL *attempt,NSString *name,double begin,double end) {
    // The downloader authenticates its transport (and the HTTP Mavericks pin).
    // Admit the actual mounted OS family below, not one historical byte copy.
    Progress(attempt,@"validate",[NSString stringWithFormat:@"Opening the %@ installer…",name],begin,end,-1,1);
    int fd=open(image.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Require(fd>=0,@"Could not read the downloaded installer.");
    @try {
        struct stat st;
        Require(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0,@"The installer file is empty or unreadable.");
    } @finally {close(fd);}
    Checkpoint(attempt);
}
static NSData *Run(NSString *program,NSArray<NSString *> *arguments,NSURL *cwd,BOOL capture) {
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:program];
    task.arguments=arguments;task.currentDirectoryURL=cwd;
    task.environment=@{@"PATH":@"/usr/bin:/bin:/usr/sbin:/sbin",@"LC_ALL":@"C"};
    task.standardInput=NSFileHandle.fileHandleWithNullDevice;
    NSPipe *pipe=capture?[NSPipe pipe]:nil;
    if(pipe)task.standardOutput=pipe;
    NSError *error=nil;BOOL launched=[task launchAndReturnError:&error];Require(launched,error.localizedDescription);
    [pipe.fileHandleForWriting closeFile];
    NSData *data=[pipe.fileHandleForReading readDataToEndOfFile];
    [task waitUntilExit];
    Require(task.terminationStatus==0,[NSString stringWithFormat:@"%@ (%@) exited with status %d.",
        program.lastPathComponent,arguments.firstObject,task.terminationStatus]);
    return data;
}
static NSDictionary *ToolPlist(NSString *program,NSArray<NSString *> *arguments) {
    return [NSPropertyListSerialization propertyListWithData:Run(program,arguments,nil,YES) options:0 format:NULL error:NULL];
}
static void Ditto(NSArray<NSString *> *items) {
    NSArray *args=@[@"--rsrc",@"--extattr",@"--acl",@"--noqtn",@"--nohfsCompression",@"--nopreserveHFSCompression"];
    Run(@"/usr/bin/ditto",[args arrayByAddingObjectsFromArray:items],nil,NO);
}

@interface VIMInstallTarget : NSObject
@property NSURL *root;
@property NSMutableDictionary<NSString *,NSArray<NSNumber *> *> *metadata;
@property NSMutableDictionary<NSString *,NSNumber *> *systemFlags;
@end
@implementation VIMInstallTarget
- (void)directory:(NSString *)path mode:(mode_t)mode {
    if(!path.length||[path isEqual:@"."])return;
    NSURL *url=At(self.root,path);struct stat info;
    if(!lstat(url.fileSystemRepresentation,&info)) {
        Require(S_ISDIR(info.st_mode),[NSString stringWithFormat:@"Expected a directory at %@.",path]);return;
    }
    [self directory:path.stringByDeletingLastPathComponent mode:0755];
    Directory(url);self.metadata[path]=@[@0,@0,@(S_IFDIR|mode)];
}
- (void)file:(NSString *)path data:(NSData *)data mode:(mode_t)mode {
    [self directory:path.stringByDeletingLastPathComponent mode:0755];
    Write(At(self.root,path),data);self.metadata[path]=@[@0,@0,@(S_IFREG|mode)];
}
- (void)plist:(NSString *)path value:(NSDictionary *)value mode:(mode_t)mode {
    [self directory:path.stringByDeletingLastPathComponent mode:0755];
    WritePlist(At(self.root,path),value);self.metadata[path]=@[@0,@0,@(S_IFREG|mode)];
}
- (void)record:(NSString *)relative uid:(uint32_t)uid gid:(uint32_t)gid receipt:(BOOL)receipt {
    NSURL *url=At(self.root,relative);struct stat info;
    Require(!lstat(url.fileSystemRepresentation,&info),[NSString stringWithFormat:@"Missing staged file %@.",relative]);
    mode_t mode=info.st_mode;
    if(receipt&&S_ISDIR(mode))mode=S_IFDIR|0775;
    if(receipt&&S_ISREG(mode))mode|=0664;
    self.metadata[relative]=@[@(uid),@(gid),@(mode)];
    if(S_ISDIR(info.st_mode)) {
        NSError *error=nil;NSArray *children=[NSFileManager.defaultManager contentsOfDirectoryAtPath:url.path error:&error];
        Require(children!=nil,error.localizedDescription);
        for(NSString *name in children)[self record:[relative stringByAppendingPathComponent:name] uid:uid gid:gid receipt:receipt];
    }
}
- (NSDictionary *)catalogPermissions {
    // Open the same private staging directories as the ordinary catalog walk
    // before resolving merged metadata under them. Keep their original modes
    // in the ledger for the detached finalizer.
    for(NSString *path in [self.metadata.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        if(!S_ISDIR([self.metadata[path][2] unsignedShortValue]))continue;
        NSURL *directory=At(self.root,path);struct stat info;
        if(!lstat(directory.fileSystemRepresentation,&info)&&S_ISDIR(info.st_mode)&&!(info.st_mode&S_IXUSR))
            Require(!chmod(directory.fileSystemRepresentation,(info.st_mode&07777)|S_IXUSR),@"Could not read a staged directory.");
    }
    VIMNormalizeAppleDoubleMetadata(self.root,self.metadata);
    NSMutableDictionary *out=[NSMutableDictionary dictionary];
    for(NSString *path in [self.metadata.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        @autoreleasepool {
            NSArray<NSNumber *> *value=self.metadata[path];mode_t mode=value[2].unsignedShortValue;
            NSURL *url=At(self.root,path);struct stat info;
            if(lstat(url.fileSystemRepresentation,&info)) {
                Stop([NSString stringWithFormat:@"Missing installed file %@.",path]);
            }
            if((mode&S_IFMT)!=(info.st_mode&S_IFMT))Stop([NSString stringWithFormat:@"Unexpected type for %@.",path]);
            // Privileged BSD flags are installed only in the detached guest
            // catalog. Applying them to this live mount would require host
            // root/security changes and impede the remaining cache writes.
            NSNumber *flags=self.systemFlags[path];
            if(flags)value=[value arrayByAddingObject:flags];
            NSNumber *cnid=@(info.st_ino);NSArray *existing=out[cnid];
            if(existing&&![existing isEqual:value])Stop([NSString stringWithFormat:@"Conflicting ownership for %@.",path]);
            out[cnid]=value;
        }
    }
    return out;
}
@end

static NSURL *Package(NSURL *media,NSString *name) {
    return At(media,[NSString stringWithFormat:@"System/Installation/Packages/%@.pkg/Contents",name]);
}
static NSDictionary *ReadBOM(NSURL *bom) {
    NSData *data=Run(@"/usr/bin/lsbom",@[@"-p",@"fmug",bom.path],nil,YES);
    NSString *listing=[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    Require(listing.length!=0,@"Could not read an original package BOM.");
    NSMutableDictionary *metadata=[NSMutableDictionary dictionary];
    [listing enumerateLinesUsingBlock:^(NSString *line,__unused BOOL *stop) {
        @autoreleasepool {
            NSArray<NSString *> *fields=[line componentsSeparatedByString:@"\t"];
            Require(fields.count==4,@"Invalid package BOM entry.");
            NSString *path=fields[0];
            if([path hasPrefix:@"./"])path=[path substringFromIndex:2];
            Require(path.length&&!path.isAbsolutePath&&![path.pathComponents containsObject:@".."],@"Invalid package path.");
            metadata[path]=@[@(fields[2].intValue),@(fields[3].intValue),@(strtoul(fields[1].UTF8String,NULL,8))];
        }
    }];
    return metadata;
}
static void ExtractTigerPackages(VIMInstallTarget *target,NSURL *media,NSURL *attempt) {
    NSArray<NSString *> *packages=Packages(@"tiger");
    NSMutableDictionary<NSString *,NSNumber *> *sizes=[NSMutableDictionary dictionary];
    double total=0,done=0;
    for(NSString *name in packages) {
        NSURL *package=Package(media,name);
        // BaseSystem is copied from the DVD filesystem, not a pax archive.
        uint64_t bytes=[name isEqual:@"BaseSystem"]?(512ULL<<20):
            [NSFileManager.defaultManager attributesOfItemAtPath:At(package,@"Archive.pax.gz").path error:nil].fileSize;
        Require(bytes!=0,[NSString stringWithFormat:@"The installer is missing %@.",name]);
        sizes[name]=@(bytes);total+=bytes;
    }
    for(NSString *name in packages) {
        @autoreleasepool {
            Checkpoint(attempt);
            Progress(attempt,[@"package-" stringByAppendingString:name],@"Installing Tiger’s system files…",
                     .04+.72*done/total,.04+.72*(done+sizes[name].doubleValue)/total,-1,
                     MAX(.5,53*sizes[name].doubleValue/total));
            NSURL *package=Package(media,name),*bom=At(package,@"Archive.bom");
            [target.metadata addEntriesFromDictionary:ReadBOM(bom)];
            if([name isEqual:@"BaseSystem"])Ditto(@[@"--bom",bom.path,media.path,target.root.path]);
            else Run(@"/bin/pax",@[@"-r",@"-z",@"-p",@"p",@"-f",At(package,@"Archive.pax.gz").path],target.root,NO);
            done+=sizes[name].doubleValue;
        }
    }
    Progress(attempt,@"receipts",@"Recording the installed packages…",.76,.80,-1,12);
    for(NSString *name in packages) {
        Checkpoint(attempt);
        NSString *receipt=[NSString stringWithFormat:@"Library/Receipts/%@.pkg",name];
        [target directory:[receipt stringByAppendingPathComponent:@"Contents"] mode:0755];
        for(NSString *leaf in @[@"Archive.bom",@"Info.plist",@"version.plist",@"Resources"])
            Ditto(@[At(Package(media,name),leaf).path,At(At(target.root,receipt),[@"Contents" stringByAppendingPathComponent:leaf]).path]);
        [target record:receipt uid:0 gid:80 receipt:YES];
    }
    Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
    target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
}

static void TigerCollectCertificates(VIMInstallTarget *target,NSDictionary<NSString *,NSMutableArray<NSData *> *> *sets) {
    for(NSString *role in sets) {
        NSURL *folder=At(target.root,[@"System/Library/Keychains" stringByAppendingPathComponent:role]);
        NSError *error=nil;NSArray *names=[NSFileManager.defaultManager contentsOfDirectoryAtPath:folder.path error:&error];
        Require(names!=nil,error.localizedDescription);
        for(NSString *name in [names sortedArrayUsingSelector:@selector(compare:)]) {
            if([name hasPrefix:@"."])continue;
            NSURL *file=At(folder,name);struct stat info;
            Require(!lstat(file.fileSystemRepresentation,&info)&&S_ISREG(info.st_mode)&&info.st_size>0&&info.st_size<64*1024,
                @"Unexpected Tiger certificate entry.");
            [sets[role] addObject:Read(file)];
        }
    }
}

static void TigerRemoveFromTarget(VIMInstallTarget *target,NSString *path) {
    NSURL *url=At(target.root,path);struct stat info;
    if(!lstat(url.fileSystemRepresentation,&info))Remove(url);
    else Require(errno==ENOENT,@"Could not examine an obsolete Tiger file.");
    NSString *prefix=[path stringByAppendingString:@"/"];
    for(NSString *key in target.metadata.allKeys)if([key isEqual:path]||[key hasPrefix:prefix])[target.metadata removeObjectForKey:key];
}

static void OpenUpdateDirectory(VIMInstallTarget *target,NSString *path,NSMutableSet<NSString *> *visited) {
    if(!path.length||[path isEqual:@"."]||[visited containsObject:path])return;
    OpenUpdateDirectory(target,path.stringByDeletingLastPathComponent,visited);
    [visited addObject:path];
    NSError *error=nil;NSURL *directory=VIMResolveGuestPath(target.root,path,YES,&error);struct stat info;
    if(!directory) {
        Require([error.domain isEqual:NSPOSIXErrorDomain]&&error.code==ENOENT,
            [NSString stringWithFormat:@"Could not inspect the staged directory %@: %@",path,error.localizedDescription]);
        return;
    }
    path=[directory.path substringFromIndex:target.root.path.length+1];
    Require(!lstat(directory.fileSystemRepresentation,&info),@"Could not inspect the guest directory.");
    Require(S_ISDIR(info.st_mode),[NSString stringWithFormat:@"The update does not target a directory at %@.",path]);
    if((info.st_mode&S_IRWXU)==S_IRWXU)return;
    // The period installer overlays files as root. The host mounts only this
    // unpublished volume without ownership; open its required directories for
    // the overlay, retaining the actual guest modes in the detached catalog.
    if(!target.metadata[path])target.metadata[path]=@[@(info.st_uid),@(info.st_gid),@(info.st_mode)];
    Require(!chmod(directory.fileSystemRepresentation,(info.st_mode&07777)|S_IRWXU),
        [NSString stringWithFormat:@"Could not open the staged directory %@.",path]);
}

static NSArray *TigerUpdateRecipe(void) {
    return @[
        @{@"Role":@"Combo",@"Product":@"zzz061-4049",@"Name":@"MacOSXUpdCombo10.4.11Intel",
          @"PackageID":@"com.apple.pkg.MacOSXUpdCombo10.4.11Intel.224.6",@"PackageBuild":@"8S2167"},
        @{@"Role":@"Security",@"Product":@"zz061-7097",@"Name":@"SecUpd2009-005Intel",
          @"PackageID":@"com.apple.pkg.SecUpd2009-005Intel",@"PackageBuild":@"8S2525"}];
}

static void OptionalUpdateUnavailable(NSString *reason) {
    @throw [NSException exceptionWithName:@"OptionalHostUpdateUnavailable" reason:reason userInfo:nil];
}

// No guest-volume writes: authenticate the entire sequence before unpacking
// any member, then validate every package/BOM before making either applicable.
static NSDictionary *PrepareTigerUpdateInputs(NSDictionary *inputs,NSData *key,NSURL *work,NSURL *attempt) {
    NSArray *recipe=TigerUpdateRecipe();NSMutableDictionary *prepared=[NSMutableDictionary dictionary];
    for(NSDictionary *step in recipe) {
        Checkpoint(attempt);
        NSString *input=inputs[step[@"Role"]];
        Require([input isKindOfClass:NSString.class]&&input.isAbsolutePath,@"An original Tiger update is missing.");
        NSString *member=[[step[@"Name"] stringByAppendingPathExtension:@"pkg"] stringByAppendingPathExtension:@"tar"];
        NSError *error=nil;
        BOOL ok=VIMStageApplePeriodUpdate([NSURL fileURLWithPath:input],member,key,400ULL<<20,At(work,member),^BOOL{
            return [NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path];
        },&error);
        if(!ok) {
            Checkpoint(attempt);
            if([error.domain isEqual:NSPOSIXErrorDomain]&&
               (error.code==EINVAL||error.code==EBADMSG||error.code==ENOENT))
                OptionalUpdateUnavailable(error.localizedDescription);
            StopWriteFailure(error,@"Could not authenticate a Tiger update.");
        }
    }
    for(NSDictionary *step in recipe) {
        Checkpoint(attempt);
        NSString *package=[step[@"Name"] stringByAppendingPathExtension:@"pkg"];
        NSURL *verified=At(work,[package stringByAppendingPathExtension:@"tar"]);
        // Authenticated Apple members only; do not execute installer scripts.
        Run(@"/usr/bin/tar",@[@"-xpf",verified.path,@"--no-same-owner",@"-C",work.path,package],nil,NO);
        Remove(verified);Checkpoint(attempt);
        NSURL *contents=At(At(work,package),@"Contents");
        NSDictionary *info=Plist(At(contents,@"Info.plist"));
        if(![info[@"CFBundleIdentifier"] isEqual:step[@"PackageID"]]||![info[@"IFPkgBuildVersion"] isEqual:step[@"PackageBuild"]])
            OptionalUpdateUnavailable(@"The signed Tiger package does not match its update recipe.");
        struct stat payload;
        if(lstat(At(contents,@"Archive.pax.gz").fileSystemRepresentation,&payload)||!S_ISREG(payload.st_mode)||payload.st_size<=0)
            OptionalUpdateUnavailable(@"The signed Tiger update payload is unavailable.");
        prepared[step[@"Role"]]=@{@"Contents":contents,@"BOM":ReadBOM(At(contents,@"Archive.bom"))};
    }
    return prepared.copy;
}

// The app supplies the complete catalog sequence. Only the authenticated base
// supplies the verification key; update package scripts are never executed.
static NSArray *ApplyTigerUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    NSArray *recipe=TigerUpdateRecipe();
    Require([inputs isKindOfClass:NSDictionary.class]&&inputs.count==recipe.count,@"Choose both original Tiger update downloads.");
    NSString *systemVersion=[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"];
    Require([Plist(At(target.root,systemVersion))[@"ProductBuildVersion"] isEqual:@"8R4088"],@"This update recipe requires the original Tiger base build.");
    NSData *key=Plist(At(target.root,@"System/Library/PrivateFrameworks/SoftwareUpdate.framework/Versions/A/Resources/PublicKey.plist"))[@"ApplePkgKeyData"];
    Require([key isKindOfClass:NSData.class],@"The original Tiger Software Update key is missing.");
    // The native combo on this iMac7,1 restore retains the newer public CGL
    // and 759-slot engine as a unit. Its payload contains the older 686-slot
    // framework, not a substitute for the restore's OpenGL or Rosetta pieces.
    NSArray *preserved=@[@"System/Library/Frameworks/OpenGL.framework",
        @"usr/libexec/oah/translate",@"usr/libexec/oah/Shims/GLEngine.bundle/GLEngine"];
    NSURL *work=At(attempt,@"TigerUpdates");Directory(work);
    @try {
        NSDictionary *prepared=nil;
        Progress(attempt,@"tiger-update-verify",@"Checking the optional Tiger updates…",.80,.805,-1,12);
        @try {prepared=PrepareTigerUpdateInputs(inputs,key,work,attempt);}
        @catch(NSException *failure) {
            // Cancellation and real storage exhaustion are not an unavailable
            // optional update. Never hide them or publish a misleading result.
            if(![failure.name isEqual:@"OptionalHostUpdateUnavailable"]||[failure.userInfo[@"ExitStatus"] isEqual:@(ENOSPC)]||
               [NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path])@throw;
            fprintf(stderr,"Optional Tiger updates were not applied: %s\n",failure.reason.UTF8String);
            WritePlist(At(attempt,@"HostUpdateNotice.plist"),@{@"Status":@"original-system-kept",
                @"Reason":@"Additional Tiger updates could not be prepared. The original system was kept."});
            return @[];
        }
        NSDictionary *certificates=@{@"Anchors":[NSMutableArray new],@"Certificates":[NSMutableArray new]};
        TigerCollectCertificates(target,certificates);
        NSMutableArray *applied=[NSMutableArray array];
        for(NSDictionary *step in recipe) {
            Checkpoint(attempt);
            NSString *name=step[@"Name"],*package=[name stringByAppendingPathExtension:@"pkg"];
            double start=.805+.0175*applied.count;
            Progress(attempt,@"tiger-update",@"Installing system updates…",start,start+.0175,-1,30);
            NSURL *contents=prepared[step[@"Role"]][@"Contents"];
            // Tiger writes old ASCII cpio. The native pax reader preserves
            // its repeated hard-link records; bsdtar links them and then
            // tries to reopen the read-only inode for writing.
            NSMutableArray *arguments=[@[@"-r",@"-z",@"-p",@"p",@"-f",At(contents,@"Archive.pax.gz").path] mutableCopy];
            NSMutableDictionary *metadata=[prepared[step[@"Role"]][@"BOM"] mutableCopy];
            for(NSString *path in preserved) {
                NSString *prefix=[path stringByAppendingString:@"/"];
                NSString *literal=[path stringByReplacingOccurrencesOfString:@"." withString:@"\\."];
                [arguments addObjectsFromArray:@[@"-s",[NSString stringWithFormat:@"|^\\./%@$||",literal],
                    @"-s",[NSString stringWithFormat:@"|^\\./%@/.*||",literal]]];
                for(NSString *key in metadata.allKeys)
                    if([key isEqual:path]||[key hasPrefix:prefix])[metadata removeObjectForKey:key];
            }
            NSMutableSet *visited=[NSMutableSet new];
            for(NSString *path in metadata) {
                Checkpoint(attempt);
                mode_t mode=[metadata[path][2] unsignedShortValue];
                OpenUpdateDirectory(target,S_ISDIR(mode)?path:path.stringByDeletingLastPathComponent,visited);
            }
            Run(@"/bin/pax",arguments,target.root,NO);
            [target.metadata addEntriesFromDictionary:metadata];
            Require([Plist(At(target.root,systemVersion))[@"ProductBuildVersion"] isEqual:@"8S2167"],@"Tiger did not reach the expected installed build.");
            NSString *receipt=[@"Library/Receipts" stringByAppendingPathComponent:package];
            NSString *receiptContents=[receipt stringByAppendingPathComponent:@"Contents"];
            [target directory:receiptContents mode:0755];
            for(NSString *leaf in @[@"Archive.bom",@"Info.plist",@"version.plist",@"Resources"])
                Ditto(@[At(contents,leaf).path,At(At(target.root,receiptContents),leaf).path]);
            [target record:receipt uid:0 gid:80 receipt:YES];
            TigerCollectCertificates(target,certificates);
            [applied addObject:step[@"Product"]];Remove(At(work,package));
        }
        for(NSString *role in certificates) {
            NSString *name=[@"X509" stringByAppendingString:role];
            NSURL *database=At(work,name);NSError *error=nil;
            BOOL ok=VIMCreateTigerCertificateKeychain(certificates[role],database,&error);
            Require(ok,error.localizedDescription);
            [target file:[@"System/Library/Keychains" stringByAppendingPathComponent:name] data:Read(database) mode:0644];
            TigerRemoveFromTarget(target,[@"System/Library/Keychains" stringByAppendingPathComponent:role]);
        }
        NSString *authorization=@"private/etc/authorization";
        NSMutableDictionary *auth=[Plist(At(target.root,authorization)) mutableCopy];
        NSMutableDictionary *rights=[auth[@"rights"] mutableCopy];
        Require(rights!=nil,@"Tiger’s authorization policy is unavailable.");
        if(!rights[@"com.apple."])rights[@"com.apple."]=@{@"rule":@"default"};
        rights[@"com.apple.dashboard.advisory.allow"]=@{@"class":@"user",@"group":@"admin",@"shared":@NO,@"timeout":@300};
        auth[@"rights"]=rights;[target plist:authorization value:auth mode:0644];
        for(NSString *daemon in @[@"smbd",@"nmbd"]) {
            NSString *path=[NSString stringWithFormat:@"System/Library/LaunchDaemons/%@.plist",daemon];
            NSMutableDictionary *settings=[Plist(At(target.root,path)) mutableCopy];
            NSArray *arguments=settings[@"ProgramArguments"];
            Require(arguments.count>0,@"A Tiger file sharing service is unavailable.");
            if(![arguments containsObject:@"-F"]) {
                settings[@"ProgramArguments"]=[arguments arrayByAddingObject:@"-F"];
                [target plist:path value:settings mode:0644];
            }
        }
        TigerRemoveFromTarget(target,@"System/Library/Frameworks/WebKit.framework/Versions/A/Frameworks/JavaScriptCore.framework");
        Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
        target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
        return applied.copy;
    } @finally {Remove(work);}
}

typedef struct {
    __strong NSURL *attempt;
    __strong NSURL *cancel;
    __strong NSString *stage;
    __strong NSString *title;
    uint64_t expected,last;
    double start,end;
} PayloadProgress;

static bool ReportPBZX(uint64_t input,__unused uint64_t decoded,void *opaque) {
    PayloadProgress *p=opaque;
    if(input==p->last)return true;
    p->last=input;
    if(!access(p->cancel.fileSystemRepresentation,F_OK))return false;
    @try {
        if(p->expected)Progress(p->attempt,p->stage,p->title,p->start,p->end,MIN(1,(double)input/p->expected),0);
    } @catch(NSException *failure) {(void)failure;}
    return true;
}

static void ExtractPBZXStream(int fd,NSURL *root,PayloadProgress *progress) {
    NSPipe *pipe=[NSPipe pipe];
    NSFileHandle *writer=pipe.fileHandleForWriting;
    NSTask *pax=[NSTask new];
    pax.executableURL=[NSURL fileURLWithPath:@"/bin/pax"];pax.arguments=@[@"-r",@"-p",@"p"];
    pax.currentDirectoryURL=root;pax.standardInput=pipe.fileHandleForReading;
    int status=-1,problem=0;BOOL launched=NO;
    @try {
        int output=writer.fileDescriptor;
        Require(fcntl(output,F_SETNOSIGPIPE,1)!=-1,@"Could not prepare the package extraction stream.");
        NSError *error=nil;launched=[pax launchAndReturnError:&error];Require(launched,error.localizedDescription);
        [pipe.fileHandleForReading closeFile];
        status=VIMDecodePBZX(fd,output,ReportPBZX,progress);if(status)problem=errno;
    } @finally {
        [writer closeFile];
        if(launched)[pax waitUntilExit];else [pipe.fileHandleForReading closeFile];
    }
    if(problem==ECANCELED)Checkpoint(progress->attempt);
    Require(status==0&&pax.terminationStatus==0,@"Could not decode and extract the original installer package.");
}

static void ExtractPBZX(NSURL *payload,NSURL *root,PayloadProgress *progress) {
    int fd=open(payload.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    Require(fd>=0,@"Could not read the installer package payload.");
    @try {ExtractPBZXStream(fd,root,progress);}
    @finally {close(fd);}
}

static void ExtractCorePayload(NSURL *package,NSURL *root,PayloadProgress *progress) {
    // BSD tar reads the XAR member directly; no multi-gigabyte scratch copy
    // of Core's already compressed data. Check both producer and consumer.
    NSPipe *pipe=[NSPipe pipe];
    NSTask *archive=[NSTask new];archive.executableURL=[NSURL fileURLWithPath:@"/usr/bin/tar"];
    archive.arguments=@[@"-xOf",package.path,@"Payload"];
    archive.standardInput=NSFileHandle.fileHandleWithNullDevice;archive.standardOutput=pipe.fileHandleForWriting;
    NSError *error=nil;BOOL launched=[archive launchAndReturnError:&error];
    [pipe.fileHandleForWriting closeFile];Require(launched,error.localizedDescription);
    @try {ExtractPBZXStream(pipe.fileHandleForReading.fileDescriptor,root,progress);}
    @finally {[pipe.fileHandleForReading closeFile];[archive waitUntilExit];}
    Require(archive.terminationStatus==0,@"Could not read Core from the original installer.");
}

static void ExtractFlatPayload(NSURL *payload,NSURL *root,PayloadProgress *progress) {
    NSFileHandle *handle=[NSFileHandle fileHandleForReadingFromURL:payload error:nil];
    Require(handle!=nil,@"The original package payload is missing.");
    NSData *signature=[handle readDataOfLength:6];[handle closeFile];
    const uint8_t *bytes=signature.bytes;
    if(signature.length>=4&&!memcmp(bytes,"pbzx",4)) {
        ExtractPBZX(payload,root,progress);return;
    }
    NSError *error=nil;
    BOOL extracted=VIMExtractCPIOPayload(payload,root,^BOOL(uint64_t consumed){
        if(!access(progress->cancel.fileSystemRepresentation,F_OK))return NO;
        return ReportPBZX(consumed,0,progress);
    },&error);
    if([error.domain isEqual:NSCocoaErrorDomain]&&error.code==NSUserCancelledError)Checkpoint(progress->attempt);
    Require(extracted,error.localizedDescription?:@"The installer package couldn’t be extracted.");
}

static void TemplateACLChildren(NSURL *root,NSString *relative,NSMutableArray<NSString *> *paths) {
    NSArray *children=[NSFileManager.defaultManager contentsOfDirectoryAtPath:At(root,relative).path error:nil];
    for(NSString *name in [children sortedArrayUsingSelector:@selector(compare:)]) {
        if([name hasPrefix:@"."]||([relative.lastPathComponent isEqual:@"Sites"]&&[name isEqual:@"images"])||
            ([relative.lastPathComponent isEqual:@"Public"]&&[name isEqual:@"Drop Box"]))continue;
        NSString *child=[relative stringByAppendingPathComponent:name];struct stat info;
        if(lstat(At(root,child).fileSystemRepresentation,&info)||!S_ISDIR(info.st_mode))continue;
        [paths addObject:child];TemplateACLChildren(root,child,paths);
    }
}
static void WriteFlatReceipts(VIMInstallTarget *target,NSArray<NSDictionary *> *receipts,
                              NSArray<NSString *> *aclPaths,NSString *guest) {
    NSString *denyDelete=@"!#acl 1\ngroup:ABCDEFAB-CDEF-ABCD-EFAB-CDEF0000000C:everyone:12:deny:delete\n";
    if([guest isEqual:@"leopard"]) {
        NSString *boms=@"Library/Receipts/boms",*db=@"Library/Receipts/db/a.receiptdb";
        Directory(At(target.root,boms));Directory(At(target.root,db.stringByDeletingLastPathComponent));
        for(NSDictionary *receipt in receipts) {
            NSURL *bom=receipt[@"BOM"];
            NSString *path=[boms stringByAppendingPathComponent:[receipt[@"Identifier"] stringByAppendingString:@".bom"]];
            Ditto(@[bom.path,At(target.root,path).path]);
            target.metadata[path]=@[@96,@0,@(S_IFREG|0644)];
        }
        VIMWriteLeopardReceipts(At(target.root,db),receipts,aclPaths,denyDelete);
        target.metadata[boms]=@[@96,@0,@(S_IFDIR|0755)];
        target.metadata[db.stringByDeletingLastPathComponent]=@[@96,@80,@(S_IFDIR|0755)];
        target.metadata[db]=@[@96,@0,@(S_IFREG|0600)];
        return;
    }
    // PackageKit on Snow Leopard and later uses binary plists and BOMs.
    // Preserve the other receipts delivered in the Apple payload itself.
    BOOL corePackages=UsesCorePackage(guest);
    NSString *folder=corePackages?@"System/Library/Receipts":@"private/var/db/receipts";
    [target directory:folder mode:0755];
    target.metadata[folder]=@[@0,@0,@(S_IFDIR|0755)];
    NSMutableDictionary *baseACL=[NSMutableDictionary dictionary],*essentialsACL=[NSMutableDictionary dictionary];
    for(NSString *path in aclPaths) {
        NSMutableDictionary *acl=[path hasPrefix:@"System/"]?essentialsACL:baseACL;
        acl[path]=denyDelete;
    }
    Run(@"/bin/chmod",@[@"+a",@"everyone deny add_file,add_subdirectory,directory_inherit,only_inherit",
        At(target.root,@"Volumes").path],nil,NO);
    baseACL[@"Volumes"]=@"!#acl 1\ngroup:ABCDEFAB-CDEF-ABCD-EFAB-CDEF0000000C:"
        @"everyone:12:deny,directory_inherit,only_inherit:write,append\n";
    NSDate *installed=[NSDate date];
    for(NSDictionary *receipt in receipts) {
        NSString *identifier=receipt[@"Identifier"],*name=receipt[@"Name"];
        NSMutableDictionary *record=[@{@"InstallDate":installed,@"InstallPrefixPath":@"/",
            @"InstallProcessName":corePackages?@"macOS Installer":@"Mac OS X Installer",@"PackageFileName":[name stringByAppendingPathExtension:@"pkg"],
            @"PackageIdentifier":identifier,@"PackageVersion":receipt[@"Version"]} mutableCopy];
        if([receipt[@"Groups"] count])record[@"PackageGroups"]=receipt[@"Groups"];
        if([name isEqual:@"BaseSystem"]||[name isEqual:@"BaseSystemResources"])record[@"PathACLs"]=baseACL;
        if([name isEqual:@"Essentials"])record[@"PathACLs"]=essentialsACL;
        if(corePackages&&[name isEqual:@"Core"]) {
            [essentialsACL addEntriesFromDictionary:baseACL];
            record[@"PathACLs"]=essentialsACL;
        }
        NSString *base=[folder stringByAppendingPathComponent:identifier];
        NSString *bomPath=[base stringByAppendingPathExtension:@"bom"];
        NSURL *bom=receipt[@"BOM"];
        Ditto(@[bom.path,At(target.root,bomPath).path]);
        target.metadata[bomPath]=@[@0,@0,@(S_IFREG|0644)];
        NSError *error=nil;
        NSData *bytes=[NSPropertyListSerialization dataWithPropertyList:record format:NSPropertyListBinaryFormat_v1_0
            options:0 error:&error];
        Require(bytes!=nil,error.localizedDescription);
        [target file:[base stringByAppendingPathExtension:@"plist"] data:bytes mode:0644];
    }
}
static void CopyBaseResources(VIMInstallTarget *target,NSURL *base,NSURL *bom,NSURL *scratch,
                              NSMutableDictionary *metadata) {
    NSString *listing=[[NSString alloc] initWithData:Run(@"/usr/bin/lsbom",@[bom.path],nil,YES)
        encoding:NSUTF8StringEncoding];
    Require(listing.length,@"Could not read the original BaseSystem resource list.");
    NSMutableString *staging=[NSMutableString string];
    [listing enumerateLinesUsingBlock:^(NSString *line,__unused BOOL *stop) {
        @autoreleasepool {
            NSMutableArray<NSString *> *fields=[[line componentsSeparatedByString:@"\t"] mutableCopy];
            Require(fields.count>=2,@"Invalid BaseSystem resource.");
            NSString *path=fields[0];if([path hasPrefix:@"./"])path=[path substringFromIndex:2];
            Require(metadata[path]!=nil,@"Invalid BaseSystem resource path.");
            struct stat info;
            if(lstat(At(base,path).fileSystemRepresentation,&info)) {
                Require(errno==ENOENT||errno==ENOTDIR,@"Could not inspect a BaseSystem resource.");
                // Some versions' BOMs list resources absent from the disc.
                [metadata removeObjectForKey:path];return;
            }
            mode_t mode=(mode_t)strtoul(fields[1].UTF8String,NULL,8);
            if([path isEqual:@".file"]&&mode==S_IFREG&&fields.count>3&&[fields[3] isEqual:@"0"]) {
                // The original HFS marker is mode 000 and cannot be read.
                Write(At(target.root,path),NSData.data);return;
            }
            if(S_ISDIR(mode)&&!(mode&S_IXUSR)) {
                fields[1]=[NSString stringWithFormat:@"%o",mode|S_IXUSR];
                NSURL *existing=At(target.root,path);
                if(!lstat(existing.fileSystemRepresentation,&info)&&S_ISDIR(info.st_mode))
                    Require(!chmod(existing.fileSystemRepresentation,(info.st_mode&07777)|S_IXUSR),
                        @"Could not make a staging directory searchable.");
            }
            [staging appendFormat:@"%@\n",[fields componentsJoinedByString:@"\t"]];
        }
    }];
    NSURL *list=At(scratch,@"CopyListing"),*copy=At(scratch,@"CopyBom");
    Write(list,[staging dataUsingEncoding:NSUTF8StringEncoding]);
    Run(@"/usr/bin/mkbom",@[@"-i",list.path,copy.path],nil,NO);
    Ditto(@[@"--bom",copy.path,base.path,target.root.path]);
}
static NSDictionary *ExtractFlatPackages(VIMInstallTarget *target,NSURL *packageRoot,NSURL *base,NSURL *attempt,
                                         NSString *guest,NSString *title) {
    BOOL leopard=[guest isEqual:@"leopard"];
    BOOL corePackages=UsesCorePackage(guest);
    NSDictionary *firmlinks=nil;
    NSArray *packages=Packages(guest);
    NSURL *staging=At(attempt,@"FlatPackages");
    Directory(staging);
    @try {
        NSMutableArray *receipts=[NSMutableArray array];
        NSMutableDictionary<NSString *,NSNumber *> *sizes=[NSMutableDictionary dictionary];
        double done=0,total=0;
        for(NSString *name in packages) {
            NSURL *pkg=At(packageRoot,[name stringByAppendingPathExtension:@"pkg"]);
            uint64_t bytes=[NSFileManager.defaultManager attributesOfItemAtPath:pkg.path error:nil].fileSize;
            Require(bytes!=0,[NSString stringWithFormat:@"The %@ installer is missing %@.",title,name]);
            if(base&&[name isEqual:@"BaseSystemResources"])
                bytes+=[NSFileManager.defaultManager attributesOfItemAtPath:
                    At(packageRoot.URLByDeletingLastPathComponent,@"BaseSystem.dmg").path error:nil].fileSize;
            sizes[name]=@(bytes);total+=bytes;
        }
        for(NSString *name in packages) {
            @autoreleasepool {
                Checkpoint(attempt);
                double weight=sizes[name].doubleValue/total;
                NSString *stage=[@"package-" stringByAppendingString:name];
                NSString *label=[NSString stringWithFormat:@"Installing %@ system files…",title];
                double start=(base?.14:.04)+(base?.62:.72)*done/total;
                double end=(base?.14:.04)+(base?.62:.72)*(done+sizes[name].doubleValue)/total;
                double estimate=MAX(.5,(base?145:leopard?118:85)*weight);
                // The large bzip2 Essentials package is CPU-bound on the
                // measured Mavericks run, unlike the smaller package copies.
                if([guest isEqual:@"mavericks"]&&[name isEqual:@"Essentials"])estimate=185;
                Progress(attempt,stage,label,start,end,-1,estimate);
                NSURL *pkg=At(packageRoot,[name stringByAppendingPathExtension:@"pkg"]),*folder=At(staging,name);
                Directory(folder);
                BOOL baseResources=base&&[name isEqual:@"BaseSystemResources"];
                BOOL streamCore=corePackages&&[name isEqual:@"Core"];
                NSMutableArray *args=[@[@"-x",@"-C",folder.path,@"-f",pkg.path,@"Bom",@"PackageInfo"] mutableCopy];
                if(!baseResources&&!streamCore)[args addObject:@"Payload"];
                Run(@"/usr/bin/xar",args,nil,NO);
                NSError *error=nil;
                NSXMLDocument *xml=[[NSXMLDocument alloc] initWithData:Read(At(folder,@"PackageInfo"))
                    options:NSXMLNodeLoadExternalEntitiesNever error:&error];
                NSXMLElement *info=xml.rootElement;
                NSString *identifier=[info attributeForName:@"identifier"].stringValue,*version=[info attributeForName:@"version"].stringValue;
                NSString *location=[info attributeForName:@"install-location"].stringValue?:@"/";
                Require(identifier.length&&version.length&&[location isEqual:@"/"]&&
                    [identifier rangeOfString:@"^[A-Za-z0-9._-]+$" options:NSRegularExpressionSearch].location!=NSNotFound,
                    error.localizedDescription?:@"An original package record is invalid.");
                NSMutableArray *groups=[NSMutableArray array];
                for(NSXMLNode *node in [info nodesForXPath:@"./groups/group" error:nil])
                    if(node.stringValue.length)[groups addObject:node.stringValue];
                NSURL *bom=At(folder,@"Bom"),*payload=At(folder,@"Payload");
                NSMutableDictionary *metadata=[ReadBOM(bom) mutableCopy];
                if(baseResources)CopyBaseResources(target,base,bom,folder,metadata);
                else {
                    PayloadProgress progress={.attempt=attempt,.cancel=At(attempt,@"Cancel"),.stage=stage,.title=label,.start=start,.end=end,
                        .expected=streamCore?[sizes[name] unsignedLongLongValue]:
                            [NSFileManager.defaultManager attributesOfItemAtPath:payload.path error:nil].fileSize};
                    if(streamCore)ExtractCorePayload(pkg,target.root,&progress);
                    else {ExtractFlatPayload(payload,target.root,&progress);Remove(payload);}
                }
                [target.metadata addEntriesFromDictionary:metadata];
                [receipts addObject:@{@"Name":name,@"Identifier":identifier,@"Version":version,@"Groups":groups,@"BOM":bom,@"Metadata":metadata}];
                done+=sizes[name].doubleValue;
            }
        }
        Progress(attempt,@"receipts",@"Recording the installed packages…",.76,.80,-1,leopard?7:4);
        if([guest isEqual:@"catalina"]) {
            // Core first populates the Data template and creates the utility
            // alias. The later receipt ACL pass expects those paths to exist.
            NSError *layoutError=nil;
            NSString *manifest=[[NSString alloc] initWithData:Read(At(target.root,@"usr/share/firmlinks")) encoding:NSUTF8StringEncoding];
            Require(manifest.length,@"The original Catalina System and Data manifest is missing.");
            firmlinks=VIMParseCatalinaFirmlinks(manifest,&layoutError);
            Require(firmlinks!=nil,layoutError.localizedDescription);
            BOOL ready=VIMMaterializeCatalinaDataTemplate(target.root,firmlinks,target.metadata,
                ^BOOL(NSURL *source,NSURL *dest,NSError **failure){
                    (void)failure;Checkpoint(attempt);Ditto(@[source.path,dest.path]);return YES;
                },&layoutError);
            Require(ready,layoutError.localizedDescription);
        }
        NSMutableArray *aclPaths=[@[@"Applications",@"Library",@"Applications/Utilities"] mutableCopy];
        NSString *templates=@"System/Library/User Template";
        NSArray *localizations=[NSFileManager.defaultManager contentsOfDirectoryAtPath:At(target.root,templates).path error:nil];
        for(NSString *lang in [localizations sortedArrayUsingSelector:@selector(compare:)])
            if([lang hasSuffix:@".lproj"])TemplateACLChildren(target.root,[templates stringByAppendingPathComponent:lang],aclPaths);
        if(corePackages) {
            [aclPaths addObject:@"System"];
            for(NSString *parent in @[@"Applications",@"Applications/Utilities"]) {
                for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:At(target.root,parent).path error:nil]) {
                    NSString *path=[parent stringByAppendingPathComponent:name];struct stat info;
                    if([name hasSuffix:@".app"]&&!lstat(At(target.root,path).fileSystemRepresentation,&info)&&S_ISDIR(info.st_mode))
                        [aclPaths addObject:path];
                }
            }
        }
        NSMutableArray *aclArguments=[@[@"+a",@"everyone deny delete"] mutableCopy];
        for(NSString *path in aclPaths)[aclArguments addObject:At(target.root,path).path];
        Run(@"/bin/chmod",aclArguments,nil,NO);
        WriteFlatReceipts(target,receipts,aclPaths,guest);
        Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
        target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
        if(corePackages)[target file:@"usr/local/.com.apple.installer.keep" data:NSData.data mode:0644];
        else {
            NSString *bsd=@"Library/Receipts/BSD.pkg";
            if(![NSFileManager.defaultManager fileExistsAtPath:At(target.root,bsd).path])
                [target file:bsd data:NSData.data mode:0644];
            target.metadata[bsd]=@[@0,@80,@(S_IFREG|0644)];
        }
        NSMutableArray *hidden=[@[@"-h",@"hidden"] mutableCopy];
        for(NSString *path in @[@"bin",@"cores",@"dev",@"etc",@"mach_kernel",@"private",@"sbin",@"tmp",@"usr",@"var",@"Volumes",@"opt",@"Network"]) {
            NSURL *url=At(target.root,path);struct stat info;
            if(!lstat(url.fileSystemRepresentation,&info))[hidden addObject:url.path];
        }
        Run(@"/usr/bin/chflags",hidden,nil,NO);
    } @finally {Remove(staging);}
    return firmlinks;
}

static NSArray *LeopardUpdateRecipe(void) {
    return VIMFlatHostUpdateRecipe(@"leopard");
}

static NSArray *PrepareFlatUpdateInputs(NSDictionary *inputs,NSArray *recipe,NSString *title,NSURL *work,NSURL *attempt) {
    // Authenticate the complete chain before unpacking either payload. An
    // unavailable optional update must not leave a half-updated base system.
    for(NSDictionary *step in recipe) {
        Checkpoint(attempt);NSString *path=inputs[step[@"Role"]];
        if(![path isKindOfClass:NSString.class]||!path.isAbsolutePath||
           ![NSFileManager.defaultManager isReadableFileAtPath:path])
            OptionalUpdateUnavailable([NSString stringWithFormat:@"An original %@ update is unavailable.",title]);
        NSData *signature=nil;
        @try {signature=Run(@"/usr/sbin/pkgutil",@[@"--check-signature",path],nil,YES);}
        @catch(NSException *failure) {OptionalUpdateUnavailable(failure.reason);}
        Checkpoint(attempt);
        NSString *report=[[NSString alloc] initWithData:signature encoding:NSUTF8StringEncoding];BOOL apple=NO;
        for(NSString *line in [report componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet])
            if([[line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] isEqual:@"Status: signed Apple Software"])apple=YES;
        if(!apple)OptionalUpdateUnavailable([NSString stringWithFormat:@"The %@ update is not an original Apple-signed package.",title]);
    }
    NSMutableArray *prepared=[NSMutableArray new];
    for(NSDictionary *step in recipe) {
        Checkpoint(attempt);NSURL *folder=At(work,step[@"Role"]);Directory(folder);
        BOOL recovery=[step[@"Kind"] isEqual:@"RecoveryImage"];
        Run(@"/usr/bin/xar",@[@"-xf",inputs[step[@"Role"]],@"-C",folder.path,@"PackageInfo",@"Bom",@"Payload",@"Scripts",
            @"cleanListSUConstructed",@"cleanDirectories",@"RecoveryHDMeta.dmg"],nil,NO);
        NSError *error=nil;NSXMLDocument *xml=[[NSXMLDocument alloc] initWithData:Read(At(folder,@"PackageInfo"))
            options:NSXMLNodeLoadExternalEntitiesNever error:&error];
        NSXMLElement *info=xml.rootElement;NSString *identifier=[info attributeForName:@"identifier"].stringValue;
        NSString *version=[info attributeForName:@"version"].stringValue;
        NSString *location=[info attributeForName:@"install-location"].stringValue?:@"/";
        if(![identifier isEqual:step[@"PackageID"]]||!version.length||![location isEqual:@"/"])
            OptionalUpdateUnavailable([NSString stringWithFormat:@"The signed %@ package does not match its OS update recipe.",title]);
        NSMutableArray *groups=[NSMutableArray new];
        for(NSXMLNode *node in [info nodesForXPath:@"./groups/group" error:nil])
            if(node.stringValue.length)[groups addObject:node.stringValue];
        if([[info nodesForXPath:@"./scripts/*" error:nil] count]) {
            NSURL *scripts=At(folder,@"ScriptFiles");Directory(scripts);
            Run(@"/bin/pax",@[@"-rzf",At(folder,@"Scripts").path],scripts,NO);
        }
        NSMutableDictionary *entry=[@{@"Name":step[@"Name"],@"Identifier":identifier,@"Version":version,@"Groups":groups,
            @"Metadata":recovery?@{}:ReadBOM(At(folder,@"Bom")),@"Folder":folder,@"PackageInfo":xml} mutableCopy];
        if(recovery)entry[@"RecoveryImage"]=At(folder,@"RecoveryHDMeta.dmg");
        else entry[@"BOM"]=At(folder,@"Bom");
        [prepared addObject:entry];
    }
    return prepared;
}
static NSArray *PrepareLeopardUpdateInputs(NSDictionary *inputs,NSURL *work,NSURL *attempt) {
    return PrepareFlatUpdateInputs(inputs,LeopardUpdateRecipe(),@"Leopard",work,attempt);
}

static NSURL *ExistingGuestPath(VIMInstallTarget *target,NSString *path,BOOL followFinal) {
    NSError *error=nil;NSURL *url=VIMResolveGuestPath(target.root,path,followFinal,&error);
    if(!url&&![error.domain isEqual:NSPOSIXErrorDomain])Stop(error.localizedDescription);
    if(!url&&error.code!=ENOENT&&error.code!=ENOTDIR)Stop(error.localizedDescription);
    return url;
}
static void RemoveLeopardUpdatePath(VIMInstallTarget *target,NSString *path,BOOL recursive) {
    NSURL *url=ExistingGuestPath(target,path,NO);if(!url)return;
    struct stat info;
    if(lstat(url.fileSystemRepresentation,&info)){Require(errno==ENOENT,@"Could not inspect an obsolete guest file.");return;}
    NSString *relative=[url.path substringFromIndex:target.root.path.length+1];
    OpenUpdateDirectory(target,relative.stringByDeletingLastPathComponent,[NSMutableSet new]);
    if(S_ISDIR(info.st_mode)&&!recursive) {
        if(rmdir(url.fileSystemRepresentation)) {
            Require(errno==ENOTEMPTY||errno==EEXIST,@"Could not remove an obsolete guest directory.");return;
        }
    } else Remove(url);
    NSString *original=[path stringByStandardizingPath];
    if(original.isAbsolutePath)original=[original substringFromIndex:1];
    [target.metadata removeObjectForKey:relative];
    [target.metadata removeObjectForKey:original];
    // A file/link has no catalog descendants; a successful nonrecursive
    // rmdir was empty. Avoid scanning hundreds of thousands of entries for
    // each obsolete leaf in a large Apple cleanup list.
    if(!S_ISDIR(info.st_mode)||!recursive)return;
    @autoreleasepool {
        NSString *prefix=[relative stringByAppendingString:@"/"];
        NSString *originalPrefix=[original stringByAppendingString:@"/"];
        for(NSString *key in target.metadata.allKeys)
            if([key hasPrefix:prefix]||[key hasPrefix:originalPrefix])
                [target.metadata removeObjectForKey:key];
    }
}
static NSArray<NSString *> *LeopardGuestPattern(VIMInstallTarget *target,NSString *pattern) {
    // Apple's component cleanup lists contain a few *.lproj/*.nib.plist
    // entries. Expand only path components, without a shell or host glob().
    NSMutableArray *paths=[NSMutableArray arrayWithObject:@""];
    pattern=[pattern stringByReplacingOccurrencesOfString:@"\\ " withString:@" "];
    for(NSString *part in pattern.pathComponents) {
        if([part isEqual:@"/"]||[part isEqual:@"."])continue;
        Require(![part isEqual:@".."],@"An obsolete guest path escapes its volume.");
        NSMutableArray *next=[NSMutableArray new];
        for(NSString *parent in paths) {
            if([part rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"*?["]].location==NSNotFound) {
                [next addObject:[parent stringByAppendingPathComponent:part]];continue;
            }
            NSURL *folder=parent.length?ExistingGuestPath(target,parent,YES):target.root;if(!folder)continue;
            for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:folder.path error:nil])
                if(!fnmatch(part.fileSystemRepresentation,name.fileSystemRepresentation,FNM_PERIOD))
                    [next addObject:[parent stringByAppendingPathComponent:name]];
        }
        paths=next;
    }
    return paths;
}
static void LeopardCleanupList(VIMInstallTarget *target,NSURL *list,NSString *prefix,BOOL recursive) {
    NSString *text=[[NSString alloc] initWithData:Read(list) encoding:NSUTF8StringEncoding];
    Require(text!=nil,@"Could not read the original update cleanup list.");
    [text enumerateLinesUsingBlock:^(NSString *line,__unused BOOL *stop) {
        if(!line.length)return;
        for(NSString *path in LeopardGuestPattern(target,[prefix stringByAppendingPathComponent:line]))
            RemoveLeopardUpdatePath(target,path,recursive);
    }];
}
static void LeopardComboActions(VIMInstallTarget *target,NSDictionary *package) {
    NSURL *tools=At(package[@"Folder"],@"ScriptFiles/Tools");
    for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:tools.path error:nil])
        if([name hasPrefix:@"cleanList"])LeopardCleanupList(target,At(tools,name),@"",NO);
    for(NSString *name in @[@"ATIRage128.kext",@"ATIRage128DVDDriver.bundle",@"ATIRage128GA.plugin",
        @"ATIRage128GLDriver.bundle",@"ATIRagePro.kext",@"ATIRageProGA.plugin",@"Adaptec29160x.kext",
        @"Adaptec39160.kext",@"Adaptec78XXSCSI.kext",@"AppleAirPort2.kext",@"AppleCore99PE.kext",
        @"AppleCuda.kext",@"AppleGossamerPE.kext",@"AppleGracklePCI.kext",@"AppleHeathrow.kext",
        @"CMD646ATA.kext",@"HeathrowATA.kext",@"AppleUSBEthernet.kext",@"nvenet.kext",
        @"IOATAFamily.kext/Contents/PlugIns/IOATABlockStorage.kext/Contents/PlugIns/ATASMARTLib.plugin",
        @"IOFireWireFamily.kext/Contents/PlugIns/AppleLynx.kext",
        @"IONetworkingFamily.kext/Contents/PlugIns/AppleBCM440XEthernet.kext",
        @"IONetworkingFamily.kext/Contents/PlugIns/AppleBMacEthernet.kext",
        @"IONetworkingFamily.kext/Contents/PlugIns/AppleDP83816Ethernet.kext",
        @"IONetworkingFamily.kext/Contents/PlugIns/AppleYukon.kext",
        @"IOSerialFamily.kext/Contents/PlugIns/AppleSCCIrDA.kext",
        @"IOSerialFamily.kext/Contents/PlugIns/DCPModemSupport.kext",
        @"IOUSBFamily.kext/Contents/PlugIns/AppleUSBCDCDriver.kext",
        @"IOUSBFamily.kext/Contents/PlugIns/AppleUSBCDCEthernet.kext",
        @"IOUSBFamily.kext/Contents/PlugIns/AppleUSBCDCWMCControl.kext",
        @"IOUSBFamily.kext/Contents/PlugIns/AppleUSBCDCWMCData.kext",
        @"IOUSBFamily.kext/Contents/PlugIns/AppleUSBComposite.kext"])
        RemoveLeopardUpdatePath(target,[@"System/Library/Extensions" stringByAppendingPathComponent:name],YES);
    RemoveLeopardUpdatePath(target,@"usr/libexec/SmartCardServices/drivers/CCIDClassDriver.bundle",YES);
    RemoveLeopardUpdatePath(target,@"private/var/db/BootCache.playlist",NO);
    for(NSDictionary *cleanup in @[
        @{@"Script":@"RemoveOldWebKitFiles",@"Info":@"System/Library/Frameworks/WebKit.framework/Versions/A/Resources/Info.plist",@"Version":@"5530.19"},
        @{@"Script":@"RemoveOldWebClipFiles",@"Info":@"Library/Widgets/Web Clip.wdgt/Info.plist",@"Version":@"5527.2"}]) {
        if(![Plist(At(target.root,cleanup[@"Info"]))[@"CFBundleShortVersionString"] isEqual:cleanup[@"Version"]])continue;
        NSURL *script=At(package[@"Folder"],[@"ScriptFiles/postinstall_actions" stringByAppendingPathComponent:cleanup[@"Script"]]);
        NSString *text=[[NSString alloc] initWithData:Read(script) encoding:NSUTF8StringEncoding];
        NSRange data=[text rangeOfString:@"\n__DATA__\n"];
        Require(text&&data.location!=NSNotFound,@"The original update cleanup data is missing.");
        [[text substringFromIndex:NSMaxRange(data)] enumerateLinesUsingBlock:^(NSString *line,__unused BOOL *stop){
            if(line.length)RemoveLeopardUpdatePath(target,line,NO);
        }];
    }
    NSXMLDocument *xml=package[@"PackageInfo"];
    for(NSXMLElement *script in [xml nodesForXPath:@"/pkg-info/scripts/postinstall[@component-id]" error:nil]) {
        NSString *identifier=[script attributeForName:@"component-id"].stringValue;
        for(NSXMLElement *bundle in [xml nodesForXPath:@"/pkg-info/bundle-version/bundle" error:nil]) {
            if(![[bundle attributeForName:@"id"].stringValue isEqual:identifier])continue;
            NSString *path=[bundle attributeForName:@"path"].stringValue;
            NSDictionary *info=Plist(At(target.root,[path stringByAppendingPathComponent:@"Contents/Info.plist"]));
            if(![info[@"CFBundleIdentifier"] isEqual:identifier])continue;
            NSURL *versionFile=At(tools,[identifier stringByAppendingPathExtension:@"version"]);
            if([NSFileManager.defaultManager fileExistsAtPath:versionFile.path]) {
                NSString *version=[[[NSString alloc] initWithData:Read(versionFile) encoding:NSUTF8StringEncoding]
                    stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
                if(![info[@"CFBundleShortVersionString"] isEqual:version])continue;
            }
            LeopardCleanupList(target,At(tools,[identifier stringByAppendingPathExtension:@"cleanup"]),path,YES);
        }
    }
    // Fresh-install equivalents of the applicable non-payload actions. No
    // running services, previous accounts, third-party add-ons or firmware
    // are migrated, and no original installer script is executed.
    for(NSString *path in @[@"Library/Security",@"Library/Security/SecurityAgentPlugins"])
        if(ExistingGuestPath(target,path,YES))target.metadata[path]=@[@0,@0,@(S_IFDIR|0755)];
    NSString *cups=@"private/etc/cups/cupsd.conf";
    NSString *config=[[NSString alloc] initWithData:Read(At(target.root,cups)) encoding:NSUTF8StringEncoding];
    NSMutableArray *lines=[[config componentsSeparatedByString:@"\n"] mutableCopy];
    for(NSUInteger i=0;i<lines.count;i++)
        if([lines[i] hasPrefix:@"SystemGroupAuthKey"])lines[i]=@"SystemGroupAuthKey system.print.admin";
    [target file:cups data:[[lines componentsJoinedByString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding] mode:0644];
    NSString *raid=@"System/Library/LaunchDaemons/com.apple.CoreRAID.plist";
    NSMutableDictionary *settings=[Plist(At(target.root,raid)) mutableCopy];
    NSMutableDictionary *services=[settings[@"MachServices"] mutableCopy],*service=[services[@"com.apple.CoreRAID"] mutableCopy];
    if(service) {
        service[@"HideUntilCheckIn"]=@NO;service[@"ResetAtClose"]=@NO;
        services[@"com.apple.CoreRAID"]=service;settings[@"MachServices"]=services;
        [target plist:raid value:settings mode:0644];
    }
}
static NSArray *ApplyLeopardUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    Require(inputs.count==2,@"Choose both original Leopard OS updates.");
    NSString *systemVersion=[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"];
    Require([Plist(At(target.root,systemVersion))[@"ProductBuildVersion"] isEqual:@"9J3050"],
        @"This update recipe requires the original Leopard catalog installer.");
    NSURL *work=At(attempt,@"LeopardUpdates");Directory(work);
    @try {
        Progress(attempt,@"leopard-update-verify",@"Checking the optional Leopard updates…",.80,.805,-1,10);
        NSArray *packages=nil;
        @try {packages=PrepareLeopardUpdateInputs(inputs,work,attempt);}
        @catch(NSException *failure) {
            Checkpoint(attempt);
            if(![failure.name isEqual:@"OptionalHostUpdateUnavailable"])@throw;
            fprintf(stderr,"Optional Leopard updates were not applied: %s\n",failure.reason.UTF8String);
            WritePlist(At(attempt,@"HostUpdateNotice.plist"),@{@"Status":@"original-system-kept",
                @"Reason":@"Additional Leopard updates could not be prepared. The original system was kept."});
            return @[];
        }
        NSMutableArray *applied=[NSMutableArray new];
        for(NSDictionary *package in packages) {
            Checkpoint(attempt);double start=.805+.0175*applied.count;
            Progress(attempt,@"leopard-update",@"Installing system updates…",start,start+.0175,-1,35);
            NSDictionary *metadata=package[@"Metadata"];NSMutableSet *visited=[NSMutableSet new];
            for(NSString *path in metadata) {
                Checkpoint(attempt);mode_t mode=[metadata[path][2] unsignedShortValue];
                OpenUpdateDirectory(target,S_ISDIR(mode)?path:path.stringByDeletingLastPathComponent,visited);
            }
            PayloadProgress progress={.attempt=attempt,.cancel=At(attempt,@"Cancel"),.stage=@"leopard-update",
                .title=@"Installing system updates…",.start=start,.end=start+.0175};
            // PackageKit's install-at-startup redirects only live installations.
            // This offline volume receives the original final payload paths.
            ExtractFlatPayload(At(package[@"Folder"],@"Payload"),target.root,&progress);
            [target.metadata addEntriesFromDictionary:metadata];
            Require([Plist(At(target.root,systemVersion))[@"ProductVersion"] isEqual:@"10.5.8"],
                @"Leopard did not reach the expected OS version.");
            if(!applied.count)LeopardComboActions(target,package);
            VIMAppendLeopardReceipts(At(target.root,@"Library/Receipts/db/a.receiptdb"),@[package]);
            NSString *bom=[@"Library/Receipts/boms" stringByAppendingPathComponent:
                [package[@"Identifier"] stringByAppendingPathExtension:@"bom"]];
            Ditto(@[[package[@"BOM"] path],At(target.root,bom).path]);
            target.metadata[bom]=@[@96,@0,@(S_IFREG|0644)];
            [applied addObject:LeopardUpdateRecipe()[applied.count][@"Product"]];
        }
        // Both updates migrate a default Samba configuration; no user changes
        // exist yet, so the final Apple template is the native offline seed.
        [target file:@"private/etc/smb.conf" data:Read(At(target.root,@"private/etc/smb.conf.template")) mode:0644];
        NSString *postfix=@"private/etc/postfix/main.cf";
        NSString *config=[[NSString alloc] initWithData:Read(At(target.root,postfix)) encoding:NSUTF8StringEncoding];
        NSMutableArray *lines=[[config componentsSeparatedByString:@"\n"] mutableCopy];
        NSIndexSet *interfaces=[lines indexesOfObjectsPassingTest:^BOOL(NSString *line,__unused NSUInteger index,__unused BOOL *stop){
            return [line hasPrefix:@"inet_interfaces "]||[line hasPrefix:@"inet_interfaces="];
        }];
        [lines removeObjectsAtIndexes:interfaces];[lines addObject:@"inet_interfaces = localhost"];
        [target file:postfix data:[[lines componentsJoinedByString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding] mode:0644];
        Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
        target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
        return applied;
    } @finally {Remove(work);}
}

static void WriteFlatUpdateReceipt(VIMInstallTarget *target,NSDictionary *package) {
    // PackageKit gives an existing protected OS receipt precedence over a
    // duplicate in /var/db/receipts. Update that original record in place.
    NSString *protectedBase=[@"System/Library/Receipts" stringByAppendingPathComponent:package[@"Identifier"]];
    NSString *folder=[NSFileManager.defaultManager fileExistsAtPath:
        At(target.root,[protectedBase stringByAppendingPathExtension:@"plist"]).path]?
        @"System/Library/Receipts":@"private/var/db/receipts";
    OpenUpdateDirectory(target,folder,[NSMutableSet new]);
    [target directory:folder mode:0755];
    NSString *base=[folder stringByAppendingPathComponent:package[@"Identifier"]];
    NSString *bom=[base stringByAppendingPathExtension:@"bom"];
    if(package[@"BOM"]) {
        Ditto(@[[package[@"BOM"] path],At(target.root,bom).path]);
    } else {
        // Script/image-only packages still need the receipt's BOM pair:
        // High Sierra pkgutil ignores a lone plist. An empty file list records
        // no root-volume payload (Recovery is installed on its own volume).
        Run(@"/usr/bin/mkbom",@[@"-i",@"/dev/null",At(target.root,bom).path],nil,NO);
    }
    target.metadata[bom]=@[@0,@0,@(S_IFREG|0644)];
    NSDictionary *receipt=@{@"InstallDate":NSDate.date,@"InstallPrefixPath":@"/",
        @"InstallProcessName":@"Software Update",@"PackageFileName":[package[@"Name"] stringByAppendingPathExtension:@"pkg"],
        @"PackageIdentifier":package[@"Identifier"],@"PackageVersion":package[@"Version"],@"PackageGroups":package[@"Groups"]};
    NSError *error=nil;NSData *bytes=[NSPropertyListSerialization dataWithPropertyList:receipt
        format:NSPropertyListBinaryFormat_v1_0 options:0 error:&error];
    Require(bytes!=nil,error.localizedDescription);
    [target file:[base stringByAppendingPathExtension:@"plist"] data:bytes mode:0644];
}
static void SetUpdateConfigurationValue(VIMInstallTarget *target,NSString *path,NSString *key,NSString *value) {
    NSString *text=[[NSString alloc] initWithData:Read(At(target.root,path)) encoding:NSUTF8StringEncoding];
    Require(text!=nil,@"An original system configuration could not be read.");
    NSMutableArray *lines=[[text componentsSeparatedByString:@"\n"] mutableCopy];
    NSIndexSet *existing=[lines indexesOfObjectsPassingTest:^BOOL(NSString *line,__unused NSUInteger index,__unused BOOL *stop){
        NSString *trimmed=[line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
        NSRange equals=[trimmed rangeOfString:@"="];
        return equals.location!=NSNotFound&&[[[trimmed substringToIndex:equals.location]
            stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] isEqual:key];
    }];
    [lines removeObjectsAtIndexes:existing];[lines addObject:[NSString stringWithFormat:@"%@ = %@",key,value]];
    [target file:path data:[[lines componentsJoinedByString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding] mode:0644];
}
static void SnowComboActions(VIMInstallTarget *target,NSDictionary *package) {
    NSURL *tools=At(package[@"Folder"],@"ScriptFiles/Tools");
    for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:tools.path error:nil])
        if([name hasPrefix:@"cleanList"])LeopardCleanupList(target,At(tools,name),@"",NO);
    for(NSString *path in @[@"System/Library/Fonts/HelveticaNeue.ttc",
        @"System/Library/Frameworks/ApplicationServices.framework/Frameworks/ATS.framework/Resources/ProtectedFonts/HelveticaNeue.ttc",
        @"System/Library/Frameworks/ApplicationServices.framework/Frameworks/ATS.framework/Resources/FontInfo/HelveticaNeue.ttc.154A98_0.ATSD",
        @"System/Library/Frameworks/ApplicationServices.framework/Frameworks/ATS.framework/Resources/FontInfo/HelveticaNeue.ttc.154A98_0.fontinfo",
        @"System/Library/Extensions/AppleHDA.kext/Contents/PlugIns/AppleHDAPlatformDriver.kext",
        @"System/Library/Extensions/GeForce8xxxGLDriver.bundle",@"System/Library/MonitorPanels/Rotate.monitorPanel",
        @"System/Library/CoreServices/Install Helper.app",@"private/var/db/BootCache.playlist"])
        RemoveLeopardUpdatePath(target,path,YES);
    if(!ExistingGuestPath(target,@"usr/local/libexec/batteryd",YES))
        RemoveLeopardUpdatePath(target,@"System/Library/LaunchDaemons/com.apple.batteryd.plist",NO);
    NSString *nvidia=@"System/Library/Extensions/NVDAGF100Hal.kext";
    if([Plist(At(target.root,[nvidia stringByAppendingPathComponent:@"Contents/Info.plist"]))[@"CFBundleShortVersionString"] isEqual:@"1.3.4"])
        RemoveLeopardUpdatePath(target,nvidia,YES);
    // The base has no user-customized Samba configuration or Mail settings.
    [target file:@"private/etc/smb.conf" data:Read(At(target.root,@"private/etc/smb.conf.template")) mode:0644];
    SetUpdateConfigurationValue(target,@"private/etc/postfix/main.cf",@"tls_random_source",@"dev:/dev/urandom");
    NSDictionary *coreAudio=Plist(At(target.root,@"private/var/db/dslocal/nodes/Default/users/_coreaudiod.plist"));
    for(NSString *name in @[@"com.apple.audio.DeviceSettings.plist",@"com.apple.audio.SystemSettings.plist"]) {
        NSString *path=[@"Library/Preferences/Audio" stringByAppendingPathComponent:name];
        NSURL *file=ExistingGuestPath(target,path,YES);if(!file)continue;
        Require([coreAudio[@"uid"] count]&&[coreAudio[@"gid"] count],@"The original Core Audio account is missing.");
        struct stat info;Require(!lstat(file.fileSystemRepresentation,&info),@"Could not inspect the audio preferences.");
        target.metadata[path]=@[@([coreAudio[@"uid"][0] intValue]),@([coreAudio[@"gid"][0] intValue]),@(info.st_mode)];
        Require(!removexattr(file.fileSystemRepresentation,"com.apple.quarantine",XATTR_NOFOLLOW)||errno==ENOATTR,
            @"Could not prepare the audio preference metadata.");
    }
    NSString *browser=@"Library/Application Support/iLifeMediaBrowser";
    if(ExistingGuestPath(target,browser,YES)) {
        [target record:browser uid:0 gid:0 receipt:NO];NSString *prefix=[browser stringByAppendingString:@"/"];
        for(NSString *path in target.metadata.allKeys)if([path isEqual:browser]||[path hasPrefix:prefix]) {
            mode_t mode=[target.metadata[path][2] unsignedShortValue];
            if(S_ISDIR(mode)||S_ISREG(mode))target.metadata[path]=@[@0,@0,@((mode&S_IFMT)|0755)];
        }
    }
}
static void EnableUpdateMRT(VIMInstallTarget *target) {
    // Apple's postflight activates these bundled services. A fresh offline
    // system needs no unload/reload, only the same persistent enabled state.
    for(NSString *path in @[@"System/Library/LaunchDaemons/com.apple.mrt.plist",@"System/Library/LaunchAgents/com.apple.mrt.uiagent.plist"]) {
        NSMutableDictionary *settings=[Plist(At(target.root,path)) mutableCopy];
        if(settings&&[settings[@"Disabled"] boolValue]) {
            settings[@"Disabled"]=@NO;[target plist:path value:settings mode:0644];
        }
    }
}
static void SnowSecurityActions(VIMInstallTarget *target) {
    NSString *path=@"private/etc/apache2/httpd.conf";
    if(ExistingGuestPath(target,path,YES)&&ExistingGuestPath(target,@"usr/libexec/apache2/mod_hfs_apple.so",YES)) {
        NSString *text=[[NSString alloc] initWithData:Read(At(target.root,path)) encoding:NSUTF8StringEncoding];
        Require(text!=nil,@"The Apache configuration could not be read.");
        NSMutableArray *lines=[[text componentsSeparatedByString:@"\n"] mutableCopy];
        NSRegularExpression *bonjour=[NSRegularExpression regularExpressionWithPattern:@"LoadModule\\s+bonjour_module" options:0 error:nil];
        NSRegularExpression *hfs=[NSRegularExpression regularExpressionWithPattern:@"LoadModule\\s+hfs_apple_module" options:0 error:nil];
        NSUInteger insert=NSNotFound;BOOL present=NO;
        for(NSUInteger i=0;i<lines.count;i++) {
            NSString *line=lines[i];NSRange range=NSMakeRange(0,line.length);
            if([bonjour firstMatchInString:line options:0 range:range])insert=i+1;
            if([hfs firstMatchInString:line options:0 range:range])present=YES;
        }
        if(!present&&insert!=NSNotFound) {
            [lines insertObject:@"LoadModule hfs_apple_module libexec/apache2/mod_hfs_apple.so" atIndex:insert];
            [target file:path data:[[lines componentsJoinedByString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding] mode:0644];
        }
    }
    EnableUpdateMRT(target);
}
static NSArray *ApplySnowLeopardUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    NSArray *recipe=VIMFlatHostUpdateRecipe(@"snowleopard");
    Require(inputs.count==recipe.count,@"Choose the complete original Snow Leopard OS update sequence.");
    NSString *systemVersion=[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"];
    Require([Plist(At(target.root,systemVersion))[@"ProductBuildVersion"] isEqual:@"10J3250"],
        @"This update recipe requires the original Snow Leopard catalog installer.");
    NSURL *work=At(attempt,@"SnowLeopardUpdates");Directory(work);
    @try {
        Progress(attempt,@"snow-update-verify",@"Checking the optional Snow Leopard updates…",.80,.805,-1,12);
        NSArray *packages=nil;
        @try {packages=PrepareFlatUpdateInputs(inputs,recipe,@"Snow Leopard",work,attempt);}
        @catch(NSException *failure) {
            Checkpoint(attempt);
            if(![failure.name isEqual:@"OptionalHostUpdateUnavailable"])@throw;
            fprintf(stderr,"Optional Snow Leopard updates were not applied: %s\n",failure.reason.UTF8String);
            WritePlist(At(attempt,@"HostUpdateNotice.plist"),@{@"Status":@"original-system-kept",
                @"Reason":@"Additional Snow Leopard updates could not be prepared. The original system was kept."});
            return @[];
        }
        uint64_t total=0,done=0;
        for(NSDictionary *package in packages)total+=[NSFileManager.defaultManager attributesOfItemAtPath:
            At(package[@"Folder"],@"Payload").path error:nil].fileSize;
        Require(total>0,@"The original Snow Leopard update payloads are missing.");
        for(NSUInteger index=0;index<packages.count;index++) {
            NSDictionary *package=packages[index];Checkpoint(attempt);
            if([recipe[index][@"Role"] isEqual:@"Security"])SnowComboActions(target,packages.firstObject);
            NSURL *payload=At(package[@"Folder"],@"Payload");
            uint64_t bytes=[NSFileManager.defaultManager attributesOfItemAtPath:payload.path error:nil].fileSize;
            double start=.805+.035*(double)done/total,end=.805+.035*(double)(done+bytes)/total;
            Progress(attempt,@"snow-update",@"Installing system updates…",start,end,-1,MAX(.5,90.*bytes/total));
            NSDictionary *metadata=package[@"Metadata"];NSMutableSet *visited=[NSMutableSet new];
            for(NSString *path in metadata) {
                Checkpoint(attempt);mode_t mode=[metadata[path][2] unsignedShortValue];
                OpenUpdateDirectory(target,S_ISDIR(mode)?path:path.stringByDeletingLastPathComponent,visited);
            }
            PayloadProgress progress={.attempt=attempt,.cancel=At(attempt,@"Cancel"),.stage=@"snow-update",
                .title=@"Installing system updates…",.start=start,.end=end};
            ExtractFlatPayload(payload,target.root,&progress);[target.metadata addEntriesFromDictionary:metadata];
            WriteFlatUpdateReceipt(target,package);done+=bytes;
        }
        Require([Plist(At(target.root,systemVersion))[@"ProductVersion"] isEqual:@"10.6.8"],
            @"Snow Leopard did not reach the expected OS version.");
        SnowSecurityActions(target);
        Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
        target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
        return @[@"zzz041-98179",@"zzz041-91751"];
    } @finally {Remove(work);}
}

static void LionSecurityActions(VIMInstallTarget *target,NSDictionary *package) {
    LeopardCleanupList(target,At(package[@"Folder"],@"ScriptFiles/Tools/cleanList"),@"",YES);
    // The deferred live-install shove also deletes this obsolete resource.
    // Offline extraction already writes every payload at its final path.
    RemoveLeopardUpdatePath(target,@"System/Library/CoreServices/loginwindow.app/Contents/Resources/Restart.tif",NO);
    NSString *path=@"private/etc/apache2/httpd.conf";
    if(ExistingGuestPath(target,path,YES)&&ExistingGuestPath(target,@"usr/libexec/apache2/mod_hfs_apple.so",YES)) {
        NSData *original=Read(At(target.root,path));
        NSString *text=[[NSString alloc] initWithData:original encoding:NSUTF8StringEncoding];
        Require(text!=nil,@"The Apache configuration could not be read.");
        NSMutableArray *lines=[[text componentsSeparatedByString:@"\n"] mutableCopy];
        NSRegularExpression *hfs=[NSRegularExpression regularExpressionWithPattern:@"LoadModule\\s+hfs_apple_module" options:0 error:nil];
        NSUInteger common=NSNotFound,server=NSNotFound;
        for(NSUInteger i=0;i<lines.count;i++) {
            NSString *line=lines[i];
            if([line containsString:@"Apple specific modules"])common=i;
            if(i&&[hfs firstMatchInString:line options:0 range:NSMakeRange(0,line.length)]&&
               [lines[i-1] containsString:@"<IfDefine MACOSXSERVER>"])server=i;
        }
        // Match the original script: only move its server-conditional entry
        // when both original anchors exist; never invent a duplicate module.
        if(common!=NSNotFound&&server!=NSNotFound) {
            lines[server]=@"";
            [lines insertObject:@"LoadModule hfs_apple_module libexec/apache2/mod_hfs_apple.so" atIndex:common+1];
            [target file:[path stringByAppendingString:@".pre-update"] data:original mode:0644];
            [target file:path data:[[lines componentsJoinedByString:@"\n"] dataUsingEncoding:NSUTF8StringEncoding] mode:0644];
        }
    }
    EnableUpdateMRT(target);
}
static void HideUpdatedKernel(VIMInstallTarget *target) {
    NSURL *kernel=ExistingGuestPath(target,@"mach_kernel",YES);struct stat info;
    if(kernel)Require(!lstat(kernel.fileSystemRepresentation,&info)&&
        !lchflags(kernel.fileSystemRepresentation,info.st_flags|UF_HIDDEN),@"Could not hide the updated guest kernel.");
}
static void MountainLionSecurityActions(VIMInstallTarget *target,NSDictionary *package) {
    NSString *preferences=@"private/var/db/SystemPolicy-prefs.plist";
    NSMutableDictionary *prefs=[Plist(At(target.root,preferences)) mutableCopy];
    BOOL reset=[prefs[@"enabled"] isEqual:@"no"];
    NSURL *database=ExistingGuestPath(target,@"private/var/db/SystemPolicy",YES);
    if(database) {
        // Edit a private working copy, not the host's policy database and not
        // the package's .SystemPolicy-default. Do not leave SQLite journals
        // or host-owned temporary files inside the unpublished guest volume.
        struct stat info;Require(!lstat(database.fileSystemRepresentation,&info)&&S_ISREG(info.st_mode),
            @"The guest's system policy database is not a regular file.");
        NSURL *copy=At(package[@"Folder"],@"SystemPolicy.sqlite");Write(copy,Read(database));
        sqlite3 *db=NULL;sqlite3_stmt *statement=NULL;
        @try {
            // Like the receipt writer, use POSIX realpath for the parent:
            // NSURL can retain /var's host alias, which SQLite NOFOLLOW rejects.
            char parent[PATH_MAX],canonical[PATH_MAX];
            Require(realpath(copy.URLByDeletingLastPathComponent.fileSystemRepresentation,parent)!=NULL,
                @"Could not locate the private system policy working directory.");
            int length=snprintf(canonical,sizeof(canonical),"%s/%s",parent,copy.lastPathComponent.fileSystemRepresentation);
            Require(length>=0&&(size_t)length<sizeof(canonical),@"The private system policy path is too long.");
            Require(sqlite3_open_v2(canonical,&db,
                SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOFOLLOW,NULL)==SQLITE_OK,@"Could not open the guest's system policy working copy.");
            Require(sqlite3_prepare_v2(db,"SELECT name FROM sqlite_master WHERE type='table'",-1,&statement,NULL)==SQLITE_OK,
                @"Could not read the guest's system policy tables.");
            NSMutableSet *tables=[NSMutableSet new];int result;
            while((result=sqlite3_step(statement))==SQLITE_ROW) {
                const char *name=(const char *)sqlite3_column_text(statement,0);
                if(name)[tables addObject:@(name)];
            }
            Require(result==SQLITE_DONE,@"Could not read the guest's system policy schema.");
            sqlite3_finalize(statement);statement=NULL;
            BOOL edited=[tables containsObject:@"object"]||(reset&&[tables containsObject:@"authority"]);
            if(edited) {
                Require(sqlite3_exec(db,"BEGIN IMMEDIATE",NULL,NULL,NULL)==SQLITE_OK,@"Could not prepare the guest's policy update.");
                if([tables containsObject:@"object"])
                    Require(sqlite3_exec(db,"DELETE FROM object",NULL,NULL,NULL)==SQLITE_OK,@"Could not clear the guest's cached assessments.");
                if(reset&&[tables containsObject:@"authority"])
                    Require(sqlite3_exec(db,"UPDATE authority SET disabled=0 WHERE label='Developer ID'",NULL,NULL,NULL)==SQLITE_OK,
                        @"Could not enable the guest's native Developer ID policy.");
                Require(sqlite3_exec(db,"COMMIT",NULL,NULL,NULL)==SQLITE_OK,@"Could not finish the guest's policy update.");
            }
            Require(sqlite3_close(db)==SQLITE_OK,@"Could not close the guest's policy update.");db=NULL;
            if(edited) {
                NSString *relative=[database.path substringFromIndex:target.root.path.length+1];
                NSArray *metadata=target.metadata[relative];
                [target file:relative data:Read(copy) mode:info.st_mode&07777];
                if(metadata)target.metadata[relative]=metadata;
            }
        } @finally {if(statement)sqlite3_finalize(statement);if(db)sqlite3_close(db);Remove(copy);}
    }
    // A pristine base may not have created its active policy DB yet. Preserve
    // that native first-start initialization instead of creating an empty DB.
    if(reset){prefs[@"enabled"]=@"yes";[target plist:preferences value:prefs mode:0644];}
    HideUpdatedKernel(target);
}
static void MavericksSecurityActions(VIMInstallTarget *target,NSDictionary *package) {
    LeopardCleanupList(target,At(package[@"Folder"],@"ScriptFiles/Tools/cleanListSUConstructed"),@"",NO);
    RemoveLeopardUpdatePath(target,@"Library/Internet Plug-Ins/QuickTime Plugin.plugin",YES);
    // Reproduce cleanIA's fixed rm -f/rmdir operations, not a recursive
    // deletion of an installer which may still contain the user's media.
    NSString *assistant=@"Install OS X Mavericks.app/Contents";
    NSMutableArray<NSString *> *directories=[NSMutableArray arrayWithObject:assistant];
    NSMutableArray<NSString *> *files=[NSMutableArray arrayWithArray:@[@"_CodeSignature/CodeResources",@"Info.plist",
        @"MacOS/Install OS X Mavericks",@"version.plist",@"Resources/createinstallmedia",@"Resources/InstallAssistantTool"]];
    for(NSString *bundle in @[@"DM_10_6.IABundle",@"IA.bundle",@"IACoreStorage.IABundle"]) {
        NSString *path=[@"PlugIns" stringByAppendingPathComponent:bundle];
        NSString *binary=[bundle stringByDeletingPathExtension];
        for(NSString *leaf in @[@"_CodeSignature/CodeResources",@"Info.plist",@"version.plist",
                               [@"MacOS" stringByAppendingPathComponent:binary]])
            [files addObject:[path stringByAppendingPathComponent:[@"Contents" stringByAppendingPathComponent:leaf]]];
        [directories addObject:[assistant stringByAppendingPathComponent:[path stringByAppendingPathComponent:@"Contents"]]];
    }
    [files addObject:@"PlugIns/IA.bundle/Contents/MacOS/libBaseIA.dylib"];
    for(NSString *leaf in @[@"_CodeSignature/CodeResources",@"MacOS/FindReaperFiles",@"version.plist"])
        [files addObject:[@"Resources/FindReaperFiles.app/Contents" stringByAppendingPathComponent:leaf]];
    [directories addObject:[assistant stringByAppendingPathComponent:@"Resources/FindReaperFiles.app/Contents"]];
    for(NSString *leaf in files)RemoveLeopardUpdatePath(target,[assistant stringByAppendingPathComponent:leaf],NO);
    for(NSString *contents in directories.reverseObjectEnumerator) {
        for(NSString *leaf in @[@"_CodeSignature",@"MacOS",@"Resources",@"PlugIns"])
            RemoveLeopardUpdatePath(target,[contents stringByAppendingPathComponent:leaf],NO);
        RemoveLeopardUpdatePath(target,contents,NO);
        RemoveLeopardUpdatePath(target,contents.stringByDeletingLastPathComponent,NO);
    }
    HideUpdatedKernel(target);
}
static void OSSecurityCleanup(VIMInstallTarget *target,NSDictionary *package,BOOL beforePayload,NSString *guest) {
    BOOL canonicalOnly=![guest isEqual:@"yosemite"],kextContents=[guest isEqual:@"sierra"];
    NSString *list=[[NSString alloc] initWithData:Read(At(package[@"Folder"],@"ScriptFiles/Tools/cleanListSUConstructed"))
        encoding:NSUTF8StringEncoding];
    Require(list!=nil,@"Could not read the original OS update cleanup list.");
    // Apple's preflight removes XPC contents (and Sierra kext contents) before overlay; postflight
    // removes the other obsolete paths. Keep that ordering, not a blanket
    // cleanup after extraction which could delete a replacement XPC service.
    [list enumerateLinesUsingBlock:^(NSString *line,__unused BOOL *stop) {
        BOOL before=[line containsString:@".xpc/"]||(kextContents&&[line containsString:@".kext/"]);
        if(!line.length||before!=beforePayload)return;
        for(NSString *path in LeopardGuestPattern(target,line)) {
            if(canonicalOnly) {
                NSURL *actual=ExistingGuestPath(target,path,NO);
                // Components were normalized by LeopardGuestPattern. Do not
                // use stringByStandardizingPath: it folds the host's /private
                // alias and would incorrectly skip canonical guest paths.
                if(!actual||![At(target.root,path).path isEqual:actual.path])continue;
            }
            RemoveLeopardUpdatePath(target,path,YES);
        }
    }];
    if(beforePayload)
        for(NSString *path in @[@"usr/local/sbin/launchd.development",@"System/Library/Kernels/kernel.development",
            @"System/Library/Caches/com.apple.kext.caches/Startup/kernelcache.development",
            @"System/Library/PrelinkedKernels/prelinkedkernel.development"])
            RemoveLeopardUpdatePath(target,path,NO);
}
static void YosemiteSecurityActions(VIMInstallTarget *target,NSDictionary *package) {
    OSSecurityCleanup(target,package,NO,@"yosemite");
    for(NSString *path in @[@"private/var/db/BootCache.playlist",@"private/var/db/spindump/UUIDToBinaryLocations"])
        RemoveLeopardUpdatePath(target,path,NO);
    RemoveLeopardUpdatePath(target,@"Library/Internet Plug-Ins/QuickTime Plugin.plugin",YES);
    NSString *firmware=@"Payloads/External/System/Library/CoreServices/FirmwareUpdates";
    RemoveLeopardUpdatePath(target,firmware,YES);
    for(NSString *parent=firmware.stringByDeletingLastPathComponent;parent.length;parent=parent.stringByDeletingLastPathComponent)
        RemoveLeopardUpdatePath(target,parent,NO);
    // Native fixPhotosFrameworks does not remove frameworks when Photos is
    // installed. Preserve that condition, including its retained nested ones.
    NSURL *photos=ExistingGuestPath(target,@"Applications/Photos.app",YES);BOOL directory=NO;
    BOOL hasPhotos=photos&&[NSFileManager.defaultManager fileExistsAtPath:photos.path isDirectory:&directory]&&directory;
    if(!hasPhotos) {
        for(NSString *path in @[@"System/Library/LaunchAgents/com.apple.cloudphotosd.plist",
            @"System/Library/LaunchAgents/com.apple.photolibraryd.plist",
            @"System/Library/LaunchAgents/com.apple.PhotoLibraryMigrationUtlility.XPC.plist",
            @"System/Library/PrivateFrameworks/CloudPhotoLibrary.framework",
            @"System/Library/PrivateFrameworks/PhotoLibraryPrivate.framework",
            @"System/Library/CoreServices/Photo Library Migration Utility.app",
            @"Library/Application Support/Apple/Photos/Print Products"])
            RemoveLeopardUpdatePath(target,path,YES);
        for(NSString *framework in @[@"CloudPhotoServices.framework",@"PhotoLibrary.framework"]) {
            NSString *root=[@"System/Library/PrivateFrameworks" stringByAppendingPathComponent:framework];
            for(NSString *suffix in @[@"",@"Versions",@"Versions/A"]) {
                NSString *relative=[root stringByAppendingPathComponent:suffix];
                NSURL *folder=ExistingGuestPath(target,relative,YES);if(!folder)continue;
                NSArray *kept=[suffix isEqual:@"Versions"]?@[@"A",@"Current"]:
                    suffix.length?@[@"Frameworks"]:@[@"Frameworks",@"Versions"];
                for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:folder.path error:nil])
                    if(![kept containsObject:name])RemoveLeopardUpdatePath(target,[relative stringByAppendingPathComponent:name],YES);
            }
        }
    }
    NSURL *itunes=ExistingGuestPath(target,@"Applications/iTunes.app",NO);
    if(itunes)Require(!removexattr(itunes.fileSystemRepresentation,"com.apple.writer_bundle_identifier",XATTR_NOFOLLOW)||errno==ENOATTR,
        @"Could not remove the obsolete guest application attribute.");
    // Offline equivalents of activateEFI and touchEFILoginResources:
    // stage boot.efi here; build both caches once from the final root later.
    Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
    target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
    // loadStackshot is a live-host launchctl action, not an offline mutation.
    // The staged guest's launchd loads its own daemon at first boot. Version
    // migration/hostname scripts have no old user state on a fresh install;
    // StageAccount supplies native current-version first-login preferences.
}
static void ProtectedHFSSecurityActions(VIMInstallTarget *target,NSDictionary *package,NSString *guest) {
    OSSecurityCleanup(target,package,NO,guest);
    if([guest isEqual:@"sierra"]) {
        // Native postflight removes these directories only when empty. Never
        // recursively erase a surviving replacement, or follow an alias.
        NSString *list=[[NSString alloc] initWithData:Read(At(package[@"Folder"],@"ScriptFiles/Tools/cleanDirectories"))
            encoding:NSUTF8StringEncoding];
        Require(list!=nil,@"Could not read the original OS update directory cleanup list.");
        [list enumerateLinesUsingBlock:^(NSString *line,__unused BOOL *stop) {
            if(!line.length)return;
            for(NSString *path in LeopardGuestPattern(target,line)) {
                NSURL *actual=ExistingGuestPath(target,path,NO);
                if(actual&&[At(target.root,path).path isEqual:actual.path])RemoveLeopardUpdatePath(target,path,NO);
            }
        }];
    }
    for(NSString *path in @[@"private/var/db/BootCache.playlist",@"private/var/db/spindump/UUIDToBinaryLocations"])
        RemoveLeopardUpdatePath(target,path,NO);
    NSString *firmware=@"Payloads/External/System/Library/CoreServices/FirmwareUpdates";
    RemoveLeopardUpdatePath(target,firmware,YES);
    for(NSString *parent=firmware.stringByDeletingLastPathComponent;parent.length;parent=parent.stringByDeletingLastPathComponent)
        RemoveLeopardUpdatePath(target,parent,NO);
    if(!target.systemFlags)target.systemFlags=[NSMutableDictionary new];
    for(NSString *path in @[@"private",@"private/var",@"private/var/db",@"private/var/db/dyld"]) {
        NSURL *file=ExistingGuestPath(target,path,YES);struct stat info;
        Require(file&&!lstat(file.fileSystemRepresentation,&info),@"The guest's protected dyld directory is missing.");
        if(!target.metadata[path])target.metadata[path]=@[@(info.st_uid),@(info.st_gid),@(info.st_mode)];
        // HFSPlusBSDInfo.adminFlags stores SF_* at bits16–23. Do not run
        // privileged chflags on a host-mounted image with SIP enabled.
        target.systemFlags[path]=@(SF_NOUNLINK>>16);
    }
    for(NSString *path in @[@"System/Library/Extensions",@"Library/Extensions",
        @"System/Library/PrivateFrameworks/EFILogin.framework/Resources/EFIResourceBuilder.bundle/Contents/Resources"]) {
        NSURL *file=ExistingGuestPath(target,path,YES);
        if(file)Require(!utimensat(AT_FDCWD,file.fileSystemRepresentation,NULL,0),@"Could not update a guest boot-resource timestamp.");
    }
    Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
    target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
    // loadStackshot belongs to guest launchd. Do not run the package's live
    // nvram deletion or a physical-Mac firmware flasher against the host.
    // Fresh guest NVRAM has no SkipBluetoothAutomaticFirmwareUpdate key.
    // Sierra's resetSIP only targets physical MacBookPro13,2/13,3 on 16B2657
    // or 16B2659, not this virtual 16G29 root. MobileAsset's com.apple.rootless
    // classification has no enforcing role with this appliance's guest SIP
    // policy; do not require host rootless-install privileges to set it.
}
static NSArray *ApplyOSSecurityUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs,NSString *guest) {
    NSDictionary *policy=@{@"lion":@{@"Work":@"LionUpdates",@"Version":@"10.7.5",@"Build":@"11G63"},
        @"mountainlion":@{@"Work":@"MountainLionUpdates",@"Version":@"10.8.5",@"Build":@"12F2560"},
        @"mavericks":@{@"Work":@"MavericksUpdates",@"Version":@"10.9.5",@"Build":@"13F1911"},
        @"yosemite":@{@"Work":@"YosemiteUpdates",@"Version":@"10.10.5",@"Build":@"14F2511"},
        @"elcapitan":@{@"Work":@"ElCapitanUpdates",@"Version":@"10.11.6",@"Build":@"15G22010"},
        @"sierra":@{@"Work":@"SierraUpdates",@"Version":@"10.12.6",@"Build":@"16G2136"}}[guest];
    Require(policy!=nil,@"This OS security update has no inventoried native actions.");
    NSDictionary *profile=RestoreProfile(guest);NSString *title=profile[@"Name"];
    NSArray *recipe=VIMFlatHostUpdateRecipe(guest);
    Require(recipe.count&&inputs.count==recipe.count,[NSString stringWithFormat:@"Choose the original %@ OS security update.",title]);
    NSString *systemVersion=[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"];
    Require([Plist(At(target.root,systemVersion))[@"ProductBuildVersion"] isEqual:profile[@"Build"]],
        [NSString stringWithFormat:@"This update recipe requires the original %@ catalog installer.",title]);
    NSURL *work=At(attempt,policy[@"Work"]);Directory(work);
    NSString *stage=[guest stringByAppendingString:@"-update"];
    @try {
        Progress(attempt,[stage stringByAppendingString:@"-verify"],[NSString stringWithFormat:@"Checking the optional %@ update…",title],.80,.805,-1,4);
        NSArray *packages=nil;
        @try {packages=PrepareFlatUpdateInputs(inputs,recipe,title,work,attempt);}
        @catch(NSException *failure) {
            Checkpoint(attempt);
            if(![failure.name isEqual:@"OptionalHostUpdateUnavailable"])@throw;
            fprintf(stderr,"Optional %s update was not applied: %s\n",title.UTF8String,failure.reason.UTF8String);
            WritePlist(At(attempt,@"HostUpdateNotice.plist"),@{@"Status":@"original-system-kept",
                @"Reason":[NSString stringWithFormat:@"The additional %@ update could not be prepared. The original system was kept.",title]});
            return @[];
        }
        NSDictionary *security=packages.firstObject;Checkpoint(attempt);
        NSString *action=@"Installing security update…";
        // Nested media reserves .82–.83 for account/graphics and .83–.87
        // for prelink. Do not make the next phase run backwards after an update.
        double end=[profile[@"NestedSystem"] boolValue]?.82:.84;
        Progress(attempt,stage,action,.805,end,-1,15);
        BOOL elCapitan=[guest isEqual:@"elcapitan"];
        BOOL sierra=[guest isEqual:@"sierra"];
        if([guest isEqual:@"yosemite"]||elCapitan||sierra)OSSecurityCleanup(target,security,YES,guest);
        if(elCapitan) {
            // Native XProtectRemovalPre: remove old files before the updated
            // payload replaces them; no host launchctl unload is appropriate.
            for(NSString *path in @[@"System/Library/CoreServices/MRTAgent.app",@"usr/libexec/MRT",
                @"System/Library/LaunchAgents/com.apple.mrt.uiagent.plist",@"System/Library/LaunchDaemons/com.apple.mrt.plist"])
                RemoveLeopardUpdatePath(target,path,NO);
        }
        uint64_t total=0,done=0;
        for(NSDictionary *package in packages)total+=[NSFileManager.defaultManager attributesOfItemAtPath:
            At(package[@"Folder"],@"Payload").path error:nil].fileSize;
        Require(total>0,@"The original OS update payloads are missing.");
        for(NSDictionary *package in packages) {
            NSDictionary *metadata=package[@"Metadata"];NSMutableSet *visited=[NSMutableSet new];
            for(NSString *path in metadata) {
                Checkpoint(attempt);mode_t mode=[metadata[path][2] unsignedShortValue];
                OpenUpdateDirectory(target,S_ISDIR(mode)?path:path.stringByDeletingLastPathComponent,visited);
            }
            NSURL *payload=At(package[@"Folder"],@"Payload");
            uint64_t bytes=[NSFileManager.defaultManager attributesOfItemAtPath:payload.path error:nil].fileSize;
            double start=.805+(end-.805)*(double)done/total,stop=.805+(end-.805)*(double)(done+bytes)/total;
            Progress(attempt,stage,action,start,stop,-1,MAX(.5,15.*bytes/total));
            PayloadProgress progress={.attempt=attempt,.cancel=At(attempt,@"Cancel"),.stage=stage,
                .title=action,.start=start,.end=stop,.expected=bytes};
            ExtractFlatPayload(payload,target.root,&progress);[target.metadata addEntriesFromDictionary:metadata];
            WriteFlatUpdateReceipt(target,package);done+=bytes;
        }
        if([guest isEqual:@"lion"])LionSecurityActions(target,security);
        else if([guest isEqual:@"mountainlion"])MountainLionSecurityActions(target,security);
        else if([guest isEqual:@"mavericks"])MavericksSecurityActions(target,security);
        else if(elCapitan||sierra)ProtectedHFSSecurityActions(target,security,guest);
        else YosemiteSecurityActions(target,security);
        NSDictionary *installed=Plist(At(target.root,systemVersion));
        Require([installed[@"ProductVersion"] isEqual:policy[@"Version"]]&&
            [installed[@"ProductBuildVersion"] isEqual:policy[@"Build"]],
            [NSString stringWithFormat:@"%@ did not reach the expected OS security update.",title]);
        return [[NSOrderedSet orderedSetWithArray:[recipe valueForKey:@"Product"]] array];
    } @finally {Remove(work);}
}
static NSArray *ApplyLionUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    return ApplyOSSecurityUpdates(target,attempt,inputs,@"lion");
}
static NSArray *ApplyMountainLionUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    return ApplyOSSecurityUpdates(target,attempt,inputs,@"mountainlion");
}
static NSArray *ApplyMavericksUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    return ApplyOSSecurityUpdates(target,attempt,inputs,@"mavericks");
}
static NSArray *ApplyYosemiteUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    return ApplyOSSecurityUpdates(target,attempt,inputs,@"yosemite");
}
static NSArray *ApplyElCapitanUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    return ApplyOSSecurityUpdates(target,attempt,inputs,@"elcapitan");
}
static NSArray *ApplySierraUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs) {
    return ApplyOSSecurityUpdates(target,attempt,inputs,@"sierra");
}

static void ValidateUpdatedRecovery(NSURL *image,NSURL *attempt,NSString *version,NSString *build);

static void MojaveSecurityActions(VIMInstallTarget *target,NSDictionary *package,BOOL before) {
    NSURL *folder=package[@"Folder"];
    if(before) {
        LeopardCleanupList(target,At(folder,@"cleanListSUConstructed"),@"",YES);return;
    }
    LeopardCleanupList(target,At(folder,@"cleanDirectories"),@"",NO);
    NSString *postfix=@"private/etc/postfix/main.cf";NSURL *config=ExistingGuestPath(target,postfix,YES);
    if(config) {
        NSString *checksum=[[NSString alloc] initWithData:Run(@"/usr/bin/cksum",@[config.path],nil,YES) encoding:NSUTF8StringEncoding];
        if([[checksum componentsSeparatedByString:@" "].firstObject isEqual:@"4045903023"])
            [target file:postfix data:Read(At(folder,@"ScriptFiles/Tools/main.cf")) mode:0644];
    }
    for(NSString *path in @[@"System/Library/Templates/Data/private/var/log/DiagnosticMessages",@"private/var/log/DiagnosticMessages"]) {
        NSURL *file=ExistingGuestPath(target,path,YES);if(!file)continue;
        Require(!chmod(file.fileSystemRepresentation,0750),@"Could not prepare the guest diagnostic-log directory.");
        NSMutableArray *metadata=[target.metadata[path] mutableCopy]?:[@[@0,@0,@(S_IFDIR|0750)] mutableCopy];
        metadata[2]=@(S_IFDIR|0750);target.metadata[path]=metadata;
    }
    [target file:@"usr/local/.com.apple.installer.keep" data:NSData.data mode:0644];
    Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
    target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
    // cleanApps applies only to specific factory-bundled iWork/iLife builds
    // on two physical boards; those apps are not in this original OS image.
    // No firmware flasher or live-host package script is executed.
}

static void CatalinaSecurityActions(VIMInstallTarget *target,NSDictionary *package,BOOL before) {
    NSURL *folder=package[@"Folder"];
    if(before) {
        LeopardCleanupList(target,At(folder,@"cleanListSUConstructed"),@"",YES);return;
    }
    LeopardCleanupList(target,At(folder,@"cleanDirectories"),@"",NO);
    for(NSString *name in @[@"MobileDevice",@"AirTrafficHost",@"DeviceLink"])
        RemoveLeopardUpdatePath(target,[NSString stringWithFormat:@"System/Library/PrivateFrameworks/%@.framework 1",name],YES);
    for(NSString *path in @[@"Applications",@"System/Library/Templates/Data/Applications"]) {
        NSURL *file=ExistingGuestPath(target,path,YES);struct stat info;
        Require(file&&!lstat(file.fileSystemRepresentation,&info)&&S_ISDIR(info.st_mode),
            @"A guest applications directory is unavailable.");
        mode_t mode=target.metadata[path]?[target.metadata[path][2] unsignedShortValue]:info.st_mode;
        mode|=S_IWGRP;
        Require(!chmod(file.fileSystemRepresentation,(info.st_mode&07777)|S_IWGRP),
            @"Could not prepare the guest applications directory.");
        target.metadata[path]=@[@0,@80,@(mode)];
    }
    for(NSString *path in @[@"System/Library/Templates/Data/private/var/log/DiagnosticMessages",@"private/var/log/DiagnosticMessages"]) {
        NSURL *file=ExistingGuestPath(target,path,YES);if(!file)continue;
        Require(!chmod(file.fileSystemRepresentation,0750),@"Could not prepare the guest diagnostic-log directory.");
        NSMutableArray *metadata=[target.metadata[path] mutableCopy]?:[@[@0,@0,@(S_IFDIR|0750)] mutableCopy];
        metadata[2]=@(S_IFDIR|0750);target.metadata[path]=metadata;
    }
    Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
    target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
}

static void HighSierraSecurityActions(VIMInstallTarget *target,NSDictionary *package,BOOL before) {
    NSURL *folder=package[@"Folder"];
    if(before) {
        // These are PackageKit static-obsolete members, not Sierra's script
        // Tools/ lists. Delete obsolete content before overlay, never a new
        // replacement. The final delta has two valid empty lists.
        LeopardCleanupList(target,At(folder,@"cleanListSUConstructed"),@"",YES);
        return;
    }
    LeopardCleanupList(target,At(folder,@"cleanDirectories"),@"",NO);
    NSString *postfix=@"private/etc/postfix/main.cf";
    NSURL *config=ExistingGuestPath(target,postfix,YES);
    if(config) {
        NSString *checksum=[[NSString alloc] initWithData:Run(@"/usr/bin/cksum",@[config.path],nil,YES) encoding:NSUTF8StringEncoding];
        // Apple's postflight replaces only its unmodified stock config.
        if([[checksum componentsSeparatedByString:@" "].firstObject isEqual:@"4045903023"])
            [target file:postfix data:Read(At(folder,@"ScriptFiles/Tools/main.cf")) mode:0644];
    }
    NSString *policy=@"private/var/db/SystemPolicyConfiguration";
    NSString *users=@"private/var/db/dslocal/nodes/Default/users";
    [target directory:policy mode:0755];
    if(!ExistingGuestPath(target,[policy stringByAppendingPathComponent:@".AppleODConvertDone"],YES)) {
        RemoveLeopardUpdatePath(target,@"private/var/db/.configureLocalKDC",NO);
        NSString *pristine=@"System/Library/DirectoryServices/DefaultLocalDB/Default/users";
        NSArray *originalUsersMode=target.metadata[users];
        NSMutableSet *visited=[NSMutableSet new];
        OpenUpdateDirectory(target,pristine,visited);OpenUpdateDirectory(target,users,visited);
        Ditto(@[At(target.root,pristine).path,
            At(target.root,users).path]);
        [target record:users uid:0 gid:0 receipt:NO];
        // cp -R merges into the existing directory without replacing its
        // permissions. Ditto's staging metadata must not change that policy.
        if(originalUsersMode)target.metadata[users]=originalUsersMode;
        [target file:[policy stringByAppendingPathComponent:@".AppleODConvertDone"] data:NSData.data mode:0644];
        // The guest's native configureLocalKDC launch job consumes the absent
        // .configureLocalKDC marker. Never configure the host's KDC here.
    }
    if(!ExistingGuestPath(target,[policy stringByAppendingPathComponent:@".AppleODLegacyDisable"],YES)) {
        for(NSString *name in @[@"_carddav",@"_pcastagent",@"_pcastlibrary",@"_pcastserver",@"_xcsbuildagent",
                               @"_xcscredserver",@"_xgridagent",@"_xgridcontroller"]) {
            NSString *path=[users stringByAppendingPathComponent:[name stringByAppendingPathExtension:@"plist"]];
            NSMutableDictionary *record=[Plist(At(target.root,path)) mutableCopy];
            if(!record)continue;
            record[@"IsHidden"]=@[@"YES"];record[@"passwd"]=@[@"********"];
            [record removeObjectForKey:@"ShadowHashData"];
            [target plist:path value:record mode:0600];
        }
        [target file:[policy stringByAppendingPathComponent:@".AppleODLegacyDisable"] data:NSData.data mode:0644];
    }
    for(NSString *path in @[@"System/Library/Templates/Data/private/var/log/DiagnosticMessages",@"private/var/log/DiagnosticMessages"]) {
        NSURL *file=ExistingGuestPath(target,path,YES);
        if(!file)continue;
        Require(!chmod(file.fileSystemRepresentation,0750),@"Could not prepare the guest diagnostic-log directory.");
        NSMutableArray *metadata=[target.metadata[path] mutableCopy]?:[@[@0,@0,@(S_IFDIR|0750)] mutableCopy];
        metadata[2]=@(S_IFDIR|0750);target.metadata[path]=metadata;
    }
    [target file:@"usr/local/.com.apple.installer.keep" data:NSData.data mode:0644];
    NSURL *resources=ExistingGuestPath(target,
        @"System/Library/PrivateFrameworks/EFILogin.framework/Resources/EFIResourceBuilder.bundle/Contents/Resources",YES);
    if(resources)Require(!utimensat(AT_FDCWD,resources.fileSystemRepresentation,NULL,0),@"Could not refresh guest EFI resources.");
    // Firmware-only im4m remnants are absent in a fresh installation. The
    // flasher scripts are never run. Native publication blesses Preboot later.
    Ditto(@[At(target.root,@"usr/standalone/i386/boot.efi").path,At(target.root,BootEFI).path]);
    target.metadata[BootEFI]=@[@0,@0,@(S_IFREG|0644)];
}

static NSArray *ApplyAPFSOSUpdates(VIMInstallTarget *target,NSURL *attempt,NSDictionary *inputs,NSString *guest) {
    BOOL highSierra=[guest isEqual:@"highsierra"];
    BOOL catalina=[guest isEqual:@"catalina"];
    NSDictionary *policy=@{
        @"highsierra":@{@"Version":@"10.13.6",@"Build":@"17G14042",@"Work":@"HighSierraUpdates"},
        @"mojave":@{@"Version":@"10.14.6",@"Build":@"18G9323",@"Work":@"MojaveUpdates"},
        @"catalina":@{@"Version":@"10.15.8",@"Build":@"19H2036",@"Work":@"CatalinaUpdates"}}[guest];
    Require(policy!=nil,@"This APFS update has no inventoried native actions.");
    NSString *title=RestoreProfile(guest)[@"Name"],*release=policy[@"Version"],*finalBuild=policy[@"Build"];
    NSString *stage=[guest stringByAppendingString:@"-update"];
    NSArray *recipe=VIMFlatHostUpdateRecipe(guest);
    Require(inputs.count==recipe.count,[NSString stringWithFormat:@"Choose the complete original %@ OS update sequence.",title]);
    NSString *version=[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"];
    Require([Plist(At(target.root,version))[@"ProductBuildVersion"] isEqual:RestoreProfile(guest)[@"Build"]],
        [NSString stringWithFormat:@"This update recipe requires the original %@ catalog installer.",title]);
    NSURL *work=At(attempt,policy[@"Work"]);Directory(work);
    @try {
        Progress(attempt,[stage stringByAppendingString:@"-verify"],
            [NSString stringWithFormat:@"Checking the optional %@ OS updates…",title],.80,.805,-1,8);
        NSArray *packages=nil;
        @try {
            packages=PrepareFlatUpdateInputs(inputs,recipe,title,work,attempt);
            ValidateUpdatedRecovery(packages.lastObject[@"RecoveryImage"],attempt,release,finalBuild);
        } @catch(NSException *failure) {
            Checkpoint(attempt);
            if(![failure.name isEqual:@"OptionalHostUpdateUnavailable"])@throw;
            fprintf(stderr,"Optional %s updates were not applied: %s\n",title.UTF8String,failure.reason.UTF8String);
            WritePlist(At(attempt,@"HostUpdateNotice.plist"),@{@"Status":@"original-system-kept",
                @"Reason":[NSString stringWithFormat:@"The additional %@ updates could not be prepared. The original system was kept.",title]});
            return @[];
        }
        uint64_t total=0,done=0;
        for(NSDictionary *package in packages)if(!package[@"RecoveryImage"])
            total+=[NSFileManager.defaultManager attributesOfItemAtPath:At(package[@"Folder"],@"Payload").path error:nil].fileSize;
        Require(total>0,@"The original OS update payloads are missing.");
        for(NSUInteger index=0;index<packages.count;index++) {
            Checkpoint(attempt);NSDictionary *package=packages[index];NSString *role=recipe[index][@"Role"];
            if(package[@"RecoveryImage"]) {
                Ditto(@[[package[@"RecoveryImage"] path],At(attempt,@"UpdatedRecoveryHDMeta.dmg").path]);
                WriteFlatUpdateReceipt(target,package);continue;
            }
            BOOL security=[role isEqual:@"HopSecurity"]||[role isEqual:@"Security"];
            if(highSierra&&[role isEqual:@"Security"])Require([Plist(At(target.root,version))[@"ProductBuildVersion"] isEqual:@"17G14033"],
                @"High Sierra did not complete its required intermediate OS update.");
            if(security) {
                if(highSierra)HighSierraSecurityActions(target,package,YES);
                else if(catalina)CatalinaSecurityActions(target,package,YES);
                else MojaveSecurityActions(target,package,YES);
            }
            NSDictionary *metadata=package[@"Metadata"];NSMutableSet *visited=[NSMutableSet new];
            for(NSString *path in metadata) {
                mode_t mode=[metadata[path][2] unsignedShortValue];
                OpenUpdateDirectory(target,S_ISDIR(mode)?path:path.stringByDeletingLastPathComponent,visited);
            }
            NSURL *payload=At(package[@"Folder"],@"Payload");
            uint64_t bytes=[NSFileManager.defaultManager attributesOfItemAtPath:payload.path error:nil].fileSize;
            double start=.805+.015*(double)done/total,end=.805+.015*(double)(done+bytes)/total;
            NSString *action=@"Installing system updates…";
            Progress(attempt,stage,action,start,end,-1,MAX(.5,40.*bytes/total));
            PayloadProgress progress={.attempt=attempt,.cancel=At(attempt,@"Cancel"),.stage=stage,
                .title=action,.start=start,.end=end,.expected=bytes};
            ExtractFlatPayload(payload,target.root,&progress);[target.metadata addEntriesFromDictionary:metadata];
            WriteFlatUpdateReceipt(target,package);done+=bytes;
            if(security) {
                if(highSierra)HighSierraSecurityActions(target,package,NO);
                else if(catalina)CatalinaSecurityActions(target,package,NO);
                else MojaveSecurityActions(target,package,NO);
                NSString *expected=[role isEqual:@"HopSecurity"]?@"17G14033":finalBuild;
                NSDictionary *installed=Plist(At(target.root,version));
                Require([installed[@"ProductBuildVersion"] isEqual:expected]&&[installed[@"ProductVersion"] isEqual:release],
                    @"The system did not reach the expected OS security update.");
            }
        }
        return [[NSOrderedSet orderedSetWithArray:[recipe valueForKey:@"Product"]] array];
    } @finally {Remove(work);}
}

static NSData *EmbeddedShadowHash(NSData *password,NSString *algorithm,BOOL smb) {
    id record;
    if([algorithm isEqual:@"SALTED-SHA512-PBKDF2"]) {
        uint8_t salt[32],entropy[128];const uint rounds=20000;
        Require(SecRandomCopyBytes(kSecRandomDefault,sizeof(salt),salt)==errSecSuccess&&
                CCKeyDerivationPBKDF(kCCPBKDF2,password.bytes,password.length,salt,sizeof(salt),
                    kCCPRFHmacAlgSHA512,rounds,entropy,sizeof(entropy))==0,
                @"Could not prepare the native account password.");
        record=@{@"salt":[NSData dataWithBytes:salt length:sizeof(salt)],@"iterations":@(rounds),
                 @"entropy":[NSData dataWithBytes:entropy length:sizeof(entropy)]};
    } else {
        uint8_t saltedDigest[4+CC_SHA512_DIGEST_LENGTH];
        Require(SecRandomCopyBytes(kSecRandomDefault,4,saltedDigest)==errSecSuccess,@"Could not generate an account salt.");
        NSMutableData *salted=[NSMutableData dataWithBytes:saltedDigest length:4];[salted appendData:password];
        CC_SHA512(salted.bytes,(CC_LONG)salted.length,saltedDigest+4);
        record=[NSData dataWithBytes:saltedDigest length:sizeof(saltedDigest)];
    }
    NSMutableDictionary *hashes=[@{algorithm:record} mutableCopy];
    if(smb) {
        NSString *text=[[NSString alloc] initWithData:password encoding:NSUTF8StringEncoding];
        NSData *unicode=[text dataUsingEncoding:NSUTF16LittleEndianStringEncoding];
        unsigned char nt[CC_MD4_DIGEST_LENGTH];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        Require(unicode&&CC_MD4(unicode.bytes,(CC_LONG)unicode.length,nt),@"Could not prepare the sharing credential.");
#pragma clang diagnostic pop
        // Lion's authority flag is SMB-NT, but native DirectoryService
        // serializes the corresponding ShadowHashData entry under "NT".
        hashes[@"NT"]=[NSData dataWithBytes:nt length:sizeof(nt)];
    }
    NSError *error=nil;
    NSData *encoded=[NSPropertyListSerialization dataWithPropertyList:hashes format:NSPropertyListBinaryFormat_v1_0
        options:0 error:&error];
    Require(encoded!=nil,error.localizedDescription);return encoded;
}
static uint32_t StageDSLocalAccount(VIMInstallTarget *target,NSString *name,NSString *guid,BOOL smb,
                                    NSData *password,NSString *algorithm) {
    NSString *node=@"private/var/db/dslocal/nodes/Default";
    NSURL *nodeURL=At(target.root,node);struct stat directory;
    if(!lstat(nodeURL.fileSystemRepresentation,&directory)&&S_ISDIR(directory.st_mode)&&!(directory.st_mode&S_IXUSR)) {
        if(!target.metadata[node])target.metadata[node]=@[@0,@0,@(directory.st_mode)];
        Require(!chmod(nodeURL.fileSystemRepresentation,(directory.st_mode&07777)|S_IXUSR),
            @"Could not prepare the installer’s local account directory.");
    }
    NSString *users=[node stringByAppendingPathComponent:@"users"];
    NSError *error=nil;
    NSArray *existing=[NSFileManager.defaultManager contentsOfDirectoryAtPath:At(target.root,users).path error:&error];
    Require(existing!=nil,error.localizedDescription);
    NSMutableIndexSet *used=[NSMutableIndexSet indexSet];
    for(NSString *file in existing)if([file hasSuffix:@".plist"]) {
        NSDictionary *record=Plist(At(target.root,[users stringByAppendingPathComponent:file]));
        for(NSString *old in record[@"name"])
            Require([name caseInsensitiveCompare:old]!=NSOrderedSame,@"The installer already contains this user name.");
        for(NSString *uid in record[@"uid"])if(uid.integerValue>=0&&uid.integerValue<65534)[used addIndex:uid.integerValue];
    }
    uint32_t uid=501;while(uid<65534&&[used containsIndex:uid])uid++;
    Require(uid<65534,@"No local account identifier is available.");
    NSString *home=[@"/Users/" stringByAppendingString:name];
    NSString *authority=algorithm?[NSString stringWithFormat:@";ShadowHash;HASHLIST:<%@%@>",algorithm,smb?@",SMB-NT":@""]:
        (smb?@";ShadowHash;HASHLIST:<SALTED-SHA1,SMB-NT>":@";ShadowHash;");
    NSMutableDictionary *record=[@{@"name":@[name],@"realname":@[name],@"uid":@[@(uid).stringValue],
        @"gid":@[@"20"],@"home":@[home],@"shell":@[@"/bin/bash"],@"passwd":@[@"********"],
        @"generateduid":@[guid],@"authentication_authority":@[authority]} mutableCopy];
    if(algorithm) {
        NSData *shadow=EmbeddedShadowHash(password,algorithm,smb);
        record[@"ShadowHashData"]=@[shadow];
        if(smb) {
            NSDictionary *hashes=[NSPropertyListSerialization propertyListWithData:shadow options:0 format:NULL error:NULL];
            record[@"KerberosKeys"]=@[VIMSMBLocalKeyset(hashes[@"NT"])];
        }
    }
    [target plist:[users stringByAppendingPathComponent:[name stringByAppendingPathExtension:@"plist"]] value:record mode:0600];
    NSString *group=[node stringByAppendingPathComponent:@"groups/admin.plist"];
    NSMutableDictionary *admin=[Plist(At(target.root,group)) mutableCopy];
    Require(admin!=nil,@"The installer is missing its local administrator group.");
    for(NSString *key in @[@"users",@"groupmembers"]) {
        NSMutableArray *entries=[admin[key] mutableCopy]?:[NSMutableArray array];
        [entries addObject:[key isEqual:@"users"]?name:guid];admin[key]=entries;
    }
    [target plist:group value:admin mode:0600];
    // Authenticated users get their native home share. Explicitly publishing
    // that same home marks every descendant as a Shared Folder in Finder.
    // Preserve SMB credentials above, and do not edit any existing shares.
    return uid;
}

#include "VIMGuestLocaleStage.h"
static void StageAccount(VIMInstallTarget *target,NSURL *system,NSDictionary *owner,NSString *name,NSData *password) {
    StageHostLocale(target,system,owner);
    // Disabling Automatic Setup must not silently select an emulated installer
    // or create a hidden/default account. Leave Setup Assistant to the user.
    if(![owner[@"Enabled"] boolValue])return;
    NSString *identityPath=@"Library/Preferences/SystemConfiguration/preferences.plist";
    [target plist:identityPath value:VIMGuestComputerNamePreferences(Plist(At(target.root,identityPath)),name) mode:0644];
    NSString *guest=owner[@"Guest"];
    BOOL tiger=[guest isEqual:@"tiger"];
    BOOL lion=[guest isEqual:@"lion"];
    BOOL mountain=[guest isEqual:@"mountainlion"];
    BOOL mavericks=[guest isEqual:@"mavericks"];
    BOOL bigSur=VIMIsAssistantGuest(guest);
    BOOL miniBuddy=bigSur||[@[@"yosemite",@"elcapitan",@"sierra",@"highsierra",@"mojave",@"catalina"] containsObject:guest];
    NSString *algorithm=lion?@"SALTED-SHA512":((mountain||mavericks||miniBuddy)?@"SALTED-SHA512-PBKDF2":nil);
    BOOL smb=[owner[@"FileSharing"] boolValue];
    BOOL login=[owner[@"AutomaticLogin"] boolValue];
    BOOL ssh=[owner[@"RemoteLogin"] boolValue];
    NSString *guid=NSUUID.UUID.UUIDString,*seed=@"private/var/db/netinfo/local.nidb";
    NSError *error=nil;
    uint32_t uid=tiger?VIMCreateTigerAccount(At(target.root,seed),name,guid,smb,&error):
        StageDSLocalAccount(target,name,guid,smb,password,algorithm);
    Require(uid!=0,error.localizedDescription);
    if(tiger) {
        NSString *prefix=[seed stringByAppendingString:@"/"];
        for(NSString *path in target.metadata.allKeys)if([path hasPrefix:prefix])[target.metadata removeObjectForKey:path];
        [target record:seed uid:0 gid:0 receipt:NO];
    }
    if(!algorithm) {
        [target directory:@"private/var/db/shadow" mode:0700];
        [target directory:@"private/var/db/shadow/hash" mode:0700];
        [target file:[@"private/var/db/shadow/hash" stringByAppendingPathComponent:guid] data:VIMClassicShadowHash(password,smb) mode:0600];
    }
    NSString *home=[@"Users" stringByAppendingPathComponent:name];
    NSURL *templates=At(target.root,bigSur?@"Library/User Template":@"System/Library/User Template");
    NSURL *shared=At(templates,@"Non_localized");
    if([NSFileManager.defaultManager fileExistsAtPath:shared.path])Ditto(@[shared.path,At(target.root,home).path]);
    NSMutableArray *localizations=[NSMutableArray array];
    for(NSString *entry in [NSFileManager.defaultManager contentsOfDirectoryAtPath:templates.path error:nil])
        if([entry.pathExtension isEqual:@"lproj"])[localizations addObject:entry.stringByDeletingPathExtension];
    NSString *selected=VIMGuestLocaleTemplate(localizations,owner[@"HostLocale"])?:@"English";
    Ditto(@[At(templates,[selected stringByAppendingPathExtension:@"lproj"]).path,At(target.root,home).path]);
    // Setup Assistant normally writes these after language selection. A
    // template copy alone leaves ICU's root-locale fallback (e.g. M11).
    NSString *global=[home stringByAppendingPathComponent:@"Library/Preferences/.GlobalPreferences.plist"];
    NSMutableDictionary *locale=[Plist(At(target.root,global)) mutableCopy]?:[NSMutableDictionary dictionary];
    [locale addEntriesFromDictionary:VIMGuestLocalePreferences(owner[@"HostLocale"])];
    [target plist:global value:locale mode:0600];
    // Seed ordinary, user-changeable defaults before first boot. The modern
    // one-shot finalizer also writes the actual guest's CurrentHost domain;
    // it must not guess that ByHost identifier on the build host.
    NSDictionary *idle=@{@"idleTime":@0,@"askForPassword":@NO,@"askForPasswordDelay":@0};
    for(NSString *path in @[[home stringByAppendingPathComponent:@"Library/Preferences/com.apple.screensaver.plist"],
                            @"Library/Preferences/com.apple.screensaver.plist"]) {
        NSMutableDictionary *prefs=[Plist(At(target.root,path)) mutableCopy]?:[NSMutableDictionary dictionary];
        [prefs addEntriesFromDictionary:idle];[target plist:path value:prefs mode:0600];
    }
    if(algorithm) {
        NSMutableDictionary *setup=[@{@"DidSeeCloudSetup":@YES} mutableCopy];
        if(lion)setup[@"GestureMovieSeen"]=@"none";
        else {
            NSDictionary *release=Plist(At(system,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]));
            NSString *version=release[@"ProductVersion"];
            Require(version.length,@"The installed OS version is missing.");
            setup[@"LastSeenCloudProductVersion"]=version;
            if(mavericks||miniBuddy)[setup addEntriesFromDictionary:@{@"LastSeenSyncProductVersion":version,@"DidSeeSyncSetup":@YES,@"DidSeeSyncSetup2":@YES}];
            if(miniBuddy) {
                Require([release[@"ProductBuildVersion"] length],@"The installed OS build is missing.");
                setup[@"LastSeenBuddyBuildVersion"]=release[@"ProductBuildVersion"];
            }
        }
        [target plist:[home stringByAppendingPathComponent:@"Library/Preferences/com.apple.SetupAssistant.plist"]
            value:setup mode:0600];
    }
    [target record:home uid:uid gid:20 receipt:NO];
    [target file:@"private/var/db/.AppleSetupDone" data:NSData.data mode:tiger?0644:0600];
    if(!mountain&&!mavericks&&!miniBuddy) {
        NSString *registration=@"Library/Receipts/.SetupRegComplete";
        [target file:registration data:NSData.data mode:0400];
        target.metadata[registration]=@[@(tiger?uid:0),@80,@(S_IFREG|0400)];
    }
    NSString *loginPolicy=@{@"tiger":@"TigerNativeSetupDone",@"leopard":@"LeopardNativeSetupDone",
        @"snowleopard":@"SnowLeopardNativeSetupDone",@"lion":@"LionNativeFirstLoginPreferences",
        @"mountainlion":@"MountainLionNativeCloudPreferences",@"mavericks":@"MavericksNativeCloudPreferences",
        @"yosemite":@"YosemiteNativeMiniBuddyPreferences",@"elcapitan":@"ElCapitanNativeMiniBuddyPreferences",
        @"sierra":@"SierraNativeMiniBuddyPreferences",@"highsierra":@"HighSierraNativeMiniBuddyPreferences",
        @"mojave":@"MojaveNativeMiniBuddyPreferences",@"catalina":@"CatalinaNativeMiniBuddyPreferences",
        @"bigsur":@"BigSurNativeMiniBuddyPreferences"}[guest]?:(bigSur?@"AssistantNativeMiniBuddyPreferences":nil);
    NSMutableDictionary *account=[owner mutableCopy];
    [account addEntriesFromDictionary:@{@"Username":name,@"UID":@(uid),@"GeneratedUID":guid,
        @"FirstLoginPolicyPrepared":@YES,@"FirstLoginPolicyMechanism":loginPolicy,
        @"Phase":login?@"autologin-armed":@"complete"}];
    account[@"InitialUserPreferencesPrepared"]=@NO;
    if(smb) {
        if(tiger)[target file:@"private/etc/smb.conf" data:Read(At(target.root,@"private/etc/smb.conf.template")) mode:0644];
        [account addEntriesFromDictionary:@{@"FileSharingAccountPrepared":@YES,@"FileSharingCredentialVersion":@1,
            @"FileSharingDeferredUntilDesktop":@YES,@"HostStagedFileSharing":@YES}];
    }
    if(login) {
        NSData *encoded=VIMClassicAutoLoginPassword(password);
        [target file:@"private/etc/kcpassword" data:encoded mode:0600];
        NSString *path=@"Library/Preferences/com.apple.loginwindow.plist";
        NSMutableDictionary *prefs=[Plist(At(target.root,path)) mutableCopy]?:[NSMutableDictionary dictionary];
        prefs[@"autoLoginUser"]=name;[target plist:path value:prefs mode:0644];
        account[@"AutoLoginSHA256"]=Digest(encoded);
    }
    BOOL nativeSMB=VIMIsModernGuest(guest)&&smb;
    if((bigSur&&ssh)||nativeSMB) {
        // Modern NetFSServer enablement persists this same smbd override;
        // creating the share/account alone does not enable its listener.
        // This also leaves Big Sur and later's authenticated System intact.
        NSString *path=@"private/var/db/com.apple.xpc.launchd/disabled.plist";
        NSMutableDictionary *disabled=[Plist(At(target.root,path)) mutableCopy]?:[NSMutableDictionary dictionary];
        [target directory:path.stringByDeletingLastPathComponent mode:0700];
        if(bigSur&&ssh)disabled[@"com.openssh.sshd"]=@NO;
        if(nativeSMB)disabled[@"com.apple.smbd"]=@NO;
        [target plist:path value:disabled mode:0644];
    }
    if(ssh) {
        if(!bigSur) {
            NSString *path=@"System/Library/LaunchDaemons/ssh.plist";
            NSMutableDictionary *launch=[Plist(At(target.root,path)) mutableCopy];
            Require(launch!=nil,@"The original SSH service is missing.");
            launch[@"Disabled"]=@NO;[target plist:path value:launch mode:0644];
        }
        account[@"RemoteLoginConfigured"]=@YES;
    }
    [target plist:[SetupState(guest) stringByAppendingPathComponent:@"Account.plist"] value:account mode:0600];
}

static void StageEFIMediaProbe(VIMInstallTarget *target,NSString *build,NSString *state) {
    NSString *path=@"System/Library/Filesystems/msdos.fs/Contents/Info.plist";
    NSString *efi=@"C12A7328-F81F-11D2-BA4B-00A0C93EC93B";
    NSData *original=Read(At(target.root,path));
    NSMutableDictionary *info=[Plist(At(target.root,path)) mutableCopy],*types=[info[@"FSMediaTypes"] mutableCopy];
    if(types[efi])return;
    NSMutableDictionary *fat=[types[@"DOS_FAT_32"] mutableCopy],*properties=[fat[@"FSMediaProperties"] mutableCopy];
    Require([properties[@"Content Hint"] isEqual:@"DOS_FAT_32"]&&[properties[@"Leaf"] boolValue]&&[fat[@"FSProbeArguments"] isEqual:@"-p"],
            @"The native FAT32 filesystem probe is not recognized.");
    NSString *backup=[state stringByAppendingPathComponent:@"OriginalFilesystemMetadata"];
    [target directory:backup mode:0700];backup=[backup stringByAppendingPathComponent:build];
    [target directory:backup mode:0700];
    [target file:[backup stringByAppendingPathComponent:@"msdos-Info.plist"] data:original mode:0600];
    properties[@"Content Hint"]=efi;fat[@"FSMediaProperties"]=properties;fat[@"autodiskmount"]=@NO;
    types[efi]=fat;info[@"FSMediaTypes"]=types;[target plist:path value:info mode:0644];
}

static void StageSupport(VIMInstallTarget *target,NSURL *support,NSDictionary *owner,NSString *build,NSString *originalBuild) {
    NSString *guest=owner[@"Guest"],*state=SetupState(guest);
    BOOL tiger=[guest isEqual:@"tiger"];
    NSURL *receipt=At(support,@"BuildReceipt.plist");
    NSString *tools=[Support stringByAppendingPathComponent:@"GuestTools"],*launch=@"Library/LaunchDaemons";
    NSString *agent=@"com.mac.virtualintelmac.opengl-agent";
    NSMutableDictionary *ownership=[owner mutableCopy];
    [ownership addEntriesFromDictionary:@{@"Phase":@"native-files-installed",@"InstallOnly":@NO}];
    if(!tiger) {
        [target directory:tools mode:0755];
        Ditto(@[support.path,At(target.root,tools).path]);
        [target record:tools uid:0 gid:0 receipt:NO];
        NSString *prefix=[tools stringByAppendingString:@"/"];
        for(NSString *path in target.metadata.allKeys)if([path isEqual:tools]||[path hasPrefix:prefix]) {
            mode_t type=[target.metadata[path][2] unsignedShortValue]&S_IFMT;
            if(type==S_IFDIR)target.metadata[path]=@[@0,@0,@(S_IFDIR|0755)];
            else if(type==S_IFREG) {
                BOOL data=[path hasSuffix:@".plist"]||[path hasSuffix:@".txt"]||[path.pathComponents containsObject:@"Licenses"];
                target.metadata[path]=@[@0,@0,@(S_IFREG|(data?0644:0755))];
            }
        }
        NSString *agentFile=[agent stringByAppendingPathExtension:@"plist"],*display=@"com.virtualintelmac.opengl-display.plist";
        [target file:[launch stringByAppendingPathComponent:agentFile] data:Read(At(support,agentFile)) mode:0644];
        [target file:[@"Library/LaunchAgents" stringByAppendingPathComponent:display] data:Read(At(support,display)) mode:0644];
    } else {
        [target file:[tools stringByAppendingPathComponent:@"BuildReceipt.plist"] data:Read(receipt) mode:0644];
        id files=Plist(receipt)[@"Files"];
        Require([files respondsToSelector:@selector(count)]&&[files count]!=0,@"Tiger guest tools are missing.");
        for(NSString *name in files) {
            Require([name isEqual:name.lastPathComponent]&&![name isEqual:@".."],@"Invalid Tiger guest tools filename.");
            mode_t mode=([name hasSuffix:@".plist"]||[name hasSuffix:@".txt"])?0644:0755;
            [target file:[tools stringByAppendingPathComponent:name] data:Read(At(support,name)) mode:mode];
        }
        [target plist:[launch stringByAppendingPathComponent:[agent stringByAppendingString:@".plist"]]
            value:@{@"Label":agent,@"RunAtLoad":@YES,@"OnDemand":@NO,
                    @"ProgramArguments":@[[NSString stringWithFormat:@"/%@/opengl-guest-agent",tools],@"--device",@"/dev/cu.serial2"]} mode:0644];
        ownership[@"GuestAgentSHA256"]=Digest(Read(At(support,@"opengl-guest-agent")));
        ownership[@"GuestToolsBuildReceiptSHA256"]=Digest(Read(receipt));
    }
    [target directory:state mode:0700];
    [target plist:[state stringByAppendingPathComponent:@"Ownership.plist"] value:ownership mode:0600];
    NSData *native=Read(At(target.root,Engine)),*engine=Read(At(support,@"GLEngine"));
    NSDictionary *graphics=Plist(At(support,@"Graphics.plist"));NSString *nativeHash=Digest(native);
    if(tiger)Require([graphics[@"NativeEngineSHA256"] isEqual:nativeHash],@"The Tiger graphics payload does not match the original installer engine.");
    NSString *backup=[state stringByAppendingPathComponent:@"OriginalGraphics"];
    [target directory:backup mode:0700];
    for(NSString *version in [build isEqual:originalBuild]?@[build]:@[build,originalBuild]) {
        NSString *directory=[backup stringByAppendingPathComponent:version];
        [target directory:directory mode:0700];
        [target file:[directory stringByAppendingPathComponent:@"GLEngine"] data:native mode:0600];
    }
    NSString *resources=Engine.stringByDeletingLastPathComponent.stringByDeletingLastPathComponent;
    [target file:[resources stringByAppendingPathComponent:@"GLEngineStock.bundle/GLEngineStock"] data:native mode:0755];
    [target file:Engine data:engine mode:0755];
    NSString *backend=@"libVIMPassthroughOpenGL.dylib";
    NSData *gl=Read(At(support,backend));
    [target file:[Engine.stringByDeletingLastPathComponent stringByAppendingPathComponent:backend] data:gl mode:0755];
    NSString *plugin=@"VMsvga2GA.plugin/Contents",*extensions=@"System/Library/Extensions";
    NSString *ga=[plugin stringByAppendingPathComponent:@"MacOS/VMsvga2GA"],*gaInfo=[plugin stringByAppendingPathComponent:@"Info.plist"];
    NSData *gaBytes=Read(At(support,ga)),*infoBytes=Read(At(support,gaInfo));
    [target file:[extensions stringByAppendingPathComponent:ga] data:gaBytes mode:0755];
    [target file:[extensions stringByAppendingPathComponent:gaInfo] data:infoBytes mode:0644];
    NSMutableDictionary *evidence=[owner mutableCopy];
    [evidence addEntriesFromDictionary:@{@"ProductBuildVersion":build,@"OriginalSourceBuild":originalBuild,@"OriginalSHA256":nativeHash,
        @"PreviousEngineSHA256":nativeHash,@"EngineSHA256":Digest(engine),@"BackendSHA256":Digest(gl),
        @"GASHA256":Digest(gaBytes),@"GAInfoSHA256":Digest(infoBytes),@"Phase":@"installed",@"ChangedThisBoot":@NO,
        @"PrebootInstall":@YES,@"DesktopQualified":@NO,
        @"GraphicsSupportRevision":@(VIM_OPENGL_SUPPORT_REVISION)}];
    if(graphics[@"DispatchCount"])evidence[@"NativeDispatchCount"]=graphics[@"DispatchCount"];
    [target plist:[state stringByAppendingPathComponent:@"Graphics.plist"] value:evidence mode:0600];
    if(!tiger){
        StageEFIMediaProbe(target,build,state);
        if([guest isEqual:@"lion"]) {
            NSString *path=@"Library/Preferences/com.apple.windowserver.plist";
            NSMutableDictionary *prefs=[Plist(At(target.root,path)) mutableCopy]?:[NSMutableDictionary dictionary];
            if(!prefs[@"DisplayResolutionEnabled"]&&![prefs[@"DisplayResolutionDisabled"] boolValue]) {
                prefs[@"DisplayResolutionEnabled"]=@YES;[target plist:path value:prefs mode:0644];
            }
        }
        return;
    }
    NSURL *audio=At(support,@"Audio");
    NSString *installed=[Support stringByAppendingPathComponent:@"NativeAudio"];
    NSMutableDictionary *audioFiles=[NSMutableDictionary dictionary];
    for(NSString *file in @[@"VoodooHDA.kext/Contents/Info.plist",@"VoodooHDA.kext/Contents/MacOS/VoodooHDA"]) {
        NSData *bytes=Read(At(audio,file));audioFiles[file]=Digest(bytes);
        [target file:[installed stringByAppendingPathComponent:file] data:bytes mode:[file hasSuffix:@".plist"]?0644:0755];
    }
    NSData *audioReceipt=Read(At(audio,@"BuildReceipt.plist"));
    [target file:[installed stringByAppendingPathComponent:@"BuildReceipt.plist"] data:audioReceipt mode:0644];
    NSMutableDictionary *audioEvidence=[owner mutableCopy];
    [audioEvidence addEntriesFromDictionary:@{@"Files":audioFiles,@"BuildReceiptSHA256":Digest(audioReceipt),@"Phase":@"installed"}];
    [target plist:[state stringByAppendingPathComponent:@"Audio.plist"] value:audioEvidence mode:0600];
    NSString *sound=@"com.mac.virtualintelmac.tiger-audio";
    [target plist:[launch stringByAppendingPathComponent:[sound stringByAppendingString:@".plist"]]
        value:@{@"Label":sound,@"RunAtLoad":@YES,@"OnDemand":@YES,
                @"ProgramArguments":@[[NSString stringWithFormat:@"/%@/opengl-tiger-audio",tools],@"--load"],
                @"StandardErrorPath":@"/var/log/VirtualIntelMacTigerAudio.log"} mode:0644];
}

static NSArray<NSDictionary *> *Attach(NSArray<NSString *> *args) {
    NSArray *modern=VIMModernWritableImageArguments(args);
    if(modern&&VIMModernDiskImagesAvailable())
        return VIMNormalizeDiskImageEntities(ToolPlist(@"/usr/sbin/diskutil",modern)[@"system-entities"]);
    return ToolPlist(HDI,[@[@"attach",@"-plist",@"-nobrowse",@"-noautoopen"] arrayByAddingObjectsFromArray:args])[@"system-entities"];
}
static NSString *WholeDevice(NSArray<NSDictionary *> *entities) {
    NSString *fallback=nil;
    for(NSDictionary *entity in entities) {
        NSString *device=entity[@"dev-entry"];
        if(![device isKindOfClass:NSString.class]||
           [device rangeOfString:@"^/dev/disk[0-9]+$" options:NSRegularExpressionSearch].location==NSNotFound)continue;
        // DiskImages' plist can list the synthesized APFS device before the
        // underlying GPT. The physical disk is required for export/eject.
        if([entity[@"content-hint"] isEqual:@"GUID_partition_scheme"])return device;
        if(!fallback)fallback=device;
    }
    return fallback;
}
static BOOL MountAt(NSArray<NSDictionary *> *entities,NSURL *mount) {
    for(NSDictionary *entity in entities) {
        NSString *path=entity[@"mount-point"];
        if(path&&[[NSURL fileURLWithPath:path].URLByResolvingSymlinksInPath.path isEqual:mount.URLByResolvingSymlinksInPath.path])return YES;
    }
    return NO;
}

static void MountRecoveryImages(NSURL *image,NSURL *media,NSURL *base,BOOL *mediaAttached,BOOL *baseAttached) {
    if(*baseAttached){Run(HDI,@[@"detach",base.path],nil,NO);*baseAttached=NO;}
    if(*mediaAttached){Run(HDI,@[@"detach",media.path],nil,NO);*mediaAttached=NO;}
    Directory(media);Directory(base);
    NSArray *parts=Attach(@[@"-readonly",@"-noverify",@"-owners",@"off",@"-mountpoint",media.path,image.path]);
    *mediaAttached=YES;
    Require(MountAt(parts,media),@"The updated Recovery image did not mount.");
    parts=Attach(@[@"-readonly",@"-noverify",@"-owners",@"off",@"-mountpoint",base.path,At(media,@"BaseSystem.dmg").path]);
    *baseAttached=YES;
    Require(MountAt(parts,base),@"The updated Recovery system did not mount.");
}

static void ValidateUpdatedRecovery(NSURL *image,NSURL *attempt,NSString *expectedVersion,NSString *expectedBuild) {
    Checkpoint(attempt);
    NSURL *scratch=[NSURL fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:
        [@"VIM-RecoveryCheck." stringByAppendingString:NSUUID.UUID.UUIDString]] isDirectory:YES];
    Directory(scratch);Require(!chmod(scratch.fileSystemRepresentation,0700),@"Could not prepare Recovery mountpoints.");
    NSURL *media=At(scratch,@"Media"),*base=At(scratch,@"Base");
    BOOL mediaAttached=NO,baseAttached=NO;NSException *issue=nil;
    @try {
        Run(HDI,@[@"verify",image.path],nil,NO);
        MountRecoveryImages(image,media,base,&mediaAttached,&baseAttached);
        NSDictionary *version=Plist(At(base,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]));
        Require([version[@"ProductVersion"] isEqual:expectedVersion]&&[version[@"ProductBuildVersion"] isEqual:expectedBuild],
            @"The optional Recovery system has a different OS build.");
        for(NSString *path in @[@"BaseSystem.dmg",@"BaseSystem.chunklist"])
            Require([NSFileManager.defaultManager isReadableFileAtPath:At(media,path).path],@"The updated Recovery files are incomplete.");
    } @catch(NSException *failure) {issue=failure;}
    @finally {
        // Cleanup errors stay fatal; never pretend that a busy attached
        // image is gone or remove anything under its mountpoint.
        if(baseAttached)Run(HDI,@[@"detach",base.path],nil,NO);
        if(mediaAttached)Run(HDI,@[@"detach",media.path],nil,NO);
        rmdir(base.fileSystemRepresentation);rmdir(media.fileSystemRepresentation);rmdir(scratch.fileSystemRepresentation);
    }
    Checkpoint(attempt);
    if(issue)OptionalUpdateUnavailable(issue.reason);
}

static NSURL *ExpandAppleESD(NSURL *attempt,NSURL *source,NSString *package) {
    NSURL *expanded=At(attempt,@"AppleInstaller");
    Run(@"/usr/sbin/pkgutil",@[@"--expand-full",At(source,package).path,expanded.path],nil,NO);
    NSURL *esd=At(attempt,@"InstallESD.dmg");
    NSError *error=nil;
    BOOL moved=[NSFileManager.defaultManager moveItemAtURL:At(At(expanded,package),@"InstallESD.dmg")
        toURL:esd error:&error];
    Require(moved,error.localizedDescription);
    Remove(expanded);return esd;
}

static void StageRecovery(NSURL *image,NSURL *mount,NSURL *media,NSURL *base,NSString *baseCache,BOOL *attached) {
    Run(HDI,@[@"create",@"-sectors",[@(VIMHFSRecoverySectors) stringValue],@"-type",@"SPARSEBUNDLE",
        @"-layout",@"NONE",@"-fs",@"HFS+",@"-volname",@"Recovery HD",image.path],nil,NO);
    NSArray *mounted=Attach(@[@"-owners",@"off",@"-mountpoint",mount.path,image.path]);
    *attached=YES;
    NSString *device=WholeDevice(mounted);
    uint32_t bootFolder=0,bootFile=0;NSDictionary *permissions=nil;
    @try {
        Require(device&&MountAt(mounted,mount),@"The new Recovery HD did not mount.");
        VIMInstallTarget *target=[VIMInstallTarget new];target.root=mount;target.metadata=[NSMutableDictionary dictionary];
        NSString *folder=@"com.apple.recovery.boot";
        NSString *cacheName=baseCache.lastPathComponent;
        [target directory:folder mode:0755];
        for(NSString *name in @[@"BaseSystem.dmg",@"BaseSystem.chunklist",@"boot.efi",cacheName,
                                @"PlatformSupport.plist",@"SystemVersion.plist"]) {
            NSString *path=[folder stringByAppendingPathComponent:name];
            NSURL *source=At(media,[name hasSuffix:@".plist"]?[CoreServices stringByAppendingPathComponent:name]:name);
            if(base) {
                if([name isEqual:@"boot.efi"])source=At(base,BootEFI);
                else if([name isEqual:cacheName])source=At(base,baseCache);
                else if([name hasSuffix:@".plist"])source=At(base,[CoreServices stringByAppendingPathComponent:name]);
            }
            // These are regular boot/data files, not resource-fork bundles.
            // Inheriting com.apple.rootless from boot.efi makes ditto fail on
            // SIP-enabled hosts. Copy bytes only; the detached ledger below
            // supplies Recovery's native ownership and modes.
            Require(!copyfile(source.fileSystemRepresentation,At(mount,path).fileSystemRepresentation,
                              NULL,COPYFILE_DATA),[NSString stringWithFormat:
                @"Could not copy the original Recovery boot file %@: %s.",name,strerror(errno)]);
            target.metadata[path]=@[@0,@0,@(S_IFREG|([name isEqual:@"SystemVersion.plist"]?0444:0644))];
        }
        NSString *loader=[folder stringByAppendingPathComponent:@"boot.efi"];
        [target plist:[folder stringByAppendingPathComponent:@"com.apple.Boot.plist"]
            value:@{@"Kernel Flags":@"rp=file:///com.apple.recovery.boot/BaseSystem.dmg",
                    @"Kernel Cache":[@"\\com.apple.recovery.boot\\" stringByAppendingString:cacheName]} mode:0644];
        [target file:@".metadata_never_index" data:NSData.data mode:0644];
        [target file:@".fseventsd/no_log" data:NSData.data mode:0644];
        permissions=[target catalogPermissions];
        struct stat directory,file;
        Require(!stat(At(mount,folder).fileSystemRepresentation,&directory)&&
            !stat(At(mount,loader).fileSystemRepresentation,&file),@"The recovery boot files are missing.");
        bootFolder=(uint32_t)directory.st_ino;bootFile=(uint32_t)file.st_ino;
    } @finally {
        Run(HDI,@[@"detach",device?:image.path],nil,NO);*attached=NO;
    }
    NSError *error=nil;
    BOOL finalized=VIMFinalizeHFSCatalog(image,0,(uint64_t)VIMHFSRecoverySectors*512,permissions,bootFolder,bootFile,&error);
    Require(finalized,error.localizedDescription);
}
static uint32_t CacheBE32(const uint8_t *p) {
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static BOOL ValidCompressedKernelCache(NSURL *cache) {
    int fd=open(cache.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return NO;
    struct stat info;uint8_t bytes[28];
    BOOL valid=!fstat(fd,&info)&&S_ISREG(info.st_mode)&&pread(fd,bytes,sizeof(bytes),0)==sizeof(bytes);
    if(valid&&CacheBE32(bytes)==0xcafebabe) {
        // Apple prefixes the compressed payload with a big-endian fat
        // Mach-O table, even when kextcache was given one architecture.
        uint32_t offset=CacheBE32(bytes+16),size=CacheBE32(bytes+20);
        valid=CacheBE32(bytes+4)==1&&CacheBE32(bytes+8)==0x01000007&&offset>=sizeof(bytes)&&size>=8&&
            (uint64_t)offset+size<=(uint64_t)info.st_size&&pread(fd,bytes,8,offset)==8;
    }
    close(fd);
    return valid&&(!memcmp(bytes,"complzss",8)||!memcmp(bytes,"complzvn",8));
}
static void StageKextHelperCache(VIMInstallTarget *target,NSString *guest) {
    if([@[@"tiger",@"leopard"] containsObject:guest]||VIMIsAssistantGuest(guest))return;
    // kextd checks this property cache when taking the reboot lock, even
    // when no kext declares OSBundleHelper. A missing empty cache queues a
    // rescan after shutdown has disabled kext loads. Darwin13 can deadlock
    // in failed-load cleanup while holding IOCatalogue's nonrecursive lock.
    // Prepare the same ordinary metadata Apple creates with -system-caches;
    // leave prelinking, driver selection and native shutdown checks intact.
    BOOL localExtensions=![@[@"snowleopard",@"lion",@"mountainlion"] containsObject:guest];
    NSError *error=nil;NSData *data=VIMKextHelperCache(target.root,@"x86_64",localExtensions,&error);
    Require(data!=nil,error.localizedDescription);
    NSString *path=@"System/Library/Caches/com.apple.kext.caches/Startup/KextPropertyValues_OSBundleHelper_x86_64.plist.gz";
    [target file:path data:data mode:0644];
    struct stat system,local;
    Require(!stat(At(target.root,@"System/Library/Extensions").fileSystemRepresentation,&system),@"The installed kernel or extensions are missing.");
    time_t stamp=system.st_mtimespec.tv_sec;
    // Mavericks adds /Library/Extensions to the system repository set.
    if(localExtensions&&
       !stat(At(target.root,@"Library/Extensions").fileSystemRepresentation,&local))stamp=MAX(stamp,local.st_mtimespec.tv_sec);
    struct timespec times[2]={{.tv_nsec=UTIME_OMIT},{.tv_sec=stamp+1}};
    Require(!utimensat(AT_FDCWD,At(target.root,path).fileSystemRepresentation,times,0),@"Could not record the startup cache timestamp.");
}

static void StagePrelinkedKernel(VIMInstallTarget *target,NSURL *tool,NSURL *attempt,
                                 NSString *kernel,NSArray<NSString *> *destinations,BOOL localExtensions) {
    NSURL *cache=At(attempt,@"PrelinkedKernel");
    NSError *error=nil;
    NSString *root=VIMOfflineKextcacheVolumePath(target.root,&error);
    Require(root!=nil,error.localizedDescription);
    // Use the verified installer's period tool and the newly extracted
    // guest's root. Never run -update-volume against the host or load kexts.
    // OSKextCreate resolves /var to /private/var, while its prelink writer
    // strips volumeRoot by string prefix. All input paths must use that same
    // physical root or the cache records nonexistent host paths in the guest.
    NSMutableArray *args=[@[@"-x86_64",tool.path,
        @"-prelinked-kernel",cache.path,@"-kernel",[root stringByAppendingPathComponent:kernel],
        @"-volume-root",root,@"-arch",@"x86_64",@"-local-root",@"-no-authentication",
        [root stringByAppendingPathComponent:@"System/Library/Extensions"]] mutableCopy];
    if(localExtensions)[args addObject:[root stringByAppendingPathComponent:@"Library/Extensions"]];
    Run(@"/usr/bin/arch",args,nil,NO);
    Require(ValidCompressedKernelCache(cache),@"The installer did not produce a bootable startup cache.");
    for(NSString *path in destinations) {
        [target directory:path.stringByDeletingLastPathComponent mode:0755];
        Ditto(@[cache.path,At(target.root,path).path]);
        target.metadata[path]=@[@0,@0,@(S_IFREG|0644)];
    }
    Remove(cache);
}
static void StageProvidedStartupCache(VIMInstallTarget *target,BOOL legacyCopy) {
    // Essentials already includes the matching kernel and startup cache.
    // Our guest kexts are supplied by EFI, not built into this cache.
    Require(ValidCompressedKernelCache(At(target.root,PrelinkedCache)),@"The original startup cache is missing.");
    [target directory:StartupCache.stringByDeletingLastPathComponent mode:0755];
    if(legacyCopy) {
        // El Capitan's native installer uses a second regular file here.
        Ditto(@[At(target.root,PrelinkedCache).path,At(target.root,StartupCache).path]);
        target.metadata[StartupCache]=@[@0,@0,@(S_IFREG|0644)];
        return;
    }
    // Sierra's compatibility path is a symlink into the installed root.
    NSString *path=[@"/" stringByAppendingString:PrelinkedCache];
    Require(!symlink(path.fileSystemRepresentation,At(target.root,StartupCache).fileSystemRepresentation),
        @"Could not prepare the native startup cache link.");
    target.metadata[StartupCache]=@[@0,@0,@(S_IFLNK|0755)];
}

static NSString *const SystemKernel=@"System/Library/Kernels/kernel";
static NSString *const APFSResources=@"System/Library/Filesystems/apfs.fs/Contents/Resources";
static NSString *const APFSEFI=@"usr/standalone/i386/apfs.efi";
static NSString *const BootPlist=@"Library/Preferences/SystemConfiguration/com.apple.Boot.plist";
static NSString *const Diskutil=@"/usr/sbin/diskutil";

static void StampStartupCaches(NSURL *root,NSArray<NSString *> *caches) {
    // BootRoot checks equality with the newest extension repository + 1
    // second (or the kernel + 1 when newer), not merely a recent mtime.
    struct stat system,local,kernel;
    Require(!stat(At(root,@"System/Library/Extensions").fileSystemRepresentation,&system)&&
        !stat(At(root,@"Library/Extensions").fileSystemRepresentation,&local)&&
        !stat(At(root,SystemKernel).fileSystemRepresentation,&kernel),@"The installed kernel or extensions are missing.");
    time_t stamp=1+MAX(system.st_mtimespec.tv_sec,local.st_mtimespec.tv_sec);
    if(kernel.st_mtimespec.tv_sec>stamp)stamp=kernel.st_mtimespec.tv_sec+1;
    struct timespec timestamps[2]={{.tv_nsec=UTIME_OMIT},{.tv_sec=stamp}};
    for(NSString *cache in caches)Require(!utimensat(AT_FDCWD,At(root,cache).fileSystemRepresentation,timestamps,0),
        @"Could not record the startup cache timestamp.");
}

static NSURL *PreparePeriodTool(NSURL *original,NSURL *privateScratch,NSURL *runtime) {
    // A verified Apple tool may have a SHA-1-only signature that current
    // taskgated rejects. Sign only a private copy for host execution; never
    // touch the original installer, guest files or the host security policy.
    NSURL *copy=At(privateScratch,original.lastPathComponent);
    Run(@"/usr/bin/install",@[@"-m",@"700",original.path,copy.path],nil,NO);
    if([original.lastPathComponent isEqual:@"kextcache"]) {
        BOOL redirected=NO;NSError *error=nil;
        Require(VIMPrepareOfflineKextcache(copy,&redirected,&error),error.localizedDescription);
        if(redirected)Run(@"/usr/bin/install",@[@"-m",@"600",
            At(runtime,@"HostInstall/offline-kext-notifications.dylib").path,At(privateScratch,@"dz.dylib").path],nil,NO);
    }
    Run(@"/usr/bin/codesign",@[@"--force",@"--sign",@"-",copy.path],nil,NO);
    return copy;
}

static void CopyBootAsset(NSURL *source,NSURL *dest,NSString *path) {
    NSURL *to=At(dest,path);
    Directory(to.URLByDeletingLastPathComponent);
    Ditto(@[At(source,path).path,to.path]);
}

static void StageAPFSStartup(VIMInstallTarget *target,NSURL *base,NSURL *attempt,
                             NSURL *scratch,NSURL *preboot,NSString *uuid,BOOL split,NSURL *runtime,BOOL updated) {
    NSURL *linker=PreparePeriodTool(At(updated?target.root:base,@"usr/sbin/kextcache"),scratch,runtime);
    StagePrelinkedKernel(target,linker,attempt,SystemKernel,@[PrelinkedCache],YES);
    Remove(linker);
    if(!split)StageProvidedStartupCache(target,NO);
    StampStartupCaches(target.root,@[PrelinkedCache]);

    for(NSString *name in @[@"boot.efi",@"boot.efi.j137ap.im4m",@"bootbase.efi.j137ap.im4m",
                              @"BridgeVersion.bin",@"PlatformSupport.plist",@"SystemVersion.plist"]) {
        CopyBootAsset([name isEqual:@"bootbase.efi.j137ap.im4m"]?base:target.root,preboot,
            [CoreServices stringByAppendingPathComponent:name]);
    }
    if(split) {
        for(NSURL *root in @[target.root,base]) {
            NSString *prefix=root==base?@"bootbase.efi.":@"boot.efi.";
            for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:At(root,CoreServices).path error:nil])
                if([name hasPrefix:prefix]&&[name hasSuffix:@".im4m"]&&![name hasSuffix:@".j137ap.im4m"])
                    CopyBootAsset(root,preboot,[CoreServices stringByAppendingPathComponent:name]);
        }
        CopyBootAsset(target.root,preboot,[CoreServices stringByAppendingPathComponent:@"BridgeVersion.plist"]);
    }
    CopyBootAsset(target.root,preboot,BootPlist);
    Write(At(preboot,[CoreServices stringByAppendingPathComponent:@".root_uuid"]),[uuid dataUsingEncoding:NSASCIIStringEncoding]);
    Write(At(preboot,[CoreServices stringByAppendingPathComponent:@".contentDetails"]),[@"Macintosh HD" dataUsingEncoding:NSUTF8StringEncoding]);
    NSString *efi=@"usr/standalone/i386/EfiLoginUI";
    for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:At(target.root,efi).path error:nil]) {
        if(![name hasSuffix:@".efires"])continue;
        NSString *source=[efi stringByAppendingPathComponent:name];
        CopyBootAsset(target.root,preboot,source);
        if(![@[@"recoveryUI.efires",@"recovery_user.efires"] containsObject:name]) {
            NSURL *cache=At(preboot,@"System/Library/Caches/com.apple.corestorage/EFILoginLocalizations");
            Directory(cache);Ditto(@[At(target.root,source).path,At(cache,name).path]);
        }
    }
}

static void CheckDyldCache(NSURL *folder,NSString *arch,const char tag[16]) {
    NSURL *cache=At(folder,[@"dyld_shared_cache_" stringByAppendingString:arch]);
    NSFileHandle *handle=[NSFileHandle fileHandleForReadingFromURL:cache error:nil];
    NSData *magic=[handle readDataOfLength:16];[handle closeFile];
    Require(magic.length==16&&!memcmp(magic.bytes,tag,16),
        [NSString stringWithFormat:@"The shared-cache tool did not produce a valid %@ cache.",arch]);
}

static void StageNativeHFSSharedCaches(VIMInstallTarget *target,NSURL *runtime,NSString *guest) {
    NSString *relative=@"private/var/db/dyld";
    [target directory:relative mode:0755];
    NSURL *folder=At(target.root,relative);
    // Build against the installed guest before recording its HFS+ catalog
    // IDs. The host tool's own architecture does not select cache slices.
    NSString *tool=[guest isEqual:@"snowleopard"]?@"snow-leopard-cache":[guest stringByAppendingString:@"-cache"];
    Run(At(runtime,[@"HostInstall" stringByAppendingPathComponent:tool]).path,
        @[target.root.path,folder.path],nil,NO);
    if(![guest isEqual:@"catalina"])CheckDyldCache(folder,@"i386","dyld_v1    i386");
    CheckDyldCache(folder,@"x86_64","dyld_v1  x86_64");
    if([guest isEqual:@"leopard"]||([guest isEqual:@"snowleopard"]&&
       [NSFileManager.defaultManager isExecutableFileAtPath:At(target.root,@"usr/libexec/oah/translate").path]))
        CheckDyldCache(folder,@"rosetta","dyld_v1     ppc");
    [target record:relative uid:0 gid:0 receipt:NO];
}

static NSDictionary *APFSContainer(NSString *store,NSURL *attempt) {
    // The physical partition can be attached before Disk Arbitration has
    // registered its synthesized container. Wait for the actual store,
    // never select a different disk or assume attach's return implies ready.
    NSTimeInterval delay=.125,deadline=NSProcessInfo.processInfo.systemUptime+12;
    do {
        Checkpoint(attempt);
        for(NSDictionary *item in ToolPlist(Diskutil,@[@"apfs",@"list",@"-plist"])[@"Containers"]) {
            NSArray *stores=item[@"PhysicalStores"];
            if(stores.count==1&&[stores[0][@"DeviceIdentifier"] isEqual:store])return item;
        }
        NSTimeInterval remaining=deadline-NSProcessInfo.processInfo.systemUptime;
        if(remaining<=0)break;
        [NSThread sleepForTimeInterval:MIN(delay,remaining)];
        delay=MIN(delay*2,2);
    } while(YES);
    Stop([NSString stringWithFormat:@"The host did not register an APFS container for %@.",store]);return nil;
}

static NSString *DiskIdentifier(id value) {
    Require([value isKindOfClass:NSString.class]&&
        [value rangeOfString:@"^disk[0-9]+(s[0-9]+)?$" options:NSRegularExpressionSearch].location!=NSNotFound,
        @"Unexpected identifier for the new APFS disk.");
    return value;
}

static void WithAPFSVolume(NSDictionary *volume,NSURL *root,void (^write)(void)) {
    NSString *disk=DiskIdentifier(volume[@"DeviceIdentifier"]);
    // These are our own attached image volumes. The native filesystem helper
    // supports their user-owned mountpoints without a global StorageKit sync
    // or an administrator prompt. Guest ownership is applied by finalization.
    Run(@"/sbin/mount_apfs",@[@"-o",@"nobrowse,noowners",[@"/dev/" stringByAppendingString:disk],root.path],nil,NO);
    @try {
        NSDictionary *info=ToolPlist(Diskutil,@[@"info",@"-plist",root.path]);
        Require([info[@"DeviceIdentifier"] isEqual:disk]&&[info[@"VolumeUUID"] isEqual:volume[@"APFSVolumeUUID"]],
            @"The mounted APFS volume is not part of the new disk.");
        write();
    } @finally {Run(@"/sbin/umount",@[root.path],nil,NO);}
}

static void ConvertToPeriodAPFS(NSURL *image,NSURL *base,NSURL *scratch,BOOL *attached) {
    NSURL *driver=At(base,APFSEFI),*converter=PreparePeriodTool(At(At(base,APFSResources),@"hfs_convert"),scratch,nil);
    NSData *efi=Read(driver);
    NSArray *parts=Attach(@[@"-nomount",image.path]);*attached=YES;
    NSString *whole=WholeDevice(parts);
    @try {
        Require(whole!=nil,@"Could not open the new APFS staging disk.");
        NSString *part=[whole stringByAppendingString:@"s2"];
        BOOL found=NO;
        for(NSDictionary *entry in parts)if([entry[@"dev-entry"] isEqual:part]&&[entry[@"content-hint"] isEqual:@"Apple_HFS"])found=YES;
        Require(found,@"The new HFS+ staging partition could not be identified.");
        Run(@"/usr/bin/arch",@[@"-x86_64",converter.path,[@"--efi=" stringByAppendingString:driver.path],part.lastPathComponent],nil,NO);
    } @finally {Run(HDI,@[@"detach",whole?:image.path],nil,NO);*attached=NO;}
    Remove(converter);
    NSError *error=nil;BOOL published=VIMPublishConvertedAPFSPartition(image,efi,&error);
    Require(published,error.localizedDescription);
}

static void CopyAPFSRecoveryAsset(NSURL *source,NSURL *destination) {
    // Recovery contains ordinary boot/data files, not bundles with resource
    // forks. As for HFS Recovery, don't transplant the mounted installer's
    // com.apple.rootless or restricted flags onto a SIP-enabled host volume.
    // Preserve the bytes and native permissions, not those host protections.
    struct stat info;
    Require(!stat(source.fileSystemRepresentation,&info)&&
            !copyfile(source.fileSystemRepresentation,destination.fileSystemRepresentation,NULL,COPYFILE_DATA)&&
            !chmod(destination.fileSystemRepresentation,info.st_mode&07777),
            [NSString stringWithFormat:@"Could not copy the original Recovery boot file %@: %s.",
                source.lastPathComponent,strerror(errno)]);
}

static void StageAPFSBootAssets(NSDictionary *named,NSURL *base,NSURL *recoveryMedia,NSURL *root,
                               NSURL *preboot,NSString *uuid,BOOL split) {
    WithAPFSVolume(named[@"Preboot"],root,^{
        NSURL *tree=At(root,uuid);Ditto(@[preboot.path,tree.path]);
        // Never set the host startup disk.
        Run(@"/usr/sbin/bless",@[@"--folder",At(tree,CoreServices).path,@"--file",At(tree,BootEFI).path],nil,NO);
    });
    WithAPFSVolume(named[@"Recovery"],root,^{
        NSURL *tree=At(root,uuid);Directory(tree);
        for(NSString *name in @[@"boot.efi",@"boot.efi.j137ap.im4m",@"PlatformSupport.plist",@"SystemVersion.plist"])
            CopyAPFSRecoveryAsset(At(At(base,CoreServices),name),At(tree,name));
        NSURL *prelinks=At(base,PrelinkedCache.stringByDeletingLastPathComponent);
        for(NSString *name in @[@"immutablekernel",@"immutablekernel.j137ap.im4m"])
            CopyAPFSRecoveryAsset(At(prelinks,name),At(tree,name));
        if(split) {
            NSArray<NSURL *> *sources=@[At(base,CoreServices),prelinks];
            NSArray<NSString *> *prefixes=@[@"boot.efi.",@"immutablekernel."];
            for(NSUInteger i=0;i<sources.count;i++)
                for(NSString *name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:sources[i].path error:nil])
                    if([name hasPrefix:prefixes[i]]&&[name hasSuffix:@".im4m"]&&![name hasSuffix:@".j137ap.im4m"])
                        CopyAPFSRecoveryAsset(At(sources[i],name),At(tree,name));
        }
        struct stat immutable,prelinked;
        if(!stat(At(prelinks,@"immutablekernel").fileSystemRepresentation,&immutable)&&
           !stat(At(prelinks,@"prelinkedkernel").fileSystemRepresentation,&prelinked)&&
           immutable.st_ino==prelinked.st_ino) {
            Require(!link(At(tree,@"immutablekernel").fileSystemRepresentation,At(tree,@"prelinkedkernel").fileSystemRepresentation),
                @"Could not link the native Recovery kernels.");
        } else CopyAPFSRecoveryAsset(At(prelinks,@"prelinkedkernel"),At(tree,@"prelinkedkernel"));
        for(NSString *name in @[@"BaseSystem.dmg",@"BaseSystem.chunklist"])
            CopyAPFSRecoveryAsset(At(recoveryMedia,name),At(tree,name));
        for(NSString *name in @[@"AppleDiagnostics.dmg",@"AppleDiagnostics.chunklist"])
            if([NSFileManager.defaultManager isReadableFileAtPath:At(recoveryMedia,name).path])
                CopyAPFSRecoveryAsset(At(recoveryMedia,name),At(tree,name));
        WritePlist(At(tree,@"com.apple.Boot.plist"),
            @{@"Kernel Cache":[NSString stringWithFormat:@"\\%@\\immutablekernel",uuid],
              @"Kernel Flags":[NSString stringWithFormat:@"root-dmg=file:///%@/BaseSystem.dmg",uuid]});
    });
}

static void StageAPFSVolumes(NSURL *image,NSURL *base,NSURL *recoveryMedia,NSURL *root,NSURL *scratch,
                             NSURL *preboot,NSString *uuid,NSURL *attempt,BOOL *attached) {
    ConvertToPeriodAPFS(image,base,scratch,attached);
    NSArray *parts=Attach(@[@"-nomount",image.path]);*attached=YES;
    NSString *whole=WholeDevice(parts);
    @try {
        Require(whole!=nil,@"Could not open the new APFS container.");
        NSString *store=[[whole stringByAppendingString:@"s2"] lastPathComponent];
        NSDictionary *container=APFSContainer(store,attempt);
        NSArray *original=container[@"Volumes"];
        Require(original.count==1&&[original[0][@"Name"] isEqual:@"Macintosh HD"]&&
            ![original[0][@"Roles"] count]&&[original[0][@"APFSVolumeUUID"] isEqual:uuid],
            @"APFS conversion did not preserve Macintosh HD’s identity.");
        NSString *disk=DiskIdentifier(container[@"ContainerReference"]);
        NSDictionary *roles=@{@"Preboot":@"B",@"Recovery":@"R",@"VM":@"V"};
        for(NSString *name in @[@"Preboot",@"Recovery",@"VM"])
            Run(Diskutil,@[@"apfs",@"addVolume",disk,@"APFS",name,@"-role",roles[name],@"-nomount"],nil,NO);
        NSArray *volumes=APFSContainer(store,attempt)[@"Volumes"];
        NSMutableDictionary *named=[NSMutableDictionary dictionary];
        for(NSDictionary *volume in volumes)named[volume[@"Name"]]=volume;
        Require(volumes.count==4&&[named[@"Macintosh HD"][@"APFSVolumeUUID"] isEqual:uuid],@"The new APFS volume layout is incomplete.");
        for(NSString *name in roles)Require([named[name][@"Roles"] isEqual:@[name]],@"A new APFS volume has the wrong role.");
        StageAPFSBootAssets(named,base,recoveryMedia,root,preboot,uuid,NO);
    } @finally {Run(HDI,@[@"detach",whole?:image.path],nil,NO);*attached=NO;}
}

static NSString *const APFSSplitSeed=@"Virtual Intel Mac APFS Seed";
static NSString *const APFSSystem=@"Macintosh HD";
static NSString *const APFSData=@"Macintosh HD - Data";

static void WithSplitAPFSDisk(NSURL *image,BOOL *attached,void (^body)(NSString *whole,NSArray<NSDictionary *> *parts)) {
    NSArray *parts=Attach(@[@"-nomount",@"-owners",@"off",image.path]);*attached=YES;
    NSString *whole=WholeDevice(parts);
    @try {
        Require(whole!=nil,@"Could not open the new System and Data disk.");
        body(whole,parts);
    } @finally {Run(HDI,@[@"detach",whole?:image.path],nil,NO);*attached=NO;}
}

static NSDictionary *SplitAPFSGroup(NSString *store,NSURL *attempt,BOOL includeUpdate) {
    NSDictionary *container=APFSContainer(store,attempt);
    NSMutableDictionary *roles=[@{APFSSystem:@"System",APFSData:@"Data",@"Preboot":@"Preboot",@"Recovery":@"Recovery",@"VM":@"VM"} mutableCopy];
    if(includeUpdate)roles[@"Update"]=@"Update";
    NSArray *volumes=container[@"Volumes"];
    NSMutableDictionary *named=[NSMutableDictionary dictionary];
    for(NSDictionary *volume in volumes)if(volume[@"Name"])named[volume[@"Name"]]=volume;
    Require(volumes.count==roles.count&&named.count==roles.count,@"The new APFS volume group is incomplete.");
    for(NSString *name in roles)Require([named[name][@"Roles"] isEqual:@[roles[name]]],@"A new APFS volume has the wrong role.");
    NSString *disk=DiskIdentifier(container[@"ContainerReference"]);
    NSArray *groups=ToolPlist(Diskutil,@[@"apfs",@"listVolumeGroups",@"-plist",disk])[@"Containers"];
    NSArray *pairs=groups.count==1&&[groups[0][@"ContainerReference"] isEqual:disk]?groups[0][@"VolumeGroups"]:nil;
    NSDictionary *pair=pairs.count==1?pairs[0]:nil;
    NSArray *members=pair[@"Volumes"];
    NSMutableDictionary *byRole=[NSMutableDictionary dictionary];
    for(NSDictionary *member in members)if(member[@"Role"]&&member[@"DiskUUID"])byRole[member[@"Role"]]=member[@"DiskUUID"];
    Require(members.count==2&&[byRole[@"System"] isEqual:named[APFSSystem][@"APFSVolumeUUID"]]&&
        [byRole[@"Data"] isEqual:named[APFSData][@"APFSVolumeUUID"]]&&
        [byRole[@"Data"] isEqual:pair[@"APFSVolumeGroupUUID"]],@"Could not confirm the new System and Data pairing.");
    return named;
}

static void CreateAssistantDisk(NSURL *image,NSURL *attempt,NSInteger size) {
    // One thin disk on every host filesystem. The temporary HFS partition is
    // replaced by APFS below; no full-capacity raw allocation or outer image.
    Run(HDI,@[@"create",@"-size",[NSString stringWithFormat:@"%ldg",(long)size],
        @"-type",@"SPARSEBUNDLE",@"-layout",@"GPTSPUD",@"-fs",@"Journaled HFS+",
        @"-volname",APFSSplitSeed,image.path],nil,NO);
    Checkpoint(attempt);
}

static NSDictionary *CreateSplitAPFSGroup(NSURL *image,NSURL *base,NSURL *scratch,NSURL *attempt,
                                           NSInteger size,BOOL bigSur,BOOL *attached) {
    if(bigSur) {
        struct stat info;
        Require(size>=8&&size<=2048&&lstat(image.fileSystemRepresentation,&info)&&errno==ENOENT,
            @"Choose a new scratch disk for the APFS group.");
        Progress(attempt,@"apfs-sparse-disk",@"Preparing the virtual disk…",AssistantDiskStart,AssistantDiskEnd,-1,2);
        CreateAssistantDisk(image,attempt,size);
        Progress(attempt,@"apfs",@"Creating the APFS volume group…",AssistantDiskEnd,AssistantPayloadStart,-1,20);
    } else ConvertToPeriodAPFS(image,base,scratch,attached);
    __block NSDictionary *named=nil;
    WithSplitAPFSDisk(image,attached,^(NSString *whole,NSArray<NSDictionary *> *parts){
        NSString *node=[whole stringByAppendingString:@"s2"],*store=DiskIdentifier(node.lastPathComponent);
        if(bigSur) {
            BOOL gpt=NO,hfs=NO;
            for(NSDictionary *entry in parts) {
                if([entry[@"dev-entry"] isEqual:whole]&&[entry[@"content-hint"] isEqual:@"GUID_partition_scheme"])gpt=YES;
                if([entry[@"dev-entry"] isEqual:node]&&[entry[@"content-hint"] isEqual:@"Apple_HFS"])hfs=YES;
            }
            Require(gpt&&hfs,@"The newly created disk does not contain the expected empty partition.");
            Run(Diskutil,@[@"apfs",@"createContainer",node],nil,NO);
            NSDictionary *empty=APFSContainer(store,attempt);
            Require([empty[@"Volumes"] isKindOfClass:NSArray.class]&&![empty[@"Volumes"] count],@"The new Big Sur APFS container is not empty.");
            Run(Diskutil,@[@"apfs",@"addVolume",DiskIdentifier(empty[@"ContainerReference"]),@"APFS",APFSSplitSeed,@"-nomount"],nil,NO);
        }
        NSDictionary *container=APFSContainer(store,attempt);
        NSArray *volumes=container[@"Volumes"];
        NSDictionary *seed=volumes.count==1?volumes[0]:nil;
        Require([seed[@"Name"] isEqual:APFSSplitSeed]&&![seed[@"Roles"] count]&&!seed[@"MountPoint"],
            @"The temporary APFS seed is not the one created for this installation.");
        NSString *disk=DiskIdentifier(container[@"ContainerReference"]);
        Run(Diskutil,@[@"apfs",@"addVolume",disk,@"APFS",APFSData,@"-role",@"D",@"-nomount"],nil,NO);
        NSString *data=nil;
        for(NSDictionary *volume in APFSContainer(store,attempt)[@"Volumes"])
            if([volume[@"Name"] isEqual:APFSData]&&[volume[@"Roles"] isEqual:@[@"Data"]])data=volume[@"DeviceIdentifier"];
        Run(Diskutil,@[@"apfs",@"addVolume",disk,@"APFS",APFSSystem,@"-role",@"S",@"-groupWith",DiskIdentifier(data),@"-nomount"],nil,NO);
        Run(Diskutil,@[@"apfs",@"deleteVolume",DiskIdentifier(seed[@"DeviceIdentifier"])],nil,NO);
        for(NSArray *role in @[@[@"Preboot",@"B"],@[@"Recovery",@"R"],@[@"VM",@"V"]])
            Run(Diskutil,@[@"apfs",@"addVolume",disk,@"APFS",role[0],@"-role",role[1],@"-nomount"],nil,NO);
        if(bigSur)Run(Diskutil,@[@"apfs",@"addVolume",disk,@"APFS",@"Update",@"-role",@"E",@"-nomount"],nil,NO);
        named=SplitAPFSGroup(store,attempt,bigSur);
    });
    if(bigSur) {
        NSError *error=nil;uint64_t offset,length;
        BOOL valid=VIMGetAPFSPartition(image,&offset,&length,&error);Require(valid,error.localizedDescription);
    }
    return named;
}

static void WithAPFSSystemAndData(NSDictionary *named,NSURL *system,NSURL *data,void (^write)(void)) {
    // On Ventura, mounting Data first causes Disk Arbitration to mount its
    // paired System at /Volumes before our explicit System mount. Own the
    // System mount first, as the Big Sur-and-later staging path already does.
    WithAPFSVolume(named[APFSSystem],system,^{
        WithAPFSVolume(named[APFSData],data,write);
    });
}

static NSDictionary *StageCatalinaGroup(NSURL *image,VIMInstallTarget *target,NSDictionary *layout,NSDictionary *initial,
    NSURL *base,NSURL *recoveryMedia,NSURL *runtime,NSURL *scratch,NSURL *preboot,NSURL *attempt,BOOL *attached) {
    NSURL *system=At(scratch,@"CatalinaSystem"),*data=At(scratch,@"CatalinaData"),*boot=At(scratch,@"CatalinaBoot");
    Directory(system);Directory(data);Directory(boot);
    __block NSDictionary *records=nil;
    WithSplitAPFSDisk(image,attached,^(NSString *whole,__unused NSArray<NSDictionary *> *parts){
        NSDictionary *named=SplitAPFSGroup([whole stringByAppendingString:@"s2"].lastPathComponent,attempt,NO);
        for(NSString *name in @[APFSSystem,APFSData])
            Require([named[name][@"APFSVolumeUUID"] isEqual:initial[name][@"APFSVolumeUUID"]],@"The staged APFS volume group changed.");
        WithAPFSSystemAndData(named,system,data,^{
            NSError *error=nil;
            records=VIMProjectCatalinaVolumes(target.root,system,data,layout,target.metadata,
                ^BOOL(NSURL *source,NSURL *dest,NSError **failure){
                    (void)failure;Checkpoint(attempt);Ditto(@[source.path,dest.path]);return YES;
                },&error);
            Require(records!=nil,error.localizedDescription);
            // Copying to the final APFS pair changes inode identities.
            // Build against those actual volumes, using the original
            // firmlink namespace without changing the host mount tree.
            NSString *relative=@"private/var/db/dyld";
            VIMInstallTarget *cache=[VIMInstallTarget new];cache.root=data;cache.metadata=[NSMutableDictionary dictionary];
            [cache directory:relative mode:0755];
            [cache record:relative uid:0 gid:0 receipt:NO];
            NSDictionary *old=[cache catalogPermissions];
            Progress(attempt,@"dyld",@"Building the shared libraries cache…",.94,.95,-1,85);
            Run(At(runtime,@"HostInstall/catalina-cache").path,
                @[system.path,At(data,relative).path,data.path],nil,NO);
            CheckDyldCache(At(data,relative),@"x86_64","dyld_v1  x86_64");
            // safeSave replaces cache inodes. Refresh just this private,
            // root-owned subtree before the detached permission pass.
            [cache.metadata removeAllObjects];[cache record:relative uid:0 gid:0 receipt:NO];
            NSMutableDictionary *permissions=[records[@"DataPermissions"] mutableCopy];
            [permissions removeObjectsForKeys:old.allKeys];
            [permissions addEntriesFromDictionary:[cache catalogPermissions]];
            NSMutableDictionary *updated=[records mutableCopy];updated[@"DataPermissions"]=permissions;records=updated;
        });
        Checkpoint(attempt);
        StageAPFSBootAssets(named,base,recoveryMedia,boot,preboot,named[APFSSystem][@"APFSVolumeUUID"],YES);
    });
    return records;
}

static void PublishPreparedGraphics(VIMInstallTarget *target,NSURL *payload,NSURL *manifest,NSString *build,NSString *guest) {
    NSString *list=[[NSString alloc] initWithData:Read(manifest) encoding:NSUTF8StringEncoding];
    NSDictionary *preparation=Plist(At(payload,@".VirtualIntelMacPreparation.plist"));
    if([preparation[@"PassthroughOpenGL"] isEqual:@YES]) {
        Require([guest isEqual:@"mojave"],@"Optional PassthroughOpenGL preparation requires Mojave.");
        list=[list stringByAppendingFormat:@"\n/%@\n",VIMPassthroughOpenGLTransportTarget];
    }
    NSString *stock=[@"private/var/db/VirtualIntelMac/HostAssembly/OriginalGraphics" stringByAppendingPathComponent:build];
    BOOL bigSur=VIMIsAssistantGuest(guest);
    if(!bigSur)[target directory:stock mode:0700];
    NSString *rootPrefix=[target.root.URLByResolvingSymlinksInPath.path stringByAppendingString:@"/"];
    [list enumerateLinesUsingBlock:^(NSString *line,__unused BOOL *stop) {
        @autoreleasepool {
            if(!line.length)return;
            Require([line hasPrefix:@"/"]&&![line.pathComponents containsObject:@".."],@"The bundled graphics path is invalid.");
            NSString *path=[line substringFromIndex:1];
            NSURL *source=At(payload,path),*destination=At(target.root,path);
            struct stat info;
            Require(path.length&&!lstat(source.fileSystemRepresentation,&info),
                [NSString stringWithFormat:@"The prepared graphics support is missing %@.",path]);
            BOOL alias=[guest isEqual:@"catalina"]&&S_ISLNK(info.st_mode)&&[path isEqual:@"System/Library/Extensions/AppleMetalGLRenderer.bundle"];
            Require(alias||S_ISREG(info.st_mode)||S_ISDIR(info.st_mode),@"The prepared graphics path has an unexpected type.");
            Require([destination.URLByDeletingLastPathComponent.URLByResolvingSymlinksInPath.path hasPrefix:rootPrefix],
                @"The graphics target is outside the staged system.");
            if(!lstat(destination.fileSystemRepresentation,&info)) {
                Require(!bigSur,@"An original Big Sur Data file conflicts with the graphics payload.");
                NSString *backup=[stock stringByAppendingPathComponent:path];
                [target directory:backup.stringByDeletingLastPathComponent mode:0700];
                Require(!rename(destination.fileSystemRepresentation,At(target.root,backup).fileSystemRepresentation),
                    @"Could not retain an original guest graphics file.");
                NSString *prefix=[path stringByAppendingString:@"/"];
                for(NSString *key in target.metadata.allKeys)if([key isEqual:path]||[key hasPrefix:prefix]) {
                    target.metadata[[backup stringByAppendingString:[key substringFromIndex:path.length]]]=target.metadata[key];
                    [target.metadata removeObjectForKey:key];
                }
            }
            [target directory:path.stringByDeletingLastPathComponent mode:0755];
            if(alias) {
                const char expected[]="AppleMetalOpenGLRenderer.bundle";char observed[sizeof(expected)];
                ssize_t count=readlink(source.fileSystemRepresentation,observed,sizeof(observed));
                Require(count==(ssize_t)sizeof(expected)-1&&!memcmp(observed,expected,sizeof(expected)-1)&&
                    !symlink(expected,destination.fileSystemRepresentation),@"Could not stage Catalina’s OpenGL renderer alias.");
            } else {
                NormalizeReplacement(source.path,NO);
                Ditto(@[source.path,destination.path]);
            }
            [target record:path uid:0 gid:0 receipt:NO];
        }
    }];
    if(!bigSur) {
        NSString *policy=@"Library/Preferences/com.apple.security.libraryvalidation.plist";
        NSMutableDictionary *prefs=[Plist(At(target.root,policy)) mutableCopy]?:[NSMutableDictionary dictionary];
        prefs[@"DisableLibraryValidation"]=@YES;[target plist:policy value:prefs mode:0644];
    }
}

static void StagePVGGraphics(VIMInstallTarget *target,NSURL *system,NSURL *runtime,NSURL *attempt,NSURL *mounts,
                             NSString *guest,NSString *build,BOOL *attached) {
    NSURL *support=At(runtime,[NSString stringWithFormat:@"GuestPayloads/%@.iso",guest]);
    NSURL *tools=At(runtime,@"HostInstall/GraphicsTools"),*prepare=At(tools,@"guest-prepare");
    NSString *policy=VIMIsAssistantGuest(guest)?@"bigsur":guest;
    NSURL *manifest=At(runtime,[NSString stringWithFormat:@"HostInstall/InstallTargets-%@.txt",policy]);
    NSURL *media=At(mounts,@"GraphicsSource"),*work=At(mounts,@"GraphicsWork");
    NSURL *image=At(attempt,@"Graphics.sparsebundle");
    Directory(media);Directory(work);
    Run(HDI,@[@"create",@"-size",@"2g",@"-type",@"SPARSEBUNDLE",@"-layout",@"NONE",@"-fs",
        @"Journaled HFS+",@"-volname",@"Graphics Staging",image.path],nil,NO);
    NSArray *original=Attach(@[@"-readonly",@"-noverify",@"-owners",@"off",@"-mountpoint",media.path,support.path]);
    NSString *source=WholeDevice(original);
    *attached=YES;
    @try {
        Require(MountAt(original,media),@"The bundled guest graphics support did not mount.");
        NSArray *staging=Attach(@[@"-owners",@"off",@"-mountpoint",work.path,image.path]);
        NSString *disk=WholeDevice(staging);
        @try {
            Require(MountAt(staging,work),@"The scratch graphics volume did not mount.");
            NSURL *archive=At(media,@"payload.tar.xz");
            if(![NSFileManager.defaultManager isReadableFileAtPath:archive.path])archive=At(media,@"payload.tar.gz");
            NSURL *expanded=At(work,@"Extracted"),*payload=At(expanded,@"payload");
            Run(prepare.path,@[@"--extract",archive.path,expanded.path],nil,NO);Checkpoint(attempt);
            NSArray *arguments=@[system.path,payload.path,tools.path];
            Run(prepare.path,arguments,nil,NO);
            NSDictionary *report=Plist(At(payload,@".VirtualIntelMacPreparation.plist"));
            Require([report[@"Prepared"] isEqual:@YES]&&[report[@"Guest"] isEqual:guest]&&
                [report[@"ProductBuildVersion"] isEqual:build]&&
                [report[@"OpenGLBootSelection"] isEqual:@YES]==[guest isEqual:@"mojave"],@"The graphics support does not match the staged system.");
            PublishPreparedGraphics(target,payload,manifest,build,guest);Checkpoint(attempt);
        } @finally {Run(HDI,@[@"detach",disk?:work.path],nil,NO);}
    } @finally {
        Run(HDI,@[@"detach",source?:media.path],nil,NO);
    }
    *attached=NO;
    Remove(image);rmdir(work.fileSystemRepresentation);rmdir(media.fileSystemRepresentation);
    // Use the bundled guest executable, not the universal host helper.
    // Running this in place avoids a new guest-agent upload at first login.
    [target file:[Support stringByAppendingPathComponent:@"host-assembly-verify"]
        data:Read(At(runtime,@"GuestTools/guest-install")) mode:0700];
}

// Populate a new Data volume while System is still staged. System must not
// be mounted or changed again after detached authenticated finalization.
NSDictionary *VIMStageBigSurData(NSURL *zip,NSURL *system,NSURL *data,NSURL *runtime,
    NSURL *attempt,NSURL *mounts,NSDictionary *owner,NSString *name,NSData *password,BOOL *graphicsAttached) {
    NSString *guest=owner[@"Guest"];
    NSString *const templatePath=@"System/Library/Templates/Data";
    NSURL *source=At(system,templatePath);
    struct stat original,destination,workspace;
    Require(!stat(source.fileSystemRepresentation,&original)&&!stat(data.fileSystemRepresentation,&destination)&&
        !stat(mounts.fileSystemRepresentation,&workspace)&&original.st_dev!=destination.st_dev&&
        destination.st_dev!=workspace.st_dev&&VIMIsAssistantGuest(guest)&&
        ![NSFileManager.defaultManager fileExistsAtPath:At(data,@"private/var/db/.AppleSetupDone").path],
        @"The destination is not a new Big Sur Data volume.");
    VIMInstallTarget *target=[VIMInstallTarget new];target.root=data;target.metadata=[NSMutableDictionary dictionary];
    NSString *prefix=[templatePath stringByAppendingString:@"/"];
    NSError *issue=nil;
    BOOL listed=VIMBigSurVisitFixups(zip,^BOOL(NSString *path,VIMBigSurFixup entry,__unused NSString *owner){
        if([path hasPrefix:prefix]) {
            NSString *relative=[path substringFromIndex:prefix.length];
            target.metadata[relative]=@[@(entry.uid),@(entry.gid),@(entry.mode)];
        }
        return YES;
    },NULL,NULL,&issue);
    Require(listed&&target.metadata.count,issue.localizedDescription?:@"The original Big Sur Data template is empty.");
    NSMutableDictionary<NSString *,NSNumber *> *opened=[NSMutableDictionary dictionary];
    @try {
        for(NSString *path in [target.metadata.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
            mode_t mode=[target.metadata[path][2] unsignedShortValue];
            mode_t access=S_ISDIR(mode)?S_IRUSR|S_IXUSR:S_ISREG(mode)?S_IRUSR:0;
            if(!access||(mode&access)==access)continue;
            NSURL *file=At(source,path);struct stat info;
            Require(!lstat(file.fileSystemRepresentation,&info)&&(info.st_mode&S_IFMT)==(mode&S_IFMT),
                @"An original Big Sur Data template item is unavailable.");
            mode_t current=info.st_mode&07777;
            if((current&access)==access)continue;
            Require(!chmod(file.fileSystemRepresentation,current|access),@"Could not open a Big Sur Data template item for copying.");
            opened[path]=@(current);
        }
        Ditto(@[source.path,data.path]);
    } @finally {
        BOOL restored=YES;
        for(NSString *path in [[opened.allKeys sortedArrayUsingSelector:@selector(compare:)] reverseObjectEnumerator])
            if(chmod(At(source,path).fileSystemRepresentation,[opened[path] unsignedShortValue]))restored=NO;
        Require(restored,@"Could not restore the original Big Sur System template permissions.");
    }
    Checkpoint(attempt);
    StageAccount(target,system,owner,name,password);
    StagePVGGraphics(target,system,runtime,attempt,mounts,guest,owner[@"Build"],graphicsAttached);
    // Compression repair may clear an unwritable protected file xattr.
    // Replay the original template labels after all private Data writes;
    // the detached pass restores their real names without host SIP changes.
    Require(VIMBigSurStageDataProtection(zip,data,&issue),issue.localizedDescription);
    Checkpoint(attempt);
    return [target catalogPermissions];
}

static NSDictionary *BootAssetPermissions(NSURL *root,NSString *uuid) {
    VIMInstallTarget *target=[VIMInstallTarget new];target.root=root;target.metadata=[NSMutableDictionary dictionary];
    [target record:uuid uid:0 gid:0 receipt:NO];
    return [target catalogPermissions];
}

static void StageAssistantCryptex(NSURL *zip,NSURL *preboot,NSURL *attempt,NSURL *mounts,
    NSUUID *dataUUID,NSString *build,NSURL *decoder,BOOL (^cancelled)(void)) {
    NSURL *scratch=At(attempt,@"CryptexInputs"),*mounted=At(mounts,@"Cryptex");
    Directory(scratch);Directory(mounted);
    @try {
        NSError *issue=nil;
        BOOL staged=VIMStageAssistantIntelCryptex(zip,preboot,scratch,dataUUID,build,decoder,cancelled,&issue);
        Require(staged,issue.localizedDescription);
        NSURL *image=At(At(preboot,dataUUID.UUIDString),@"cryptex1/current/os.dmg");
        NSArray *parts=Attach(@[@"-imagekey",@"diskimage-class=CRawDiskImage",@"-readonly",@"-noverify",
            @"-mountpoint",mounted.path,image.path]);
        @try {
            Require(MountAt(parts,mounted),@"The original macOS Intel Cryptex could not be opened.");
            BOOL valid=VIMVerifyAssistantIntelCryptexCaches(mounted,&issue);
            Require(valid,issue.localizedDescription);
        } @finally {Run(HDI,@[@"detach",WholeDevice(parts)?:image.path],nil,NO);}
    } @finally {rmdir(mounted.fileSystemRepresentation);rmdir(scratch.fileSystemRepresentation);}
}

// The original System payload is already mounted on the new group.
// Preserve its installed bytes, then finish all VFS work before handing the
// detached disk, the original seal and the prepared inode records onward.
NSDictionary *VIMStageBigSurMountedGroup(NSURL *zip,NSURL *system,NSDictionary *group,NSURL *runtime,
    NSURL *attempt,NSURL *mounts,NSDictionary *owner,NSString *name,NSData *password,NSUUID *snapshot,NSURL *sealInputs,
    VIMAssistantCacheLayout cacheLayout,NSURL *decoder,BOOL *graphicsAttached) {
    NSUUID *uuid=[[NSUUID alloc] initWithUUIDString:group[APFSData][@"APFSVolumeUUID"]];
    Require(uuid&&snapshot&&[uuid.UUIDString isEqual:owner[@"VolumeUUID"]],
        @"The new Big Sur Data volume or snapshot identity changed during preparation.");
    // Catch genuine mapping disagreements before the expensive compression and
    // boot-asset phases. Do not equate unused text-map entries with real stubs.
    NSError *firmlinkError=nil;
    NSString *firmlinkText=[NSString stringWithContentsOfURL:At(system,@"usr/share/firmlinks")
        encoding:NSUTF8StringEncoding error:&firmlinkError];
    Require(firmlinkText&&VIMBigSurFirmlinkPlan(zip,firmlinkText,&firmlinkError)!=nil,firmlinkError.localizedDescription);
    NSError *compressionError=nil;
    BOOL (^cancelled)(void)=^BOOL{return [NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path];};
    NSString *compressionTitle=@"Compressing system files…";
    Progress(attempt,@"compression",compressionTitle,BigSurCompressionStart,BigSurCompressionEnd,-1,1450);
    __block NSTimeInterval lastUpdate=0;
    BOOL compressed=VIMBigSurRestoreCompression(zip,system,cancelled,^(uint64_t sent,uint64_t total) {
        NSTimeInterval now=NSProcessInfo.processInfo.systemUptime;
        if(!total||(sent&&sent<total&&now-lastUpdate<1))return;
        lastUpdate=now;
        Progress(attempt,@"compression",compressionTitle,BigSurCompressionStart,BigSurCompressionEnd,
            MIN(.98,(double)sent/total),1450);
    },&compressionError);
    Require(compressed,compressionError.localizedDescription);
    Checkpoint(attempt);
    Progress(attempt,@"boot-assets",[owner[@"Enabled"] boolValue]?@"Preparing your account, graphics, and startup files…":
        @"Preparing graphics and startup files…",BigSurCompressionEnd,.90,-1,90);
    NSURL *data=At(mounts,@"Data"),*preboot=At(mounts,@"Preboot"),*recovery=At(mounts,@"Recovery");
    for(NSURL *mount in @[data,preboot,recovery])Directory(mount);
    __block NSDictionary *dataModes=nil,*prebootModes=nil,*recoveryModes=nil;
    __block NSData *seal=nil,*driver=nil;
    WithAPFSVolume(group[APFSData],data,^{
        dataModes=VIMStageBigSurData(zip,system,data,runtime,attempt,mounts,owner,name,password,graphicsAttached);
    });
    WithAPFSVolume(group[@"Preboot"],preboot,^{
        WithAPFSVolume(group[@"Recovery"],recovery,^{
            NSError *error=nil;
            seal=VIMBigSurStageBootAssets(zip,system,preboot,recovery,uuid,owner[@"Build"],cancelled,&error);
            Require(seal!=nil,error.localizedDescription);
            recoveryModes=BootAssetPermissions(recovery,uuid.UUIDString);
            if(sealInputs) {
                // Apple supplies the same Intel EFI image as a small standalone
                // boot asset. Do not materialize the whole Recovery disk just
                // to read these bytes. Retain Recovery extraction for media
                // revisions that omit or change the standalone representation.
                driver=VIMBigSurReadSmallArchiveMember(zip,@"AssetData/boot/Firmware/usr/standalone/i386/apfs.efi",nil);
                if(!VIMIsIntelAPFSDriver(driver)) {
                    NSURL *raw=At(sealInputs,@"Recovery.raw");
                    @try {
                        Run(At(runtime,@"bin/qemu-img").path,@[@"convert",@"-f",@"dmg",@"-O",@"raw",
                            At(At(recovery,uuid.UUIDString),@"BaseSystem.dmg").path,raw.path],nil,NO);
                        driver=VIMReadIntelAPFSJumpstart(raw,&error);
                        Require(driver!=nil,error.localizedDescription);
                    } @finally {unlink(raw.fileSystemRepresentation);}
                }
            }
        });
        if(cacheLayout==VIMAssistantCachesIntelCryptex) {
            Progress(attempt,@"cryptex",@"Preparing system components…",.89,.90,-1,75);
            StageAssistantCryptex(zip,preboot,attempt,mounts,uuid,owner[@"Build"],decoder,cancelled);
        }
        NSURL *tree=At(preboot,uuid.UUIDString),*core=At(tree,CoreServices);
        Write(At(core,@".root_uuid"),[snapshot.UUIDString dataUsingEncoding:NSASCIIStringEncoding]);
        // This only blesses files on the unpublished Preboot volume; it
        // does not change the host’s startup disk. The selector is not
        // published until the detached snapshot matches this exact UUID.
        Run(@"/usr/sbin/bless",@[@"--folder",core.path,@"--file",At(tree,BootEFI).path],nil,NO);
        prebootModes=BootAssetPermissions(preboot,uuid.UUIDString);
        if(sealInputs) {
            NSError *error=nil;
            BOOL extracted=VIMBigSurExtractSealInputs(At(tree,@"usr/standalone/OS.dmg.mtree"),sealInputs,NULL,NULL,&error);
            Require(extracted,error.localizedDescription);
        }
    });
    Checkpoint(attempt);
    NSError *error=nil;
    NSDictionary *firmlinks=VIMBigSurReserveFirmlinks(zip,system,&error);
    Require(firmlinks!=nil,error.localizedDescription);
    NSMutableDictionary *result=[@{@"DataPermissions":dataModes,@"PrebootPermissions":prebootModes,@"RecoveryPermissions":recoveryModes,
        @"Firmlinks":firmlinks,@"RootHash":seal,@"SnapshotUUID":snapshot.UUIDString} mutableCopy];
    if(driver)result[@"IntelEFI"]=driver;
    if(cacheLayout==VIMAssistantCachesIntelCryptex)result[@"IntelCryptexVerified"]=@YES;
    return result;
}

static NSData *Convert(NSURL *qemu,NSString *input,NSString *format,NSURL *disk,NSURL *attempt,double start) {
    // qemu-img emits its conversion percentage as carriage-return records.
    // Read these from a regular scratch file while the native tool runs.
    NSURL *logURL=At(attempt,@"Conversion.log");
    Write(logURL,NSData.data);
    NSFileHandle *log=[NSFileHandle fileHandleForWritingToURL:logURL error:nil];
    NSFileHandle *reader=[NSFileHandle fileHandleForReadingFromURL:logURL error:nil];
    Require(log&&reader,@"Could not track guest disk conversion.");
    NSTask *task=[NSTask new];task.executableURL=qemu;
    task.arguments=@[@"convert",@"-p",@"-f",format,@"-O",@"qcow2",input,disk.path];
    task.standardInput=NSFileHandle.fileHandleWithNullDevice;task.standardOutput=log;
    // qemu-img emits progress on stdout. Inherit Worker.log on stderr so an
    // orphaned converter keeps the installation lease until its last write.
    dispatch_semaphore_t exited=dispatch_semaphore_create(0);
    task.terminationHandler=^(__unused NSTask *finished){dispatch_semaphore_signal(exited);};
    NSError *error=nil;BOOL launched=[task launchAndReturnError:&error];Require(launched,error.localizedDescription);
    NSRegularExpression *pattern=[NSRegularExpression regularExpressionWithPattern:@"([0-9]+(?:\\.[0-9]+)?)/100%" options:0 error:nil];
    NSMutableString *pending=[NSMutableString string];
    BOOL cancelled=NO;
    while(dispatch_semaphore_wait(exited,dispatch_time(DISPATCH_TIME_NOW,500*NSEC_PER_MSEC))) {
        if(!cancelled&&[NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path]) {
            cancelled=YES;[task terminate];
        }
        @autoreleasepool {
            NSData *bytes=[reader readDataOfLength:32768];
            NSString *chunk=[[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding];
            if(!chunk.length)continue;
            [pending appendString:chunk];
            NSTextCheckingResult *last=[pattern matchesInString:pending options:0 range:NSMakeRange(0,pending.length)].lastObject;
            if(last) {
                double fraction=[pending substringWithRange:[last rangeAtIndex:1]].doubleValue/100;
                [pending deleteCharactersInRange:NSMakeRange(0,NSMaxRange(last.range))];
                // Never throw out of the wait loop while the tool owns the disk.
                if(!cancelled)@try {Progress(attempt,@"convert",@"Finalizing the virtual disk…",start,.98,fraction,0);}
                               @catch(NSException *failure) {(void)failure;}
            } else if(pending.length>4096)[pending deleteCharactersInRange:NSMakeRange(0,pending.length-128)];
        }
    }
    [reader closeFile];[log closeFile];
    Checkpoint(attempt);
    Require(task.terminationStatus==0,@"Could not convert the installed virtual disk.");
    return Run(qemu.path,@[@"info",@"--output=json",disk.path],nil,YES);
}

static NSData *FinalizeCatalinaDisk(NSURL *qemu,NSURL *image,NSURL *disk,NSURL *attempt,
    NSDictionary *group,NSDictionary *records,double start) {
    NSError *error=nil;uint64_t offset=0,length=0;
    BOOL located=VIMGetAPFSPartition(image,&offset,&length,&error);
    Require(located,error.localizedDescription);
    Progress(attempt,@"catalog",@"Finalizing Catalina’s System and Data volumes…",.95,start,-1,45);
    NSUUID *system=[[NSUUID alloc] initWithUUIDString:group[APFSSystem][@"APFSVolumeUUID"]];
    NSUUID *data=[[NSUUID alloc] initWithUUIDString:group[APFSData][@"APFSVolumeUUID"]];
    Require(system&&data&&records[@"FirmlinkInodes"]&&records[@"SystemPermissions"]&&records[@"DataPermissions"],
        @"The detached disk is missing its Catalina volume identities or metadata.");
    BOOL systemReady=VIMFinalizeAPFSVolume(image,offset,length,system,VIMAPFSSystemRole,records[@"FirmlinkInodes"],
        records[@"SystemPermissions"],&error);
    Require(systemReady,error.localizedDescription);Checkpoint(attempt);
    BOOL dataReady=VIMFinalizeAPFSVolume(image,offset,length,data,VIMAPFSDataRole,nil,records[@"DataPermissions"],&error);
    Require(dataReady,error.localizedDescription);Checkpoint(attempt);
    Progress(attempt,@"convert",@"Finalizing the virtual disk…",start,.98,0,0);
    NSData *info=Convert(qemu,image.path,@"apple-sparsebundle",disk,attempt,start);
    Remove(image);
    return info;
}

static NSURL *DownloadCache(NSURL *file,NSURL *downloads) {
    NSURL *cache=file.URLByDeletingLastPathComponent;
    return [file.lastPathComponent isEqual:@"Payload.pkg"]&&
        [cache.URLByDeletingLastPathComponent.URLByResolvingSymlinksInPath.path isEqual:downloads.URLByResolvingSymlinksInPath.path]&&
        [cache.lastPathComponent rangeOfString:@"^[0-9a-f]{64}$" options:NSRegularExpressionSearch].location!=NSNotFound?cache:nil;
}

static void PublishSystem(NSURL *stage,NSURL *attempt,NSURL *runtime,NSDictionary *request,
    NSString *identifier,NSString *volume,NSString *build,BOOL apfs,BOOL pvg,NSData *info,
    NSArray<NSString *> *downloadKeys,NSDictionary *assemblyDetails) {
    NSString *guest=request[@"Guest"];NSInteger size=[request[@"DiskGiB"] integerValue];
    NSDictionary *diskInfo=[NSJSONSerialization JSONObjectWithData:info options:0 error:nil];
    Checkpoint(attempt);
    Progress(attempt,@"verify",@"Verifying the new virtual disk…",.98,1,-1,0);
    Require([diskInfo[@"format"] isEqual:@"qcow2"]&&!diskInfo[@"backing-filename"]&&
            [diskInfo[@"virtual-size"] unsignedLongLongValue]==((uint64_t)size<<30),@"The virtual disk is not standalone.");
    Run(At(runtime,@"bin/qemu-img").path,@[@"check",@"-q",@"-f",@"qcow2",At(stage,@"Disks/System.qcow2").path],nil,NO);
    Write(At(stage,@"Firmware/NVRAM.fd"),Read(At(At(At(runtime,@"Firmware"),guest),@"VARS.fd")));
    NSMutableDictionary *settings=[@{@"FormatVersion":@1,@"Identifier":identifier,@"Guest":guest,
        @"InstallState":@"installed",@"Disk":@"Disks/System.qcow2",@"DiskGiB":@(size),
        @"Firmware":@"Firmware/CODE.fd",@"FirmwareSource":@"application",@"NVRAM":@"Firmware/NVRAM.fd",
        @"BootMedia":@"Firmware/EFI.img",@"BootMediaKind":@"efi-disk",
        @"CPUCount":[guest isEqual:@"snowleopard"]?@4:@2,@"MemoryMiB":[guest isEqual:@"snowleopard"]?@4096:@2048,
        @"SSHEnabled":@([request[@"RemoteLogin"] boolValue]),@"SSHPort":@23224,
        @"FileSharingEnabled":@([request[@"FileSharing"] boolValue]),@"FileSharingPort":@1445} mutableCopy];
    if(!pvg)[settings addEntriesFromDictionary:@{@"LegacyDisplayAdapter":@"vmware",@"PassthroughOpenGLEnabled":@YES}];
    else if([guest isEqual:@"mojave"])
        settings[@"PassthroughOpenGLEnabled"]=request[@"PassthroughOpenGLEnabled"]?:@YES;
    if([assemblyDetails[@"NetworkAdapter"] isEqual:@"rtl8139"]&&[guest isEqual:@"leopard"])
        settings[@"NetworkAdapter"]=@"rtl8139";
    WritePlist(At(stage,@"Machine.plist"),settings);
    WritePlist(At(stage,@"State/InstallationTarget.plist"),@{@"FormatVersion":@1,@"VolumeUUID":volume,@"Filesystem":apfs?@"APFS":@"HFS+"});
    NSMutableDictionary *assembly=[@{@"FormatVersion":@1,@"Guest":guest,@"ProductBuildVersion":build,
        @"VolumeUUID":volume,@"MachineIdentifier":identifier,@"Phase":@"awaiting-first-boot",@"Packages":Packages(guest)} mutableCopy];
    assembly[@"InstallerSource"]=[request[@"UserSelectedInstaller"] boolValue]?@"user-selected":@"catalog";
    if([guest isEqual:@"mojave"])assembly[@"OpenGLBootSelection"]=@YES;
    if(pvg) {
        assembly[@"PVGVerifierStaged"]=@YES;
        if(VIMHostInstallCreatesAccount(request))assembly[@"InitialUserPreferencesStaged"]=@YES;
    }
    if(downloadKeys.count==1)assembly[@"InstallerDownloadKey"]=downloadKeys.firstObject;
    else if(downloadKeys.count>1)assembly[@"InstallerDownloadKeys"]=downloadKeys;
    if(assemblyDetails)[assembly addEntriesFromDictionary:assemblyDetails];
    WritePlist(At(stage,@"State/HostAssembly.plist"),assembly);
    Checkpoint(attempt);
    NSError *error=nil;
    BOOL moved=[NSFileManager.defaultManager moveItemAtURL:stage toURL:At(attempt,@"Prepared.vintelmac") error:&error];
    Require(moved,error.localizedDescription);
}

typedef struct { __unsafe_unretained NSURL *attempt; __unsafe_unretained NSString *title; uint64_t total,next; } BigSurPayloadProgress;
static bool BigSurPayloadBytes(uint64_t consumed,__unused uint64_t decoded,void *opaque) {
    BigSurPayloadProgress *progress=opaque;
    if(consumed<progress->next&&consumed!=progress->total)return true;
    @try {
        Checkpoint(progress->attempt);
        Progress(progress->attempt,@"payload",progress->title,AssistantPayloadStart,BigSurCompressionStart,
            MIN(1,(double)consumed/progress->total),0);
        progress->next=consumed+(32ULL<<20);
        return true;
    } @catch(__unused NSException *error) {return false;}
}

static NSURL *PrepareAssistantCryptexDecoder(NSURL *package,NSURL *runtime,NSURL *attempt) {
    // This is the small installer-app Payload, not SharedSupport or the System
    // archive. Use its matching decoder on every host, including11/12 where
    // libParallelCompression does not export RawImagePatch. No Apple Mach-O
    // edits, host library replacement or translated process is involved.
    NSURL *work=At(attempt,@"InstallerDecoder");Directory(work);
    Progress(attempt,@"installer-tools",@"Preparing startup tools…",.02,.025,-1,3);
    PayloadProgress progress={.attempt=attempt,.cancel=At(attempt,@"Cancel"),
        .stage=@"installer-tools",.title=@"Preparing startup tools…",.start=.02,.end=.025};
    ExtractCorePayload(package,work,&progress);Checkpoint(attempt);
    NSError *error=nil;NSFileManager *fm=NSFileManager.defaultManager;
    NSArray<NSURL *> *apps=[fm contentsOfDirectoryAtURL:At(work,@"Applications")
        includingPropertiesForKeys:nil options:0 error:&error];
    NSURL *framework=nil;
    for(NSURL *app in apps) {
        NSURL *candidate=At(app,@"Contents/Frameworks/OSInstallerSetup.framework/Versions/A");
        if([fm isReadableFileAtPath:At(candidate,@"Frameworks/OSInstallerSetupInternal.framework/Versions/A/OSInstallerSetupInternal").path]) {
            framework=candidate;break;
        }
    }
    Require(framework!=nil,@"The installer’s startup tools are unavailable.");
    NSURL *decoder=At(framework,@"Resources/VirtualIntelMacCryptexDecoder");
    Require([fm copyItemAtURL:At(runtime,@"HostInstall/cryptex-decoder") toURL:decoder error:&error],error.localizedDescription);
    // Resolve all original framework dependencies before expensive OS staging.
    Run(decoder.path,@[@"--library",VIMAssistantCryptexDecoderLibrary(decoder).path,@"--availability"],nil,NO);
    Checkpoint(attempt);return decoder;
}

static void AssembleAssistant(NSURL *attempt,NSDictionary *request,NSData *password,NSURL *runtime) {
    NSInteger size=[request[@"DiskGiB"] integerValue];
    NSString *source=VIMAssistantPackageSource(request[@"Media"]),*name=request[@"Username"];
    NSString *guest=request[@"Guest"],*release=VIMReleaseNameForGuest(guest);
    NSInteger major=VIMMajorVersionForGuest(guest).integerValue;
    Require((major>=11&&major<=15)||major==26,@"Host installation for this version of macOS is not ready yet.");
    VIMAssistantCacheLayout cacheLayout=major>=13?VIMAssistantCachesIntelCryptex:VIMAssistantCachesInline;
    Require([source isKindOfClass:NSString.class]&&source.isAbsolutePath,@"The original Apple installer package is missing.");
    NSURL *file=[NSURL fileURLWithPath:source],*qemu=At(runtime,@"bin/qemu-img");
    NSURL *stage=At(attempt,@"Staging.vintelmac"),*raw=At(attempt,@"Staging.sparsebundle"),*seal=At(attempt,@"SealInputs");
    NSURL *cache=DownloadCache(file,At(attempt.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent,@"Downloads"));
    char *mountTemplate=strdup([NSTemporaryDirectory() stringByAppendingPathComponent:@"VIM-HostInstall.XXXXXX"].fileSystemRepresentation);
    char *mountPath=mountTemplate?mkdtemp(mountTemplate):NULL;
    NSURL *mounts=mountPath?[NSURL fileURLWithFileSystemRepresentation:mountPath isDirectory:YES relativeToURL:nil]:nil;
    free(mountTemplate);Require(mounts!=nil,@"Could not make temporary mountpoints.");
    WritePlist(At(attempt,@"MountDirectory.plist"),@{@"Path":mounts.path});
    NSURL *media=At(mounts,@"Source"),*system=At(mounts,@"System");
    BOOL sourceAttached=NO;
    __block BOOL attached=NO,graphicsAttached=NO;
    int lease=-1;
    @try {
        if(cache) {
            lease=open(At(cache,@"Download.lock").fileSystemRepresentation,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
            Require(lease>=0&&!flock(lease,LOCK_SH|LOCK_NB),@"This download is busy. Try again after it finishes.");
        }
        struct statfs fs;
        Require(!statfs(attempt.fileSystemRepresentation,&fs),@"Could not inspect the installation workspace.");
        uint64_t free=(uint64_t)fs.f_bavail*fs.f_bsize,required=VIMAssistantHostScratchGiB(guest)<<30;
        if(free<required)StopOutOfSpace(@"The library drive does not have enough space to prepare this system.");
        for(NSString *path in @[@"Disks",@"Firmware",@"State",@"Logs",@"Installation"])Directory(At(stage,path));
        Directory(seal);Directory(media);Directory(system);
        Progress(attempt,@"validate",@"Checking Apple’s installer signature…",0,.025,-1,12);
        NSData *signature=Run(@"/usr/sbin/pkgutil",@[@"--check-signature",file.path],nil,YES);
        NSString *report=[[NSString alloc] initWithData:signature encoding:NSUTF8StringEncoding];
        BOOL signedByApple=NO;
        for(NSString *line in [report componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet])
            if([[line stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet] isEqual:@"Status: signed Apple Software"])
                signedByApple=YES;
        Require(signedByApple,@"The installer is not an original Apple-signed package.");
        Checkpoint(attempt);
        NSURL *decoder=cacheLayout==VIMAssistantCachesIntelCryptex?PrepareAssistantCryptexDecoder(file,runtime,attempt):nil;
        NSArray *mounted=Attach(@[@"-readonly",@"-noverify",@"-mountpoint",media.path,file.path]);sourceAttached=YES;
        NSString *device=WholeDevice(mounted);
        __block NSString *build=nil;
        NSString *identifier=NSUUID.UUID.UUIDString;
        __block NSString *volume=nil;
        NSDictionary *verified=nil;
        @try {
            Require(MountAt(mounted,media),@"The signed Apple installer could not be opened.");
            NSString *assetName=@"com_apple_MobileAsset_MacSoftwareUpdate";
            NSURL *asset=At(media,assetName);
            NSArray *assets=Plist(At(asset,[assetName stringByAppendingPathExtension:@"xml"]))[@"Assets"];
            NSArray<NSURL *> *contents=[NSFileManager.defaultManager contentsOfDirectoryAtURL:asset includingPropertiesForKeys:nil options:0 error:nil];
            NSArray<NSURL *> *archives=[contents filteredArrayUsingPredicate:[NSPredicate predicateWithBlock:^BOOL(NSURL *url,__unused NSDictionary *bindings){
                return [url.pathExtension isEqual:@"zip"];
            }]];
            Require(archives.count==1,@"The signed Apple installer does not have one System archive.");
            NSURL *zip=archives.firstObject;NSError *error=nil;
            NSDictionary *assetInfo=VIMAssistantAssetForArchive(assets,zip,major,&error);
            Require(assetInfo!=nil,error.localizedDescription);
            NSDictionary *inventory=VIMAssistantPayloadInventory(zip,assetInfo,major,&error);
            Require(inventory!=nil,error.localizedDescription);
            build=inventory[@"Build"];
            // The signed archive inventory already validates the requested
            // OS family. New point releases keep their own seal/boot assets;
            // Big Sur must not remain pinned to the first build we qualified.
            NSString *payloadTitle=[NSString stringWithFormat:@"Installing macOS %@…",release];
            (void)CreateSplitAPFSGroup(raw,nil,nil,attempt,size,YES,&attached);
            NSUUID *snapshot=NSUUID.UUID;
            __block NSDictionary *group=nil,*staged=nil;
            WithSplitAPFSDisk(raw,&attached,^(NSString *whole,__unused NSArray<NSDictionary *> *parts){
                group=SplitAPFSGroup([whole stringByAppendingString:@"s2"].lastPathComponent,attempt,YES);
                volume=group[APFSData][@"APFSVolumeUUID"];
                WithAPFSVolume(group[APFSSystem],system,^{
                    NSError *issue=nil;BigSurPayloadProgress progress={attempt,payloadTitle,[inventory[@"TotalEncodedLength"] unsignedLongLongValue],0};
                    BOOL installed=VIMBigSurStageSystemPayload(zip,inventory,system,cacheLayout,BigSurPayloadBytes,&progress,&issue);
                    Require(installed,issue.localizedDescription);
                });
            });
            // Finish the extraction mount and its filesystem transactions
            // before Apple's separate compression verifier scans the tree.
            // A Monterey scan left the live System mount returning EIO;
            // the same retained image checks clean and
            // compresses successfully after detach/reattach. Never force an
            // eject or continue past a failed unmount in the product path.
            WithSplitAPFSDisk(raw,&attached,^(NSString *whole,__unused NSArray<NSDictionary *> *parts){
                group=SplitAPFSGroup([whole stringByAppendingString:@"s2"].lastPathComponent,attempt,YES);
                WithAPFSVolume(group[APFSSystem],system,^{
                    NSDictionary *owner=@{@"FormatVersion":@1,@"Kind":@"VirtualIntelMacLegacySetup",@"Guest":guest,
                        @"Build":build,@"MachineIdentifier":identifier,@"VolumeUUID":volume,@"Enabled":@(VIMHostInstallCreatesAccount(request)),
                        @"AutomaticLogin":@([request[@"AutomaticLogin"] boolValue]),
                        @"HostLocale":request[@"HostLocale"]?:@{},
                        @"RemoteLogin":@([request[@"RemoteLogin"] boolValue]),@"FileSharing":@([request[@"FileSharing"] boolValue])};
                    staged=VIMStageBigSurMountedGroup(zip,system,group,runtime,attempt,mounts,owner,name,password,snapshot,seal,
                        cacheLayout,decoder,&graphicsAttached);
                });
            });
            Checkpoint(attempt);
            verified=VIMFinalizeBigSurDisk(raw,zip,group,staged,seal,staged[@"IntelEFI"],At(attempt,@"System.tree"),cacheLayout,
                ^BOOL(NSArray<NSString *> *paths,NSError **repairError){
                    __block BOOL repaired=YES;
                    __block NSError *repairIssue=nil;
                    Checkpoint(attempt);
                    WithSplitAPFSDisk(raw,&attached,^(NSString *whole,__unused NSArray<NSDictionary *> *parts){
                        NSDictionary *observed=SplitAPFSGroup([whole stringByAppendingString:@"s2"].lastPathComponent,attempt,YES);
                        Require([observed[APFSSystem][@"APFSVolumeUUID"] isEqual:group[APFSSystem][@"APFSVolumeUUID"]],
                            @"The staged Big Sur System volume changed.");
                        WithAPFSVolume(observed[APFSSystem],system,^{
                            for(NSString *path in paths) {
                                Checkpoint(attempt);
                                if(!VIMBigSurTrimCompressedFork(At(system,path),&repairIssue)){repaired=NO;break;}
                            }
                        });
                    });
                    if(!repaired&&repairError)*repairError=repairIssue;
                    return repaired;
                },^BOOL{
                return [NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path];
            },^(NSString *step){
                if([step isEqual:@"metadata"])Progress(attempt,@"catalog",
                    @"Preparing startup metadata…",.90,.92,-1,60);
                if([step isEqual:@"seal"])Progress(attempt,@"seal",@"Rebuilding Apple’s signed System volume…",.92,.95,-1,120);
                if([step isEqual:@"snapshot"])Progress(attempt,@"snapshot",@"Publishing and checking the startup snapshot…",.95,.96,-1,30);
            },&error);
            Require(verified!=nil,error.localizedDescription);
        } @finally {Run(HDI,@[@"detach",device?:media.path],nil,NO);sourceAttached=NO;}
        Remove(seal);Checkpoint(attempt);
        Progress(attempt,@"convert",@"Finalizing the virtual disk…",.96,.98,0,0);
        NSData *info=Convert(qemu,raw.path,@"apple-sparsebundle",At(stage,@"Disks/System.qcow2"),attempt,.96);
        Remove(raw);
        NSString *graphicsKey=[guest isEqual:@"bigsur"]?@"BigSurGraphicsStaged":@"AssistantGraphicsStaged";
        PublishSystem(stage,attempt,runtime,request,identifier,volume,build,YES,YES,info,cache?@[cache.lastPathComponent]:@[],
            @{graphicsKey:@YES,@"BootSnapshotUUID":verified[@"SnapshotUUID"]});
    } @catch(NSException *failure) {
        // Explicit component-debug runs can retain a failed, unpublished disk
        // for detached inspection. Normal users and cancellation still reclaim
        // it; this never publishes a failed image or skips its verification.
        BOOL retain=[NSProcessInfo.processInfo.environment[@"VIM_HOST_INSTALL_RETAIN_FAILURE"] isEqual:@"1"]&&
            ![NSFileManager.defaultManager fileExistsAtPath:At(attempt,@"Cancel").path];
        if(!retain&&!sourceAttached&&!attached&&!graphicsAttached)
            for(NSURL *url in @[raw,seal,stage,At(attempt,@"Graphics.sparsebundle")])
                [NSFileManager.defaultManager removeItemAtURL:url error:nil];
        if(retain)fprintf(stderr,"Retained unpublished assistant workspace for explicit component debugging.\n");
        @throw failure;
    } @finally {
        if(lease>=0)close(lease);
        NSURL *decoderWork=At(attempt,@"InstallerDecoder");
        if([NSFileManager.defaultManager fileExistsAtPath:decoderWork.path])Remove(decoderWork);
        for(NSString *leaf in @[@"Source",@"System",@"Data",@"Preboot",@"Recovery",@"GraphicsSource",@"GraphicsWork"])
            rmdir(At(mounts,leaf).fileSystemRepresentation);
        rmdir(mounts.fileSystemRepresentation);
    }
}

static void Assemble(NSURL *attempt,NSDictionary *request,NSData *password) {
    NSInteger size=[request[@"DiskGiB"] integerValue];
    NSString *name=request[@"Username"],*guest=request[@"Guest"];
    BOOL tiger=[guest isEqual:@"tiger"];
    NSDictionary *tigerUpdates=request[@"TigerUpdates"];
    Require(!tigerUpdates||tiger,@"This update recipe applies only to Tiger.");
    NSDictionary *leopardUpdates=request[@"LeopardUpdates"];
    Require(!leopardUpdates||[guest isEqual:@"leopard"],@"This update recipe applies only to Leopard.");
    NSDictionary *snowUpdates=request[@"SnowLeopardUpdates"];
    Require(!snowUpdates||[guest isEqual:@"snowleopard"],@"This update recipe applies only to Snow Leopard.");
    NSDictionary *lionUpdates=request[@"LionUpdates"];
    Require(!lionUpdates||[guest isEqual:@"lion"],@"This update recipe applies only to Lion.");
    NSDictionary *mountainUpdates=request[@"MountainLionUpdates"];
    Require(!mountainUpdates||[guest isEqual:@"mountainlion"],@"This update recipe applies only to Mountain Lion.");
    NSDictionary *mavericksUpdates=request[@"MavericksUpdates"];
    Require(!mavericksUpdates||[guest isEqual:@"mavericks"],@"This update recipe applies only to Mavericks.");
    NSDictionary *yosemiteUpdates=request[@"YosemiteUpdates"];
    Require(!yosemiteUpdates||[guest isEqual:@"yosemite"],@"This update recipe applies only to Yosemite.");
    NSDictionary *elCapitanUpdates=request[@"ElCapitanUpdates"];
    Require(!elCapitanUpdates||[guest isEqual:@"elcapitan"],@"This update recipe applies only to El Capitan.");
    NSDictionary *sierraUpdates=request[@"SierraUpdates"];
    Require(!sierraUpdates||[guest isEqual:@"sierra"],@"This update recipe applies only to Sierra.");
    NSDictionary *highSierraUpdates=request[@"HighSierraUpdates"];
    Require(!highSierraUpdates||[guest isEqual:@"highsierra"],@"This update recipe applies only to High Sierra.");
    NSDictionary *mojaveUpdates=request[@"MojaveUpdates"];
    Require(!mojaveUpdates||[guest isEqual:@"mojave"],@"This update recipe applies only to Mojave.");
    NSDictionary *catalinaUpdates=request[@"CatalinaUpdates"];
    Require(!catalinaUpdates||[guest isEqual:@"catalina"],@"This update recipe applies only to Catalina.");
    NSNumber *updateChoice=request[@"DownloadSecurityUpdates"];
    BOOL updatesRequested=updateChoice?updateChoice.boolValue:![request[@"UserSelectedInstaller"] boolValue];
    if(!updatesRequested) {
        tigerUpdates=nil;leopardUpdates=nil;snowUpdates=nil;lionUpdates=nil;
        mountainUpdates=nil;mavericksUpdates=nil;yosemiteUpdates=nil;elCapitanUpdates=nil;
        sierraUpdates=nil;highSierraUpdates=nil;mojaveUpdates=nil;catalinaUpdates=nil;
    }
    NSDictionary *profile=RestoreProfile(guest);
    BOOL wrappedESD=[profile[@"WrappedESD"] boolValue],nestedSystem=[profile[@"NestedSystem"] boolValue];
    BOOL apfs=[profile[@"APFS"] boolValue];
    BOOL pvg=[profile[@"PVG"] boolValue];
    BOOL splitAPFS=[profile[@"APFSVolumeGroup"] boolValue];
    BOOL sierra=[guest isEqual:@"sierra"];
    BOOL splitMedia=wrappedESD||nestedSystem;
    BOOL nativeHFSCache=[@[@"leopard",@"snowleopard",@"lion",@"mountainlion",@"mavericks",@"yosemite",@"elcapitan"] containsObject:guest];
    NSString *title=profile[@"Name"],*expectedBuild=profile[@"Build"];
    NSDictionary *inputs=request[@"Media"];
    NSString *input=inputs[@"installer"]?:inputs[@"diskimage"]?:inputs[@"esd"];
    BOOL createAccount=VIMHostInstallCreatesAccount(request);
    Require(size>=8&&size<=2048&&(!createAccount||([name isKindOfClass:NSString.class]&&
            [name rangeOfString:@"^[a-z][a-z0-9_-]{0,30}$" options:NSRegularExpressionSearch].location!=NSNotFound)),
            @"The virtual disk size or account name is invalid.");
    BOOL assistant=VIMIsAssistantGuest(guest);
    Require((assistant||([input isKindOfClass:NSString.class]&&input.isAbsolutePath))&&[request[@"Application"] isAbsolutePath],
            @"Choose an original installer disk image.");
    NSURL *runtime=At([NSURL fileURLWithPath:request[@"Application"]],@"Contents/Resources/Runtime");
    NSURL *support=At(At(runtime,@"OpenGLGuests"),guest),*qemu=At(runtime,@"bin/qemu-img");
    NSString *supportFile=pvg?[NSString stringWithFormat:@"GuestPayloads/%@.iso",guest]:nil;
    Require([NSFileManager.defaultManager isReadableFileAtPath:(pvg?At(runtime,supportFile):At(support,@"GLEngine")).path]&&
            [NSFileManager.defaultManager isExecutableFileAtPath:qemu.path]&&
            (!pvg||[NSFileManager.defaultManager isExecutableFileAtPath:At(runtime,@"HostInstall/GraphicsTools/guest-prepare").path]),
            @"The bundled guest support is unavailable.");
    if(assistant){AssembleAssistant(attempt,request,password,runtime);return;}
    NSURL *stage=At(attempt,@"Staging.vintelmac"),*image=At(attempt,@"Staging.sparsebundle");
    NSURL *appleFiles=splitAPFS?At(attempt,@"AppleFiles.sparsebundle"):image;
    NSURL *sourceFile=[NSURL fileURLWithPath:input],*appSupport=nil;
    BOOL manualESDApp=[request[@"UserSelectedInstaller"] boolValue]&&VIMHostInstallUsesESDApp(guest,input);
    BOOL manualDVD=[request[@"UserSelectedInstaller"] boolValue]&&VIMHostInstallUsesDVD(guest,input);
    if(manualESDApp) {
        appSupport=At(sourceFile,@"Contents/SharedSupport");
        sourceFile=At(appSupport,@"InstallESD.dmg");input=sourceFile.path;
        // Keep splitMedia's package/Recovery layout, but no outer download
        // package needs expanding when the user supplied the actual app.
        wrappedESD=NO;
    }
    NSURL *library=attempt.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent.URLByDeletingLastPathComponent;
    NSURL *downloads=At(library,@"Downloads");
    NSMutableArray<NSNumber *> *leases=[NSMutableArray array];
    NSMutableArray<NSString *> *downloadKeys=[NSMutableArray array];
    NSMutableDictionary<NSString *,NSURL *> *mediaFiles=[NSMutableDictionary dictionary];
    BOOL splitCatalog=apfs&&!manualESDApp;
    NSArray *roles=splitCatalog?@[@"assistant",@"recovery",@"esd"]:@[@"image"];
    for(NSString *role in roles) {
        NSString *path=splitCatalog?inputs[role]:sourceFile.path;
        Require([path isKindOfClass:NSString.class]&&path.isAbsolutePath,@"An original installer component is missing.");
        mediaFiles[role]=[NSURL fileURLWithPath:path];
    }
    // DiskImages refuses custom mountpoints on exFAT. Keep only empty
    // mountpoint directories on the host filesystem; image bytes stay with
    // the library on its chosen drive.
    char *mountTemplate=strdup([NSTemporaryDirectory() stringByAppendingPathComponent:@"VIM-HostInstall.XXXXXX"].fileSystemRepresentation);
    char *mountPath=mkdtemp(mountTemplate);
    NSURL *mounts=mountPath?[NSURL fileURLWithFileSystemRepresentation:mountPath isDirectory:YES relativeToURL:nil]:nil;
    free(mountTemplate);
    Require(mounts!=nil,@"Could not make temporary mountpoints.");
    WritePlist(At(attempt,@"MountDirectory.plist"),@{@"Path":mounts.path});
    NSURL *media=At(mounts,@"Source"),*root=At(mounts,@"Target");
    NSURL *esdMount=At(mounts,@"InstallESD"),*base=At(mounts,@"BaseSystem");
    NSURL *recoverySource=At(mounts,@"RecoverySource"),*recoveryMeta=At(attempt,@"RecoveryHDMeta.dmg");
    if(apfs&&manualESDApp)recoverySource=appSupport;
    NSURL *preboot=At(attempt,@"Preboot");
    NSURL *esd=nil,*recovery=At(attempt,@"Recovery.sparsebundle");
    NSURL *disk=At(stage,@"Disks/System.qcow2");
    NSString *identifier=NSUUID.UUID.UUIDString,*volume=nil,*build=nil;
    NSArray *appliedUpdates=nil;
    uint32_t bootFolder=0,bootFile=0;uint64_t partition=0,length=0;
    NSDictionary *permissions=nil,*group=nil,*projected=nil;
    BOOL sourceAttached=NO,targetAttached=NO,esdAttached=NO,baseAttached=NO,recoveryAttached=NO,recoverySourceAttached=NO,graphicsAttached=NO;
    BOOL groupAttached=NO;
    NSString *networkAdapter=nil;
    @try {
        NSMutableArray<NSURL *> *leaseFiles=[mediaFiles.allValues mutableCopy];
        NSDictionary *updateInputs=tigerUpdates?:leopardUpdates?:snowUpdates?:lionUpdates?:mountainUpdates?:mavericksUpdates?:yosemiteUpdates?:elCapitanUpdates?:sierraUpdates?:highSierraUpdates?:mojaveUpdates?:catalinaUpdates;
        for(NSString *role in updateInputs) {
            NSString *path=updateInputs[role];
            if([path isKindOfClass:NSString.class]&&path.isAbsolutePath&&[NSFileManager.defaultManager fileExistsAtPath:path])
                [leaseFiles addObject:[NSURL fileURLWithPath:path]];
        }
        for(NSURL *file in leaseFiles) {
            NSURL *cache=DownloadCache(file,downloads);
            if(!cache||[downloadKeys containsObject:cache.lastPathComponent])continue;
            int lease=open(At(cache,@"Download.lock").fileSystemRepresentation,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
            Require(lease>=0,@"The installer download is unavailable.");
            [leases addObject:@(lease)];
            Require(!flock(lease,LOCK_SH|LOCK_NB),@"This download is busy. Try again after it finishes.");
            [downloadKeys addObject:cache.lastPathComponent];
        }
        for(NSString *path in @[@"Disks",@"Firmware",@"State",@"Logs",@"Installation"])Directory(At(stage,path));
        Directory(media);Directory(root);
        if(splitMedia){Directory(esdMount);Directory(base);}
        Checkpoint(attempt);
        if(splitCatalog) {
            CheckMedia(mediaFiles[@"assistant"],attempt,title,0,.001);
            CheckMedia(mediaFiles[@"recovery"],attempt,title,.001,.004);
            CheckMedia(mediaFiles[@"esd"],attempt,title,.004,.025);
        } else if(manualESDApp||manualDVD) {
            Progress(attempt,@"validate",@"Checking your installer disk image…",0,.025,-1,8);
            // Explicit user-selected media is not one exact catalog download.
            // Read-only media admission plus the mounted OS family below
            // replaces the catalog byte identity; no package scripts run here.
            // Raw ISO/CDR discs need not carry a UDIF checksum. Their read-only
            // mount, actual system version and required packages are checked
            // below; do not confuse absence of a checksum with a bad disc.
            if(manualESDApp)Run(HDI,@[@"verify",sourceFile.path],nil,NO);
            Checkpoint(attempt);
            if(apfs) {
                Require([NSFileManager.defaultManager isReadableFileAtPath:At(appSupport,@"BaseSystem.dmg").path]&&
                        [NSFileManager.defaultManager isReadableFileAtPath:At(appSupport,@"BaseSystem.chunklist").path],
                        @"Choose a complete installer app with its Recovery files.");
                Run(HDI,@[@"verify",At(appSupport,@"BaseSystem.dmg").path],nil,NO);Checkpoint(attempt);
            }
        } else CheckMedia(sourceFile,attempt,title,0,.025);
        Progress(attempt,@"mount",[NSString stringWithFormat:@"Opening the %@ installer…",title],.025,.04,-1,4);
        if(splitCatalog) {
            Progress(attempt,@"expand",[NSString stringWithFormat:@"Unpacking the original %@ disk images…",title],.04,.13,-1,40);
            esd=At(attempt,@"InstallESD.dmg");
            Run(@"/usr/bin/xar",@[@"-x",@"-C",attempt.path,@"-f",mediaFiles[@"esd"].path,esd.lastPathComponent],nil,NO);
            Checkpoint(attempt);
            Run(@"/usr/bin/xar",@[@"-x",@"-C",attempt.path,@"-f",mediaFiles[@"recovery"].path,recoveryMeta.lastPathComponent],nil,NO);
            input=esd.path;
        }
        NSArray *source=Attach(@[@"-readonly",@"-noverify",@"-owners",@"off",@"-mountpoint",media.path,input]);
        NSString *mediaDevice=WholeDevice(source);
        sourceAttached=YES;
        @try {
            Require(mediaDevice&&MountAt(source,media),@"The original installer did not mount.");
            NSURL *systemMedia=media;
            if(wrappedESD) {
                Progress(attempt,@"expand",[NSString stringWithFormat:@"Unpacking the original %@ installer…",title],.04,.13,-1,45);
                esd=ExpandAppleESD(attempt,media,profile[@"InstallerPackage"]?:@"InstallMacOSX.pkg");Checkpoint(attempt);
                NSArray *entities=Attach(@[@"-readonly",@"-noverify",@"-owners",@"off",@"-mountpoint",esdMount.path,esd.path]);
                esdAttached=YES;
                Require(MountAt(entities,esdMount),@"The installer’s InstallESD did not mount.");
                systemMedia=esdMount;
            }
            if(splitMedia) {
                NSURL *baseSource=systemMedia;
                if(splitCatalog) {
                    Directory(recoverySource);
                    NSArray *recoveryParts=Attach(@[@"-readonly",@"-noverify",@"-owners",@"off",@"-mountpoint",recoverySource.path,recoveryMeta.path]);
                    recoverySourceAttached=YES;
                    Require(MountAt(recoveryParts,recoverySource),@"The installer’s Recovery image did not mount.");
                    baseSource=recoverySource;
                } else if(apfs)baseSource=appSupport;
                NSArray *entities=Attach(@[@"-readonly",@"-noverify",@"-owners",@"off",@"-mountpoint",base.path,At(baseSource,@"BaseSystem.dmg").path]);
                baseAttached=YES;
                Require(MountAt(entities,base),@"The installer’s BaseSystem did not mount.");
            }
            NSDictionary *sourceVersion=Plist(At(nestedSystem?base:systemMedia,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]));
            build=sourceVersion[@"ProductBuildVersion"];
            Require(VIMHostInstallDVDVersionMatches(guest,sourceVersion)||
                VIMHostInstallESDVersionMatches(guest,sourceVersion),
                @"Choose a complete installer for the selected Mac OS version.");
            if(![build isEqual:expectedBuild]) {
                // Optional delta-update actions describe a known base. A new
                // Apple media revision must not block installing that base or
                // receive an older/inapplicable delta recipe.
                tigerUpdates=nil;leopardUpdates=nil;snowUpdates=nil;lionUpdates=nil;
                mountainUpdates=nil;mavericksUpdates=nil;yosemiteUpdates=nil;elCapitanUpdates=nil;
                sierraUpdates=nil;highSierraUpdates=nil;mojaveUpdates=nil;catalinaUpdates=nil;
                if(updatesRequested)WritePlist(At(attempt,@"HostUpdateNotice.plist"),@{
                    @"Status":@"original-system-kept",@"Reason":
                    @"This installer contains a different system revision. Additional updates can be installed inside your virtual Mac."});
            }
            expectedBuild=build;
            Run(HDI,@[@"create",@"-size",[NSString stringWithFormat:@"%ldg",(long)size],@"-type",@"SPARSEBUNDLE",
                @"-layout",@"GPTSPUD",@"-fs",@"Journaled HFS+",@"-volname",splitAPFS?APFSSplitSeed:APFSSystem,image.path],nil,NO);
            if(splitAPFS) {
                group=CreateSplitAPFSGroup(image,base,mounts,attempt,size,NO,&groupAttached);
                Run(HDI,@[@"create",@"-size",@"32g",@"-type",@"SPARSEBUNDLE",@"-layout",@"GPTSPUD",
                    @"-fs",@"Journaled HFS+",@"-volname",@"Catalina Apple Files",appleFiles.path],nil,NO);
            }
            if(splitMedia&&!apfs) {
                // EFI ends at LBA 409639; keep seven spare sectors before the
                // backup GPT, as on a disk produced by Apple's installer.
                uint64_t sectors=((uint64_t)size<<21)-40-409640-VIMHFSRecoverySectors;
                Run(HDI,@[@"resize",@"-partitiononly",@"-partitionID",@"2",@"-sectors",[@(sectors) stringValue],image.path],nil,NO);
            }
            NSArray *mounted=Attach(@[@"-owners",@"off",@"-mountpoint",root.path,appleFiles.path]);
            NSString *device=WholeDevice(mounted);
            targetAttached=YES;
            NSException *stageFailure=nil;
            @try {
                Require(device&&![device isEqual:mediaDevice]&&MountAt(mounted,root),@"The new HFS+ volume did not mount.");
                struct statfs fs;struct stat targetInfo,sourceInfo,parentInfo;
                Require(!statfs(root.fileSystemRepresentation,&fs)&&!strcmp(fs.f_fstypename,"hfs")&&
                    !stat(root.fileSystemRepresentation,&targetInfo)&&!stat(media.fileSystemRepresentation,&sourceInfo)&&
                    !stat(mounts.fileSystemRepresentation,&parentInfo)&&
                    targetInfo.st_dev!=sourceInfo.st_dev&&targetInfo.st_dev!=parentInfo.st_dev,
                    @"The destination is not the new scratch HFS+ mount.");
                NSDictionary *geometry=ToolPlist(@"/usr/sbin/diskutil",@[@"info",@"-plist",root.path]);
                volume=splitAPFS?group[APFSData][@"APFSVolumeUUID"]:geometry[@"VolumeUUID"];
                partition=[geometry[@"PartitionMapPartitionOffset"] unsignedLongLongValue];
                length=[geometry[@"Size"] unsignedLongLongValue];
                Require(volume.length&&partition&&length,@"Could not locate the new HFS+ partition.");
                NSDictionary *owner=@{@"FormatVersion":@1,@"Kind":@"VirtualIntelMacLegacySetup",@"Guest":guest,
                    @"MachineIdentifier":identifier,@"VolumeUUID":volume,@"Enabled":@(createAccount),
                    @"AutomaticLogin":@([request[@"AutomaticLogin"] boolValue]),@"RemoteLogin":@([request[@"RemoteLogin"] boolValue]),
                    @"HostLocale":request[@"HostLocale"]?:@{},
                    @"FileSharing":@([request[@"FileSharing"] boolValue])};
                VIMInstallTarget *target=[VIMInstallTarget new];target.root=root;target.metadata=[NSMutableDictionary dictionary];
                NSDictionary *firmlinks=nil;
                if(tiger) {
                    ExtractTigerPackages(target,media,attempt);
                    if(tigerUpdates) {
                        appliedUpdates=ApplyTigerUpdates(target,attempt,tigerUpdates);
                        build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                    }
                }
                else firmlinks=ExtractFlatPackages(target,At(systemMedia,splitMedia?@"Packages":@"System/Installation/Packages"),
                    splitMedia?base:nil,attempt,guest,title);
                if(leopardUpdates) {
                    appliedUpdates=ApplyLeopardUpdates(target,attempt,leopardUpdates);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                }
                if(snowUpdates) {
                    appliedUpdates=ApplySnowLeopardUpdates(target,attempt,snowUpdates);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                }
                if(lionUpdates) {
                    appliedUpdates=ApplyLionUpdates(target,attempt,lionUpdates);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                }
                if(mountainUpdates) {
                    appliedUpdates=ApplyMountainLionUpdates(target,attempt,mountainUpdates);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                }
                if(mavericksUpdates) {
                    appliedUpdates=ApplyMavericksUpdates(target,attempt,mavericksUpdates);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                }
                if(yosemiteUpdates) {
                    appliedUpdates=ApplyYosemiteUpdates(target,attempt,yosemiteUpdates);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                }
                if(elCapitanUpdates) {
                    appliedUpdates=ApplyElCapitanUpdates(target,attempt,elCapitanUpdates);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                }
                if(sierraUpdates) {
                    appliedUpdates=ApplySierraUpdates(target,attempt,sierraUpdates);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                }
                if(highSierraUpdates||mojaveUpdates||catalinaUpdates) {
                    appliedUpdates=ApplyAPFSOSUpdates(target,attempt,highSierraUpdates?:mojaveUpdates?:catalinaUpdates,guest);
                    build=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]))[@"ProductBuildVersion"];
                    if(appliedUpdates.count)MountRecoveryImages(At(attempt,@"UpdatedRecoveryHDMeta.dmg"),
                        recoverySource,base,&recoverySourceAttached,&baseAttached);
                }
                Checkpoint(attempt);
                NSDictionary *installed=Plist(At(root,[CoreServices stringByAppendingPathComponent:@"SystemVersion.plist"]));
                Require(VIMHostInstallDVDVersionMatches(guest,installed)||
                    VIMHostInstallESDVersionMatches(guest,installed),
                    @"The installed packages do not match the selected Mac OS version.");
                // Authentic Recovery and installed packages can have different
                // builds. Use the installed files for graphics/cache inputs.
                build=installed[@"ProductBuildVersion"];
                Progress(attempt,@"account",createAccount?@"Setting up your account and graphics…":@"Setting up graphics support…",
                    appliedUpdates.count?(nestedSystem?.82:.84):.80,nestedSystem?.83:.85,-1,1);
                if(pvg)StagePVGGraphics(target,target.root,runtime,attempt,mounts,guest,build,&graphicsAttached);
                else StageSupport(target,support,owner,build,expectedBuild);
                StageAccount(target,target.root,owner,name,password);Checkpoint(attempt);
                StageKextHelperCache(target,guest);Checkpoint(attempt);
                if([guest isEqual:@"leopard"]) {
                    NSString *plugins=@"System/Library/Extensions/IONetworkingFamily.kext/Contents/PlugIns";
                    networkAdapter=VIMHostInstallNetworkAdapter(guest,
                        Plist(At(root,[plugins stringByAppendingPathComponent:@"AppleIntel8254XEthernet.kext/Contents/Info.plist"])),
                        Plist(At(root,[plugins stringByAppendingPathComponent:@"AppleRTL8139Ethernet.kext/Contents/Info.plist"])));
                }
                if(nativeHFSCache&&!nestedSystem) {
                    Progress(attempt,@"dyld",@"Building the shared libraries cache…",.85,splitMedia?.875:.89,-1,12);
                    StageNativeHFSSharedCaches(target,runtime,guest);
                    Checkpoint(attempt);
                }
                if(nestedSystem) {
                    BOOL updatedHFS=([guest isEqual:@"elcapitan"]||sierra)&&appliedUpdates.count;
                    BOOL provided=[profile[@"ProvidedStartupCache"] boolValue]&&!updatedHFS;
                    Progress(attempt,@"prelink",provided?@"Preparing the startup cache…":@"Building the startup cache…",.83,.87,-1,provided?1:5);
                    if(apfs) {
                        NSString *bootUUID=splitAPFS?group[APFSSystem][@"APFSVolumeUUID"]:volume;
                        StageAPFSStartup(target,base,attempt,mounts,preboot,bootUUID,splitAPFS,runtime,appliedUpdates.count>0);
                        Checkpoint(attempt);
                        if(!splitAPFS) {
                            Progress(attempt,@"dyld",@"Building the shared libraries cache…",.87,.91,-1,85);
                            StageNativeHFSSharedCaches(target,runtime,guest);
                        }
                    } else if(provided) {
                        BOOL elCapitan=[guest isEqual:@"elcapitan"];
                        StageProvidedStartupCache(target,elCapitan);
                        StampStartupCaches(root,elCapitan?@[PrelinkedCache,StartupCache]:@[PrelinkedCache]);
                    }
                    else {
                        BOOL legacy=[guest isEqual:@"mavericks"];
                        NSURL *tool=updatedHFS?PreparePeriodTool(At(root,@"usr/sbin/kextcache"),mounts,runtime):At(base,@"usr/sbin/kextcache");
                        @try {
                            StagePrelinkedKernel(target,tool,attempt,
                                legacy?@"mach_kernel":@"System/Library/Kernels/kernel",
                                legacy?@[StartupCache]:sierra?@[PrelinkedCache]:@[PrelinkedCache,StartupCache],updatedHFS);
                            if(sierra)StageProvidedStartupCache(target,NO);
                            if(updatedHFS)StampStartupCaches(root,sierra?@[PrelinkedCache]:@[PrelinkedCache,StartupCache]);
                        } @finally {if(updatedHFS)Remove(tool);}
                    }
                    Checkpoint(attempt);
                    if(nativeHFSCache||sierra) {
                        Progress(attempt,@"dyld",@"Building the shared libraries cache…",.87,sierra?.90:.885,-1,sierra?75:20);
                        StageNativeHFSSharedCaches(target,runtime,guest);
                        Checkpoint(attempt);
                    }
                }
                if(splitAPFS) {
                    Progress(attempt,@"apfs",@"Preparing Catalina’s System and Data volumes…",.91,.95,-1,420);
                    projected=StageCatalinaGroup(image,target,firmlinks,group,base,recoverySource,runtime,mounts,preboot,attempt,&groupAttached);
                } else {
                    double start=apfs?.91:sierra?.90:nativeHFSCache?(nestedSystem?.885:splitMedia?.875:.89):nestedSystem?.87:.85;
                    double end=apfs?.925:sierra?.92:splitMedia?.89:.91;
                    Progress(attempt,@"catalog",@"Preparing the boot volume…",start,end,-1,tiger?10:12);
                    permissions=[target catalogPermissions];
                    struct stat folder,efi;
                    Require(!stat(At(root,CoreServices).fileSystemRepresentation,&folder)&&!stat(At(root,BootEFI).fileSystemRepresentation,&efi),
                            @"The installed boot files are missing.");
                    bootFolder=(uint32_t)folder.st_ino;bootFile=(uint32_t)efi.st_ino;
                }
            } @catch(NSException *failure) {
                stageFailure=failure;@throw;
            } @finally {
                @try {
                    Run(HDI,@[@"detach",device?:appleFiles.path],nil,NO);
                    targetAttached=NO;
                } @catch(NSException *cleanup) {
                    if(!stageFailure)@throw;
                    fprintf(stderr,"Could not detach the scratch volume: %s\n",cleanup.reason.UTF8String);
                }
            }
            if(splitAPFS) {
                Remove(appleFiles);Remove(preboot);Checkpoint(attempt);
            } else if(apfs) {
                Checkpoint(attempt);
                NSError *error=nil;BOOL ready=VIMFinalizeHFSCatalog(image,partition,length,permissions,bootFolder,bootFile,&error);
                Require(ready,error.localizedDescription);
                Progress(attempt,@"apfs",@"Preparing APFS and Recovery…",.925,.96,-1,20);
                StageAPFSVolumes(image,base,recoverySource,root,mounts,preboot,volume,attempt,&targetAttached);
                Remove(preboot);Checkpoint(attempt);
            } else if(splitMedia) {
                Checkpoint(attempt);
                Progress(attempt,@"recovery",@"Preparing Recovery HD…",sierra?.92:.89,.94,-1,6);
                StageRecovery(recovery,root,systemMedia,nestedSystem?base:nil,
                    profile[@"RecoveryCache"]?:StartupCache,&recoveryAttached);
            }
        } @finally {
            @try {
                if(baseAttached){Run(HDI,@[@"detach",base.path],nil,NO);baseAttached=NO;}
                if(recoverySourceAttached){Run(HDI,@[@"detach",recoverySource.path],nil,NO);recoverySourceAttached=NO;}
                if(esdAttached){Run(HDI,@[@"detach",esdMount.path],nil,NO);esdAttached=NO;}
            } @finally {
                Run(HDI,@[@"detach",mediaDevice?:media.path],nil,NO);
                // Leave scratch alone if a detach threw: no cleanup of a mount.
                sourceAttached=NO;
            }
        }
        if(esd)Remove(esd);
        if(apfs)RemoveRecoveryWork(attempt);
        Checkpoint(attempt);
        NSError *error=nil;
        if(!apfs) {
            BOOL finalized=VIMFinalizeHFSCatalog(image,partition,length,permissions,bootFolder,bootFile,&error);
            Require(finalized,error.localizedDescription);
            if(splitMedia) {
                BOOL added=VIMAddHFSRecovery(image,recovery,&error);
                Require(added,error.localizedDescription);
                Remove(recovery);Checkpoint(attempt);
            }
        }
        double conversionStart=apfs?.96:splitMedia?.94:.91;
        NSData *info=nil;
        if(splitAPFS) {
            info=FinalizeCatalinaDisk(qemu,image,disk,attempt,group,projected,conversionStart);
        } else {
            Progress(attempt,@"convert",@"Finalizing the virtual disk…",conversionStart,.98,0,0);
            // HFS catalog finalization and APFS staging have already detached
            // this owned, unencrypted image. Read its materialized bands
            // directly so empty virtual capacity is a zero extent, not a
            // whole-disk scan through another DiskImages attachment.
            info=Convert(qemu,image.path,@"apple-sparsebundle",disk,attempt,conversionStart);
        }
        if(!splitAPFS)Remove(image);
        NSMutableDictionary *details=appliedUpdates?[@{@"HostAppliedAppleUpdates":appliedUpdates,@"OriginalMediaBuild":expectedBuild} mutableCopy]:nil;
        if(networkAdapter) {
            if(!details)details=[NSMutableDictionary dictionary];
            details[@"NetworkAdapter"]=networkAdapter;
        }
        NSDictionary *updateNotice=Plist(At(attempt,@"HostUpdateNotice.plist"))?:
            (updatesRequested?request[@"HostUpdateNotice"]:nil);
        if(updateNotice) {
            if(!details)details=[NSMutableDictionary dictionary];
            details[@"HostUpdateNotice"]=updateNotice;
        }
        PublishSystem(stage,attempt,runtime,request,identifier,volume,build,apfs,pvg,info,downloadKeys,details);
    } @catch(NSException *failure) {
        if(!sourceAttached&&!targetAttached&&!esdAttached&&!baseAttached&&!recoveryAttached&&!recoverySourceAttached&&!graphicsAttached&&!groupAttached) {
            [NSFileManager.defaultManager removeItemAtURL:image error:nil];
            if(splitAPFS) {
                [NSFileManager.defaultManager removeItemAtURL:appleFiles error:nil];
                [NSFileManager.defaultManager removeItemAtURL:At(attempt,@"RawScratch.sparsebundle") error:nil];
            }
            [NSFileManager.defaultManager removeItemAtURL:recovery error:nil];
            [NSFileManager.defaultManager removeItemAtURL:At(attempt,@"InstallESD.dmg") error:nil];
            [NSFileManager.defaultManager removeItemAtURL:At(attempt,@"AppleInstaller") error:nil];
            [NSFileManager.defaultManager removeItemAtURL:At(attempt,@"PrelinkedKernel") error:nil];
            [NSFileManager.defaultManager removeItemAtURL:recoveryMeta error:nil];
            [NSFileManager.defaultManager removeItemAtURL:At(attempt,@"UpdatedRecoveryHDMeta.dmg") error:nil];
            [NSFileManager.defaultManager removeItemAtURL:preboot error:nil];
            [NSFileManager.defaultManager removeItemAtURL:At(attempt,@"Graphics.sparsebundle") error:nil];
            [NSFileManager.defaultManager removeItemAtURL:stage error:nil];
        }
        @throw;
    } @finally {
        for(NSNumber *lease in leases)close(lease.intValue);
        for(NSString *name in @[@"kextcache",@"dz.dylib",@"update_dyld_shared_cache",@"update_dyld_shared_cache_root_mode",@"hfs_convert"])
            unlink(At(mounts,name).fileSystemRepresentation);
        // Never recursively remove a failed/busy mountpoint.
        rmdir(base.fileSystemRepresentation);rmdir(esdMount.fileSystemRepresentation);
        rmdir(media.fileSystemRepresentation);rmdir(root.fileSystemRepresentation);
        rmdir(recoverySource.fileSystemRepresentation);
        rmdir(At(mounts,@"GraphicsSource").fileSystemRepresentation);rmdir(At(mounts,@"GraphicsWork").fileSystemRepresentation);
        for(NSString *name in @[@"CatalinaSystem",@"CatalinaData",@"CatalinaBoot",@"CatalinaRaw"])
            rmdir(At(mounts,name).fileSystemRepresentation);
        rmdir(mounts.fileSystemRepresentation);
    }
}

int main(int argc,const char *argv[]) {
    @autoreleasepool {
        if(argc!=3||strcmp(argv[1],"--request"))return 2;
        @try {
            NSURL *attempt=[NSURL fileURLWithPath:NSFileManager.defaultManager.currentDirectoryPath isDirectory:YES];
            NSURL *url=At(attempt,@"Request.plist");
            NSURL *argument=[NSURL fileURLWithFileSystemRepresentation:argv[2] isDirectory:NO relativeToURL:nil];
            struct stat requestInfo;
            Require([url.URLByResolvingSymlinksInPath.path isEqual:argument.URLByResolvingSymlinksInPath.path]&&
                !lstat(url.fileSystemRepresentation,&requestInfo)&&S_ISREG(requestInfo.st_mode),
                @"The request is not in its private attempt.");
            Checkpoint(attempt);
            NSDictionary *request=Plist(url);
            Require([request[@"FormatVersion"] isEqual:@1]&&RestoreProfile(request[@"Guest"]?:@"")!=nil,
                    @"This worker does not support this installer.");
            Require(!request[@"CreateAccount"]||[request[@"CreateAccount"] isKindOfClass:NSNumber.class],
                    @"The account setup option is invalid.");
            NSData *password=[NSFileHandle.fileHandleWithStandardInput readDataOfLength:512];
            Require((password.length||!VIMHostInstallCreatesAccount(request))&&password.length<512&&
                    (!password.length||!memchr(password.bytes,0,password.length))&&
                    [[NSString alloc] initWithData:password encoding:NSUTF8StringEncoding]!=nil,@"The setup password is invalid.");
            Assemble(attempt,request,password);
            return 0;
        } @catch(NSException *failure) {
            fprintf(stderr,"%s\n",failure.reason.UTF8String?:"Could not prepare the installed system.");
            return [failure.userInfo[@"ExitStatus"] isEqual:@(ENOSPC)]?ENOSPC:1;
        }
    }
}
