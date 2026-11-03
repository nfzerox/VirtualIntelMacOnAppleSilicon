// Cold setup/update evidence and requested native automatic-login policy. No timer,
// capture, guest credentials, socket listener or always-running user process.
#import <Foundation/Foundation.h>
#import "opengl-system-version.h"
#import <SystemConfiguration/SystemConfiguration.h>
#import <DiskArbitration/DiskArbitration.h>
#import <CommonCrypto/CommonDigest.h>
#import "guest-console-state.h"
#import "opengl-login-policy.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
#import "opengl-tiger-services.h"
#include "opengl-tiger-kernel-identity.h"
#else
#import "opengl-sharing.h"
#endif
#import "opengl-first-login.h"
#import "opengl-guest-machine.h"
#import "opengl-boot-identity.h"
#import "opengl-desktop-processes.h"
#include "opengl-state-path.h"
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <fcntl.h>
static int EnableFileSharing(void){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    return VIMTigerEnableNativeServices(YES,NO);
#else
    int result=VIMOpenGLConfigureFileSharing();if(result)return result;
    // Yosemite's native API verifies persisted launchd enablement itself;
    // it no longer uses Mavericks' launchd.db/overrides.plist location.
    if(VIMOpenGLPersistentJobSharing())return VIMOpenGLFileSharingConfigured()?0:75;
    if(VIMOpenGLLeopardSharing()){
        // Darwin 9 predates launchd.db's override store. Its native SFL
        // service persists enablement in the actual job configuration.
        NSDictionary *job=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/LaunchDaemons/smbd.plist"];
        id disabled=[job objectForKey:@"Disabled"];
        return job&&(!disabled||([disabled isKindOfClass:[NSNumber class]]&&![disabled boolValue]))?0:75;
    }
    NSDictionary *overrides=[NSDictionary dictionaryWithContentsOfFile:@"/private/var/db/launchd.db/com.apple.launchd/overrides.plist"];
    id disabled=[[overrides objectForKey:(NSString *)VIMOpenGLFileSharingJobLabel()] objectForKey:@"Disabled"];
    // Do not confuse an enabled current launchd job with persisted boot policy.
    return [disabled isKindOfClass:[NSNumber class]]&&![disabled boolValue]?0:75;
#endif
}
static NSString *const State=VIM_OPENGL_STATE_PATH;
static BOOL Owned(NSString *path){struct stat s;return !lstat([path fileSystemRepresentation],&s)&&S_ISREG(s.st_mode)&&s.st_uid==0&&s.st_nlink==1&&!(s.st_mode&0077);}
static NSString *SHA(NSData *data){CC_SHA256_CTX context;unsigned char hash[CC_SHA256_DIGEST_LENGTH];CC_SHA256_Init(&context);CC_SHA256_Update(&context,[data bytes],(CC_LONG)[data length]);CC_SHA256_Final(hash,&context);NSMutableString *s=[NSMutableString string];for(unsigned i=0;i<sizeof(hash);i++)[s appendFormat:@"%02x",hash[i]];return s;}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
static BOOL TigerInstalledFile(NSString *path){
    int fd=open([path fileSystemRepresentation],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return NO;
    struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||st.st_size<1||st.st_size>8*1024*1024){close(fd);return NO;}
    close(fd);return YES;
}
#endif
static NSDictionary *Record(NSString *leaf){NSString *path=[State stringByAppendingPathComponent:leaf];return Owned(path)?[NSDictionary dictionaryWithContentsOfFile:path]:nil;}
static NSString *RootUUID(void){
    DASessionRef session=DASessionCreate(NULL);if(!session)return nil;
    DADiskRef disk=DADiskCreateFromVolumePath(NULL,session,(CFURLRef)[NSURL fileURLWithPath:@"/"]);
    NSDictionary *info=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;CFUUIDRef uuid=(CFUUIDRef)[info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    NSString *value=uuid?[(NSString *)CFUUIDCreateString(NULL,uuid) autorelease]:nil;
    [info release];if(disk)CFRelease(disk);CFRelease(session);return value;
}
static BOOL Save(NSDictionary *value,NSString *leaf){
    NSString *path=[State stringByAppendingPathComponent:leaf];NSData *data=[NSPropertyListSerialization dataWithPropertyList:value format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    return data&&[data writeToFile:path options:NSDataWritingAtomic error:NULL]&&!chmod([path fileSystemRepresentation],0600);
}
int main(int argc,char **argv){@autoreleasepool{
    BOOL finish=argc==2&&!strcmp(argv[1],"--finish-first-login");if(argc>2||(argc==2&&!finish))return 64;
    NSDictionary *system=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *version=[system objectForKey:@"ProductVersion"],*build=[system objectForKey:@"ProductBuildVersion"];
    if(geteuid()!=0||!VIMOpenGLGuestForSystemVersion(version))return 77;
    if(!VIMOpenGLGuestMachine())return 77;
    NSDictionary *ownership=Record(@"Ownership.plist"),*graphics=Record(@"Graphics.plist"),*account=Record(@"Account.plist");
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    BOOL graphicsFiles=TigerInstalledFile(@"/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle/GLEngine")&&
        TigerInstalledFile(@"/System/Library/Frameworks/OpenGL.framework/Versions/A/Resources/GLEngine.bundle/libVIMPassthroughOpenGL.dylib")&&
        TigerInstalledFile(@"/System/Library/Extensions/VMsvga2GA.plugin/Contents/MacOS/VMsvga2GA")&&
        TigerInstalledFile(@"/System/Library/Extensions/VMsvga2GA.plugin/Contents/Info.plist");
#endif
    if(graphics&&[graphics objectForKey:@"InstalledBootSessionUUID"]){
        NSMutableDictionary *current=[[graphics mutableCopy] autorelease];
        [current setObject:[NSNumber numberWithBool:[[graphics objectForKey:@"ChangedThisBoot"] boolValue]&&
            [[graphics objectForKey:@"InstalledBootSessionUUID"] isEqual:VIMOpenGLBootSessionUUID(NULL)]] forKey:@"ChangedThisBoot"];
        graphics=current;
    }
    NSString *uuid=RootUUID();BOOL identity=uuid&&[[ownership objectForKey:@"VolumeUUID"] isEqual:uuid]&&
        [[graphics objectForKey:@"VolumeUUID"] isEqual:uuid]&&[[graphics objectForKey:@"MachineIdentifier"] isEqual:[ownership objectForKey:@"MachineIdentifier"]];
    // Installation authorization exists before graphics are published. A
    // missing graphics receipt must not be mistaken for a foreign volume.
    id identifier=[ownership objectForKey:@"MachineIdentifier"];
    CFUUIDRef machine=[identifier isKindOfClass:[NSString class]]?CFUUIDCreateFromString(NULL,(CFStringRef)identifier):NULL;
    BOOL installationIdentity=uuid&&[[ownership objectForKey:@"VolumeUUID"] isEqual:uuid]&&
        [[ownership objectForKey:@"Guest"] isEqual:VIMOpenGLGuestForSystemVersion(version)]&&machine;
    if(machine)CFRelease(machine);
    uid_t uid=0;gid_t gid=0;NSString *name=(NSString *)SCDynamicStoreCopyConsoleUser(NULL,&uid,&gid);
    BOOL desktop=VIMOpenGLDesktopRunning(uid);NSInteger locked=VIMConsoleLockState();
    NSString *lockPath=[State stringByAppendingPathComponent:@"Account.lock"];
    int lock=open([lockPath fileSystemRepresentation],O_RDWR|O_NOFOLLOW|O_CLOEXEC);
    BOOL accountBusy=lock>=0&&flock(lock,LOCK_EX|LOCK_NB)!=0;
    if(finish){
        if(!identity||!desktop||accountBusy||uid!=[[account objectForKey:@"UID"] unsignedIntValue]||
           ![[account objectForKey:@"Username"] isEqual:name]||![[account objectForKey:@"MachineIdentifier"] isEqual:[ownership objectForKey:@"MachineIdentifier"]]){if(lock>=0)close(lock);[name release];return 77;}
        NSString *autoUser=VIMOpenGLLoginUser();
        BOOL ours=[autoUser isEqual:name]&&Owned(@"/etc/kcpassword")&&
            [[account objectForKey:@"AutoLoginSHA256"] isEqual:SHA([NSData dataWithContentsOfFile:@"/etc/kcpassword"])];
        NSMutableDictionary *completed=[[account mutableCopy] autorelease];
        if([[completed objectForKey:@"FirstLoginCookieIntent"] boolValue]&&
           ![[completed objectForKey:@"FirstLoginPolicyRetired"] boolValue]){
            if(VIMOpenGLFirstLoginCookie(name,uid,2)<0){if(lock>=0)close(lock);[name release];return 1;}
            // Native MiniBuddy sets this temporary mode in its own defaults.
            // Reset it as the actual user, not root's CFPreferences domain.
            NSTask *reset=[[[NSTask alloc] init] autorelease];[reset setLaunchPath:@"/usr/bin/sudo"];
            [reset setArguments:[NSArray arrayWithObjects:@"-H",@"-u",name,@"/usr/bin/defaults",@"write",@"com.apple.SetupAssistant",@"RunNonInteractive",@"-bool",@"false",nil]];
            [reset setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
            [reset setStandardOutput:[NSFileHandle fileHandleWithNullDevice]];
            [reset setStandardError:[NSFileHandle fileHandleWithNullDevice]];
            @try{[reset launch];[reset waitUntilExit];if([reset terminationStatus]){if(lock>=0)close(lock);[name release];return 1;}}
            @catch(NSException *exception){if(lock>=0)close(lock);[name release];return 1;}
            NSDictionary *prefs=[NSDictionary dictionaryWithContentsOfFile:[[@"/Users/" stringByAppendingString:name]
                stringByAppendingPathComponent:@"Library/Preferences/com.apple.SetupAssistant.plist"]];
            if(!prefs||[[prefs objectForKey:@"RunNonInteractive"] boolValue]){if(lock>=0)close(lock);[name release];return 1;}
            [completed setObject:[NSNumber numberWithBool:YES] forKey:@"FirstLoginPolicyRetired"];
        }
        // Setup Assistant may save the initial sharing overrides after account
        // provisioning. Finalize once at the actual account desktop, not on
        // every later boot (which would undo the user's Sharing preferences).
        if([[completed objectForKey:@"DesktopServicesVersion"] intValue]<2){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
            if([[ownership objectForKey:@"FileSharing"] boolValue]&&
               (![[account objectForKey:@"FileSharingAccountPrepared"] boolValue]||!VIMTigerSMBUserReady(name,uid))){
                if(lock>=0)close(lock);[name release];return 77;
            }
#endif
            int sharing=[[ownership objectForKey:@"FileSharing"] boolValue]?EnableFileSharing():0;
            if(sharing){
                if(lock>=0)close(lock);[name release];return sharing;
            }
            [completed setObject:[NSNumber numberWithBool:YES] forKey:@"DesktopServicesConfigured"];
            [completed setObject:[NSNumber numberWithInt:2] forKey:@"DesktopServicesVersion"];
            if([[ownership objectForKey:@"FileSharing"] boolValue])
                [completed setObject:[NSNumber numberWithBool:YES] forKey:@"FileSharingConfigured"];
        }
        if(ours&&[[ownership objectForKey:@"AutomaticLogin"] boolValue]){
            // Retain only the credential this transaction already verified.
            // Never recreate it or override a later user change. This is the
            // native root-only loginwindow credential, not a polling service
            // or a retained copy of the host setup password.
            [completed setObject:[NSNumber numberWithBool:YES] forKey:@"AutomaticLoginConfigured"];
        }else if(ours){
            if(!VIMOpenGLSetLoginUser(nil)||unlink("/etc/kcpassword")){if(lock>=0)close(lock);[name release];return 1;}
            [completed setObject:[NSNumber numberWithBool:YES] forKey:@"TemporaryLoginCredentialRemoved"];
        }else if(![[account objectForKey:@"Phase"] isEqual:@"complete"]){
            // The user changed login policy themselves; do not undo it or
            // delete another credential. Record the non-secret distinction.
            [completed setObject:[NSNumber numberWithBool:YES] forKey:@"LoginPolicyChangedByUser"];
        }
        [completed setObject:@"complete" forKey:@"Phase"];
        if(!Save(completed,@"Account.plist")){if(lock>=0)close(lock);[name release];return 1;}account=completed;
    }
    if(lock>=0)close(lock);
    BOOL kernelMatches=NO;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    kernelMatches=VIMTigerRunningKernelMatchesFile();
#else
    char kernel[128]={0};size_t bytes=sizeof(kernel);sysctlbyname("kern.osversion",kernel,&bytes,NULL,0);
    kernelMatches=[build isEqual:[NSString stringWithUTF8String:kernel]];
#endif
    NSMutableDictionary *report=[NSMutableDictionary dictionaryWithObjectsAndKeys:
        [NSNumber numberWithInt:1],@"FormatVersion",version?:@"",@"ProductVersion",build?:@"",@"ProductBuildVersion",
        [NSNumber numberWithInt:1],@"GraphicsRestartEvidenceVersion",
        uuid?:@"",@"VolumeUUID",[ownership objectForKey:@"MachineIdentifier"]?:@"",@"MachineIdentifier",
        [NSNumber numberWithBool:identity],@"OwnershipVerified",[NSNumber numberWithBool:kernelMatches],@"KernelMatchesSystem",
        [NSNumber numberWithBool:desktop],@"DesktopRunning",[NSNumber numberWithUnsignedInt:uid],@"ConsoleUID",name?:@"",@"ConsoleUsername",
        [NSNumber numberWithBool:accountBusy],@"AccountBusy",graphics?:[NSDictionary dictionary],@"Graphics",account?:[NSDictionary dictionary],@"Account",nil];
    [report setObject:[NSNumber numberWithBool:installationIdentity] forKey:@"InstallationOwnershipVerified"];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    [report setObject:[NSNumber numberWithBool:graphicsFiles] forKey:@"GraphicsFilesVerified"];
#endif
    if(locked>=0)[report setObject:[NSNumber numberWithBool:locked!=0] forKey:@"ConsoleLocked"];
    NSString *bootSource=nil,*boot=VIMOpenGLBootSessionUUID(&bootSource);
    if([version hasPrefix:@"10.12."])
        [report setObject:[NSNumber numberWithInt:1] forKey:@"SierraLoginHandoffVersion"];
    if(identity&&!accountBusy&&!desktop&&VIMSierraLoginHandoffMissing(version,ownership,account,boot,uid))
        [report setObject:[NSNumber numberWithBool:YES] forKey:@"AutoLoginHandoffMissing"];
    if(boot){[report setObject:boot forKey:@"BootSessionUUID"];[report setObject:bootSource forKey:@"BootIdentitySource"];
        [report setObject:[NSNumber numberWithBool:VIMOpenGLControlIdleSleepGuard(boot)] forKey:@"IdleSleepGuardRegistered"];}
    NSData *json=[NSJSONSerialization dataWithJSONObject:report options:0 error:NULL];[name release];
    if(!json)return 1;fwrite([json bytes],1,[json length],stdout);fputc('\n',stdout);return 0;
}}
