// Modern Recovery entry point. Delegate installation to Apple's shipped CLI,
// not GUI clicks, injected code, a package reimplementation or a host installer.
#import <Foundation/Foundation.h>
#import "guest-system-version.h"
#import "opengl-guest-machine.h"
#import "modern-setup-identity.h"
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <errno.h>

static void Require(BOOL valid,NSString *message) {
    if(!valid){fprintf(stderr,"%s\n",message.UTF8String);exit(65);}
}
static BOOL UUID(id value) {
    return [value isKindOfClass:NSString.class]&&[[NSUUID alloc] initWithUUIDString:value]!=nil;
}
static NSData *Run(NSString *executable,NSArray *arguments) {
    NSTask *task=[NSTask new];NSPipe *pipe=NSPipe.pipe;
    task.executableURL=[NSURL fileURLWithPath:executable];task.arguments=arguments;
    task.standardInput=NSFileHandle.fileHandleWithNullDevice;
    task.standardOutput=pipe;task.standardError=NSFileHandle.fileHandleWithNullDevice;
    NSError *error=nil;if(![task launchAndReturnError:&error])return nil;
    NSData *data=[pipe.fileHandleForReading readDataToEndOfFile];[task waitUntilExit];
    return task.terminationStatus==0&&data.length<1024*1024?data:nil;
}
static NSDictionary *DiskInfo(NSString *identifier) {
    NSData *data=Run(@"/usr/sbin/diskutil",@[@"info",@"-plist",identifier]);
    id result=data?[NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL]:nil;
    return [result isKindOfClass:NSDictionary.class]?result:nil;
}
static NSDictionary *Request(void) {
    NSMutableData *data=[NSMutableData new];unsigned char bytes[1024];ssize_t count;
    while((count=read(STDIN_FILENO,bytes,sizeof(bytes)))!=0){
        if(count<0&&errno==EINTR)continue;
        Require(count>0&&data.length+(NSUInteger)count<=4096,@"Invalid native installation request.");
        [data appendBytes:bytes length:(NSUInteger)count];
    }
    id value=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    Require([value isKindOfClass:NSDictionary.class]&&[value[@"FormatVersion"] isEqual:@1]&&
        UUID(value[@"MachineIdentifier"])&&UUID(value[@"VolumeUUID"])&&
        [@[@"mojave",@"catalina",@"bigsur"] containsObject:value[@"Guest"]?:@""],
        @"The new virtual Mac or its installation target could not be identified.");
    return value;
}
static NSString *Installer(NSString *guest) {
    NSString *name=@{@"mojave":@"Install macOS Mojave.app",@"catalina":@"Install macOS Catalina.app",
        @"bigsur":@"Install macOS Big Sur.app"}[guest];
    NSString *identifier=[@"com.apple.InstallAssistant." stringByAppendingString:
        [guest isEqual:@"bigsur"]?@"macOSBigSur":([guest isEqual:@"mojave"]?@"Mojave":@"Catalina")];
    // Big Sur Recovery carries its own matching InstallAssistant at /. Older
    // recovery layouts can use the original app on the attached install media.
    NSMutableArray *roots=[NSMutableArray arrayWithObjects:@"/",@"/Applications",nil];
    for(NSString *volume in [NSFileManager.defaultManager contentsOfDirectoryAtPath:@"/Volumes" error:NULL])
        [roots addObject:[@"/Volumes" stringByAppendingPathComponent:volume]];
    for(NSString *root in roots){
        NSString *app=[root stringByAppendingPathComponent:name];
        NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[app stringByAppendingPathComponent:@"Contents/Info.plist"]];
        NSString *tool=[app stringByAppendingPathComponent:@"Contents/Resources/startosinstall"];
        if([info[@"CFBundleIdentifier"] isEqual:identifier]&&[NSFileManager.defaultManager isExecutableFileAtPath:tool])return tool;
    }
    return nil;
}
int main(int argc,const char **argv){@autoreleasepool{
    BOOL start=argc==2&&!strcmp(argv[1],"--start");
    if(argc!=2||(!start&&strcmp(argv[1],"--plan")))return 64;
    Require(geteuid()==0&&VIMModernSetupGuestMachine(),@"Run native installation only in this virtual Mac’s privileged guest service.");
    NSDictionary *request=Request();NSString *guest=request[@"Guest"];
    NSDictionary *system=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    Require([VIMGuestForSystemVersion(system[@"ProductVersion"]) isEqual:guest],@"The recovery system does not match the requested OS.");
    NSFileManager *fm=NSFileManager.defaultManager;
    BOOL recovery=[fm fileExistsAtPath:@"/System/Installation/CDIS/Recovery Springboard.app"]||
        [fm fileExistsAtPath:@"/System/Installation/CDIS/macOS Utilities.app"];
    Require(recovery&&![fm fileExistsAtPath:@"/System/Library/CoreServices/Finder.app"],@"Native installation requires Recovery, not an installed desktop.");
    NSString *installer=Installer(guest);
    Require(installer!=nil,@"This recovery system has no matching native installation tool.");
    NSDictionary *target=DiskInfo(request[@"VolumeUUID"]);
    Require([target[@"VolumeUUID"] isEqual:request[@"VolumeUUID"]],@"The new installation disk is not available.");
    if(start&&![target[@"MountPoint"] length]){
        Require(Run(@"/usr/sbin/diskutil",@[@"mount",request[@"VolumeUUID"]])!=nil,@"The new installation disk could not be mounted.");
        target=DiskInfo(request[@"VolumeUUID"]);
    }
    NSString *mount=target[@"MountPoint"];
    Require([target[@"VolumeUUID"] isEqual:request[@"VolumeUUID"]]&&[mount hasPrefix:@"/Volumes/"]&&
        [mount.pathComponents count]==3&&[target[@"WritableVolume"] isEqual:@YES],@"The writable installation target could not be confirmed.");
    for(NSString *path in @[@"System/Library/CoreServices/SystemVersion.plist",@"macOS Install Data",@"OS X Install Data"])
        Require(![fm fileExistsAtPath:[mount stringByAppendingPathComponent:path]],@"Installation has already started on this disk; it was not repeated.");
    NSArray *arguments=@[@"--volume",mount,@"--agreetolicense",@"--nointeraction",@"--rebootdelay",@"0"];
    NSDictionary *plan=@{@"FormatVersion":@1,@"Guest":guest,@"MachineIdentifier":request[@"MachineIdentifier"],
        @"VolumeUUID":request[@"VolumeUUID"],@"MountPoint":mount,@"Executable":installer,@"Arguments":arguments,
        @"TargetDiskUUID":target[@"DiskUUID"]?:@"",@"EraseRequested":@NO,@"InstallationStarted":@NO};
    if(!start){NSData *json=[NSJSONSerialization dataWithJSONObject:plan options:0 error:NULL];fwrite(json.bytes,1,json.length,stdout);puts("");return 0;}
    // No elapsed-time deadline or retry. A real failure exits; successful
    // preparation may reboot the guest and close the caller's control channel.
    // The host must observe that boot instead of replaying an uncertain start.
    Require([request[@"LicenseAccepted"] isEqual:@YES],@"Accept the OS software license before starting automatic installation.");
    NSString *name=request[@"Username"];
    NSRegularExpression *names=[NSRegularExpression regularExpressionWithPattern:@"^[a-z][a-z0-9_-]{0,30}$" options:0 error:NULL];
    Require([name isKindOfClass:NSString.class]&&[names numberOfMatchesInString:name options:0 range:NSMakeRange(0,name.length)]==1&&
        ![@[@"root",@"daemon",@"nobody",@"guest",@"wheel"] containsObject:name]&&UUID(target[@"DiskUUID"]),@"The automatic account policy or target partition is invalid.");
    NSMutableDictionary *owner=[@{@"FormatVersion":@1,@"Enabled":@YES,@"Guest":guest,
        @"MachineIdentifier":request[@"MachineIdentifier"],@"VolumeUUID":request[@"VolumeUUID"],
        @"TargetDiskUUID":target[@"DiskUUID"],@"Username":name} mutableCopy];
    for(NSString *key in @[@"FileSharing",@"RemoteLogin",@"AutomaticLogin"]){
        Require([request[key] isEqual:@YES]||[request[key] isEqual:@NO],@"The automatic service policy is invalid.");owner[key]=request[key];
    }
    NSString *ownership=[mount stringByAppendingPathComponent:[VIMModernInstallerOwnership substringFromIndex:1]];
    NSDictionary *prior=[NSDictionary dictionaryWithContentsOfFile:ownership];
    Require(![fm fileExistsAtPath:ownership]||[prior isEqual:owner],@"Another setup already owns this target.");
    // Like High Sierra, keep pre-install state in Library. Manufacturing
    // /private/var on a new disk makes Apple's migration preflight reject it.
    NSString *directory=ownership.stringByDeletingLastPathComponent;
    Require([fm createDirectoryAtPath:directory withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0755} error:NULL]&&
        chmod(directory.fileSystemRepresentation,0700)==0&&
        [owner writeToFile:ownership atomically:YES]&&chmod(ownership.fileSystemRepresentation,0600)==0,
        @"The installed system's account handoff could not be prepared.");
    NSMutableArray *values=[NSMutableArray arrayWithObject:installer];[values addObjectsFromArray:arguments];
    char **native=calloc(values.count+1,sizeof(char *));Require(native!=NULL,@"The native installer could not be started.");
    for(NSUInteger i=0;i<values.count;i++)native[i]=(char *)[values[i] UTF8String];
    execv(installer.fileSystemRepresentation,native);perror("Native installer launch failed");free(native);return 71;
}}
