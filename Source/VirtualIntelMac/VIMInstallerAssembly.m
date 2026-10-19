#import "VIMInstallerAssembly.h"
#import <signal.h>
#import <fcntl.h>
#import <unistd.h>
#import <sys/stat.h>
#import <errno.h>
#import <math.h>
#import "VIMLibraryLocation.h"
#include <sys/file.h>

static BOOL AssemblyError(NSError **error, NSString *message) {
    if (error) *error=[NSError errorWithDomain:@"VirtualIntelMac.InstallerAssembly" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
static BOOL RegularFile(NSURL *URL) {
    NSNumber *regular=nil;
    return URL.isFileURL && [URL getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil] && regular.boolValue;
}
static BOOL NonemptyFile(NSURL *URL) {
    NSNumber *size=nil;
    return RegularFile(URL)&&[URL getResourceValue:&size forKey:NSURLFileSizeKey error:nil]&&size.unsignedLongLongValue>0;
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
    return @{@"mojave":@"com.apple.InstallAssistant.Mojave",
             @"catalina":@"com.apple.InstallAssistant.Catalina",
             @"bigsur":@"com.apple.InstallAssistant.macOSBigSur"};
}
static BOOL InstallerVersionMatches(id version,NSString *guest) {
    if(![version isKindOfClass:NSString.class])return NO;
    NSString *family=@{@"mojave":@"10.14",@"catalina":@"10.15",@"bigsur":@"11"}[guest];
    if(!family)return NO;
    NSArray *components=[version componentsSeparatedByString:@"."];
    for(NSString *component in components)
        if(!component.length||[component rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789"] invertedSet]].location!=NSNotFound)return NO;
    return [version isEqual:family]||[version hasPrefix:[family stringByAppendingString:@"."]];
}
static BOOL UnlinkedDirectory(NSURL *URL) {
    struct stat st;return !lstat(URL.fileSystemRepresentation,&st)&&S_ISDIR(st.st_mode);
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
@end
@implementation VIMInstallerAssembly
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
        if([record[@"FormatVersion"] isEqual:@1]&&[record[@"Complete"] isEqual:@YES]&&[record[@"Length"] isKindOfClass:NSNumber.class]&&[source.scheme isEqual:@"https"]&&[source.host isEqual:@"swcdn.apple.com"]&&
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
           (![guest isEqual:@"bigsur"]||[media[@"SignedSharedSupportPackage"] isEqual:@YES])&&
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
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfURL:[URL URLByAppendingPathComponent:@"Contents/Info.plist"]];
    NSString *identifier=info[@"CFBundleIdentifier"];
    NSString *guest=nil;
    if([identifier isKindOfClass:NSString.class])for(NSString *candidate in InstallerIdentifiers())
        if([identifier isEqual:InstallerIdentifiers()[candidate]]){guest=candidate;break;}
    if(!guest){AssemblyError(error,@"Choose a complete installer app for macOS Mojave 10.14, macOS Catalina 10.15, or macOS Big Sur 11.");return nil;}
    return [self validateInstallerApplication:URL guest:guest error:error]?guest:nil;
}
+ (BOOL)validateInstallerApplication:(NSURL *)URL guest:(NSString *)guest error:(NSError **)error {
    NSString *identifier=InstallerIdentifiers()[guest];
    NSDictionary *info=[NSDictionary dictionaryWithContentsOfURL:[URL URLByAppendingPathComponent:@"Contents/Info.plist"]];
    NSURL *support=[URL URLByAppendingPathComponent:@"Contents/SharedSupport"];
    NSURL *installURL=[support URLByAppendingPathComponent:@"InstallInfo.plist"];
    NSDictionary *install=[NSDictionary dictionaryWithContentsOfURL:installURL];
    NSDictionary *system=[install[@"System Image Info"] isKindOfClass:NSDictionary.class]?install[@"System Image Info"]:nil;
    id version=system[@"version"];
    BOOL legacy=![guest isEqual:@"bigsur"];
    BOOL complete=NonemptyFile([URL URLByAppendingPathComponent:@"Contents/Resources/createinstallmedia"])&&
        NonemptyFile([support URLByAppendingPathComponent:legacy?@"InstallESD.dmg":@"SharedSupport.dmg"])&&
        (!legacy||NonemptyFile([support URLByAppendingPathComponent:@"BaseSystem.dmg"]));
    // Big Sur ships only SharedSupport.dmg, with OSVersion inside its asset
    // manifest. Media preparation checks that manifest before using boot files.
    // Do not reject Apple's complete app merely because InstallInfo is absent.
    BOOL versionMatches=(!legacy&&![NSFileManager.defaultManager fileExistsAtPath:installURL.path])||InstallerVersionMatches(version,guest);
    BOOL matches=identifier&&URL.isFileURL&&[URL.pathExtension.lowercaseString isEqual:@"app"]&&[info[@"CFBundleIdentifier"] isEqual:identifier]&&versionMatches;
    if(!matches||!complete)return AssemblyError(error,[NSString stringWithFormat:@"Choose the complete “Install macOS %@.app” installer for macOS %@. This file is a different version or is missing installation files.",@{@"mojave":@"Mojave",@"catalina":@"Catalina",@"bigsur":@"Big Sur"}[guest]?:@"",@{@"mojave":@"Mojave 10.14",@"catalina":@"Catalina 10.15",@"bigsur":@"Big Sur 11"}[guest]?:@""]);
    if(!legacy&&!InstallerPackageImage([support URLByAppendingPathComponent:@"SharedSupport.dmg"]))
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
// No shell, package scripts, privileged installer invocation, or output pipes.
// Logs stay in the job; a child producing much output cannot deadlock its parent.
- (BOOL)run:(NSString *)path arguments:(NSArray<NSString *> *)arguments timeout:(NSTimeInterval)timeout
    cleanup:(BOOL)cleanup error:(NSError **)error {
    if(self.cancelled&&!cleanup)return AssemblyError(error,@"Installer preparation was cancelled. Downloads were retained.");
    NSURL *log=[self.job URLByAppendingPathComponent:[NSString stringWithFormat:@"%02lu-%@.log",++self.step,path.lastPathComponent]];
    self.lastLog=log;
    int fd=open(log.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)return AssemblyError(error,@"Cannot create the installer preparation log.");
    NSFileHandle *output=[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:path];
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
        NSLocalizedDescriptionKey:@"The macOS installer couldn’t be prepared.",
        NSLocalizedRecoverySuggestionErrorKey:@"Your downloads are saved. Try again to resume setup. If it fails again, check that your library’s drive is connected and has enough free space (about 45 GB for Mojave or Catalina, 65 GB for Big Sur).",
        @"PreparationLog":log.path}];
    return NO;
}
- (NSURL *)assembleGuest:(NSString *)guest files:(NSDictionary<NSString *,NSURL *> *)files
    jobDirectory:(NSURL *)directory error:(NSError **)error {
    if(NSThread.isMainThread) { AssemblyError(error,@"Installer assembly must run on a background worker.");return nil; }
    NSDictionary *names=@{@"mojave":@"Install macOS Mojave.app",@"catalina":@"Install macOS Catalina.app",@"bigsur":@"Install macOS Big Sur.app"};
    NSString *name=names[guest];BOOL legacy=![guest isEqual:@"bigsur"];
    if(!name||!directory.isFileURL||!RegularFile(files[@"assistant"])||
       (legacy&&(!RegularFile(files[@"recovery"])||!RegularFile(files[@"esd"])))) {
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
    [self nextWorkTo:.30 seconds:legacy?45:180];
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
    if(candidates.count!=1){AssemblyError(error,@"The package does not contain exactly one expected macOS installer.");return nil;}
    NSURL *app=[directory URLByAppendingPathComponent:name];
    if(![fm moveItemAtURL:candidates.firstObject toURL:app error:error])return nil;
    NSURL *support=[app URLByAppendingPathComponent:@"Contents/SharedSupport" isDirectory:YES];
    if(![fm createDirectoryAtURL:support withIntermediateDirectories:YES attributes:nil error:error])return nil;
    // All package members we need now live in the app. This may itself contain
    // several GB of unused payload; never carry it through media conversion.
    if(![fm removeItemAtURL:expanded error:error])return nil;
    if(!legacy) {
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
    if(legacy) {
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
    NSString *payload=legacy?@"InstallESD.dmg":@"SharedSupport.dmg";
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
- (NSURL *)createLegacyMediaFromInstaller:(NSURL *)installer guest:(NSString *)guest jobDirectory:(NSURL *)directory error:(NSError **)error {
    if(NSThread.isMainThread){AssemblyError(error,@"Media preparation must run on a background worker.");return nil;}
    if(![self.class validateInstallerApplication:installer guest:guest error:error])return nil;
    NSString *name=@{@"mojave":@"Install macOS Mojave.app",@"catalina":@"Install macOS Catalina.app"}[guest];
    NSURL *base=[installer URLByAppendingPathComponent:@"Contents/SharedSupport/BaseSystem.dmg"];
    NSURL *esd=[installer URLByAppendingPathComponent:@"Contents/SharedSupport/InstallESD.dmg"];
    if(!name||![installer.pathExtension.lowercaseString isEqual:@"app"]||!directory.isFileURL||!RegularFile(base)||!RegularFile(esd)) {
        AssemblyError(error,@"Choose a complete Mojave or Catalina installer and a new media job directory.");return nil;
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
    NSArray *resize=modernImages?@[@"image",@"resize",@"--size",@"16g",working.path]:@[@"resize",@"-size",@"16g",working.path];
    [self completedWork:.45];[self nextWorkTo:.56 seconds:25];
    if(![self run:imageTool arguments:convert timeout:600 cleanup:NO error:error]){rmdir(mount.fileSystemRepresentation);return nil;}
    [self nextWorkTo:.60 seconds:8];
    if(![self run:imageTool arguments:resize timeout:180 cleanup:NO error:error]){rmdir(mount.fileSystemRepresentation);return nil;}
    [self nextWorkTo:.65 seconds:10];
    BOOL attached=[self run:@"/usr/bin/hdiutil" arguments:@[@"attach",@"-owners",@"off",@"-nobrowse",@"-noautoopen",@"-mountpoint",mount.path,working.path] timeout:180 cleanup:NO error:error];
    BOOL copied=NO;NSDictionary *version=nil;
    if(attached) {
        version=[NSDictionary dictionaryWithContentsOfURL:[mount URLByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]];
        NSString *expected=[guest isEqual:@"mojave"]?@"10.14.":@"10.15.";
        if([version[@"ProductVersion"] isKindOfClass:NSString.class]&&[version[@"ProductVersion"] hasPrefix:expected]&&
           RegularFile([mount URLByAppendingPathComponent:@"System/Library/CoreServices/boot.efi"]))
        {
            NSURL *stub=[mount URLByAppendingPathComponent:name];
            BOOL removed=![fm fileExistsAtPath:stub.path]||[fm removeItemAtURL:stub error:error];
            [self report:@"Adding the full macOS installer…"];
            [self completedWork:.65];
            [self nextWorkTo:.87 seconds:100];
            if(removed)copied=[self run:@"/usr/bin/ditto" arguments:@[@"--norsrc",@"--noextattr",@"--noacl",installer.path,stub.path] timeout:1800 cleanup:NO error:error];
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
    [self report:@"Creating the bootable installer image…"];
    [self completedWork:.9];
    [self nextWorkTo:.99 seconds:80];
    // Preserve BaseSystem's filesystem catalog, including root ownership,
    // hardlinks, ACLs and blessing. makehybrid on an ownership-disabled mount
    // stamps host UID 501 onto launchd jobs, which require system ownership.
    // The historical .iso name is a raw bootable disk image, not an ISO9660 FS.
    if(modernImages) {
        // diskutil created this working image with --format RAW already.
        // Resizing/mounting edited that same disk. QEMU can use it directly:
        // publish by a same-directory rename, not a second 16 GB conversion.
        if(![fm moveItemAtURL:working toURL:image error:error])return nil;
        [self completedWork:.99];
    } else {
        NSURL *converted=[directory URLByAppendingPathComponent:@"Installer-blocks.cdr"];
        if(![self run:@"/usr/bin/hdiutil" arguments:@[@"convert",working.path,@"-format",@"UDTO",@"-o",converted.path]
            timeout:1800 cleanup:NO error:error]||![fm moveItemAtURL:converted toURL:image error:error])return nil;
    }
    // The working copy is never reused; drop it as soon as the image exists.
    if(!modernImages)[fm removeItemAtURL:working error:nil];
    NSNumber *imageSize=[fm attributesOfItemAtPath:image.path error:error][NSFileSize];
    if(imageSize.longLongValue<=0){AssemblyError(error,@"The installer disk image is unavailable. Reconnect the library drive and try again.");return nil;}
    NSDictionary *receipt=@{@"FormatVersion":@1,@"Guest":guest,@"Stage":@"media-created",@"Media":image.lastPathComponent,@"Size":imageSize,@"ProductVersion":version[@"ProductVersion"],@"BootValidated":@NO,@"FilesystemMetadataPreserved":@YES};
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
        if(archives.count==1&&[asset[@"OSVersion"] isKindOfClass:NSString.class]&&[asset[@"OSVersion"] hasPrefix:@"11."]&&
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
    // Staged files are now inside the image and are never reused.
    if(![fm removeItemAtURL:stage error:error])return nil;
    NSNumber *imageSize=[fm attributesOfItemAtPath:image.path error:error][NSFileSize];
    if(imageSize.longLongValue<=0){AssemblyError(error,@"The installer disk image is unavailable. Reconnect the library drive and try again.");return nil;}
    NSDictionary *receipt=@{@"FormatVersion":@1,@"Guest":@"bigsur",@"Stage":@"media-created",@"Media":@"Installer.iso",
        @"Size":imageSize,@"ProductVersion":asset[@"OSVersion"],@"ProductBuildVersion":asset[@"Build"],@"BootValidated":@NO,@"SignedSharedSupportPackage":@YES};
    NSData *data=[NSPropertyListSerialization dataWithPropertyList:receipt format:NSPropertyListXMLFormat_v1_0 options:0 error:error];
    if(!data||![data writeToURL:[directory URLByAppendingPathComponent:@"Media.plist"] options:NSDataWritingAtomic error:error])return nil;
    [self completedWork:1];return image;
}
@end
