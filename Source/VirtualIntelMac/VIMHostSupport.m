#import "VIMLocalization.h"
#import "VIMHostSupport.h"
#import "VIMAlerts.h"
#import "VIMGuestFamilies.h"
#include <signal.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <mach/machine.h>
#include <fcntl.h>
#include <errno.h>
@interface VIMInstallationActivity ()
@property id token;
@end
@implementation VIMInstallationActivity
+ (instancetype)beginWithReason:(NSString *)reason {
    VIMInstallationActivity *activity=[self new];
    activity.token=[NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated reason:reason];
    return activity;
}
- (void)dealloc {if(_token)[NSProcessInfo.processInfo endActivity:_token];}
@end
NSString *VIMDevelopmentSourceRoot(void) {
    NSString *runtime=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime"];
    // Debug download preferences are shared across builds. They must never
    // redirect a self-contained app to stale or restricted developer binaries.
    if([NSFileManager.defaultManager fileExistsAtPath:runtime])return nil;
    NSUserDefaults *defaults=NSUserDefaults.standardUserDefaults;
    return [defaults boolForKey:@"VIMDebugMode"]?[defaults stringForKey:@"VIMDevelopmentSourceRoot"]:nil;
}
BOOL VIMGuestRequiresPVGHostSupport(NSString *guest) {
    return VIMIsModernGuest(guest);
}
BOOL VIMHostInstallerRequiresRosetta(NSString *guest,BOOL userSelectedInstaller) {
    // These routes build a startup cache and/or convert to period APFS even
    // with selected media. El Capitan/Sierra only rebuild after catalog updates.
    if([@[@"mavericks",@"yosemite",@"highsierra",@"mojave",@"catalina"] containsObject:guest])return YES;
    return !userSelectedInstaller&&[@[@"elcapitan",@"sierra"] containsObject:guest];
}
NSString *VIMHostUpdateNoticeForGuest(NSOperatingSystemVersion version,NSString *guest) {
    if(version.majorVersion<28||!VIMGuestRequiresPVGHostSupport(guest))return nil;
    NSString *number=[NSString stringWithFormat:@"%ld",version.majorVersion];
    if(version.minorVersion||version.patchVersion)number=[number stringByAppendingFormat:@".%ld",version.minorVersion];
    if(version.patchVersion)number=[number stringByAppendingFormat:@".%ld",version.patchVersion];
    return [NSString stringWithFormat:VIMLocalized(@"hostsupport.virtual-intel-mac-requires-an-update-for-macos", @"Virtual Intel Mac requires an update for macOS %@."),number];
}
static BOOL AppleSiliconHost(void) {
#if defined(__arm64__)
    return YES;
#else
    int arm=0;size_t size=sizeof(arm);
    return !sysctlbyname("hw.optional.arm64",&arm,&size,NULL,0)&&arm;
#endif
}
static void CheckIntelTool(NSString *helper,int expected,NSString *failureMessage,void (^completion)(BOOL,NSError *)) {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        posix_spawnattr_t attr;posix_spawn_file_actions_t files;
        posix_spawnattr_init(&attr);posix_spawn_file_actions_init(&files);
        cpu_type_t architecture=CPU_TYPE_X86_64;size_t accepted=0;
        int code=posix_spawnattr_setbinpref_np(&attr,1,&architecture,&accepted);
        if(!code)code=posix_spawnattr_setflags(&attr,POSIX_SPAWN_CLOEXEC_DEFAULT);
        if(!code)code=posix_spawn_file_actions_addopen(&files,STDIN_FILENO,"/dev/null",O_RDONLY,0);
        if(!code)code=posix_spawn_file_actions_addopen(&files,STDOUT_FILENO,"/dev/null",O_WRONLY,0);
        if(!code)code=posix_spawn_file_actions_addopen(&files,STDERR_FILENO,"/dev/null",O_WRONLY,0);
        char *arguments[]={(char *)helper.fileSystemRepresentation,NULL};
        char *environment[]={"PATH=/usr/bin:/bin",NULL};pid_t child=0;
        if(!code)code=posix_spawn(&child,helper.fileSystemRepresentation,&files,&attr,arguments,environment);
        posix_spawn_file_actions_destroy(&files);posix_spawnattr_destroy(&attr);
        BOOL missing=code==EBADARCH;NSError *error=nil;
        if(code&&!missing)error=[NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:@{NSLocalizedDescriptionKey:failureMessage}];
        if(!code) {
            int status=0;pid_t result=0;double deadline=NSProcessInfo.processInfo.systemUptime+10;
            do {result=waitpid(child,&status,WNOHANG);if(result==0)usleep(20000);}
            while((result==0||(result<0&&errno==EINTR))&&NSProcessInfo.processInfo.systemUptime<deadline);
            if(result!=child||!WIFEXITED(status)||WEXITSTATUS(status)!=expected) {
                if(result==0){kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}}
                error=[NSError errorWithDomain:@"VirtualIntelMac.Host" code:1 userInfo:@{NSLocalizedDescriptionKey:failureMessage}];
            }
        }
        dispatch_async(dispatch_get_main_queue(),^{completion(missing,error);});
    });
}
void VIMCheckRosettaForPVG(void (^completion)(BOOL,NSError *)) {
    if(!AppleSiliconHost()){completion(NO,nil);return;}
    NSString *helper=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/bin/apple-gfx-helper"];
    if(![NSFileManager.defaultManager isExecutableFileAtPath:helper]) {
        helper=[VIMDevelopmentSourceRoot() stringByAppendingPathComponent:@"artifacts/host-arm64/apple-gfx-helper-app"];
        if(!helper.length||![NSFileManager.defaultManager isExecutableFileAtPath:helper]) {
            NSError *error=[NSError errorWithDomain:@"VirtualIntelMac.Host" code:2 userInfo:@{NSLocalizedDescriptionKey:VIMLocalized(@"vimhostsupport.the-intel-graphics-helper-is-missing-reinstall-virtual-intel", @"The Intel graphics helper is missing. Reinstall Virtual Intel Mac and try again.")}];
            completion(NO,error);return;
        }
    }
    CheckIntelTool(helper,64,VIMLocalized(@"hostsupport.the-intel-graphics-helper-could-not-finish-its-startup-check", @"The Intel graphics helper could not finish its startup check. Reinstall Virtual Intel Mac or check for a macOS update."),completion);
}
void VIMCheckRosettaForHostInstaller(void (^completion)(BOOL,NSError *)) {
    if(!AppleSiliconHost()){completion(NO,nil);return;}
    // Check translation without loading any graphics framework or installing
    // anything. Booting an already prepared OpenGL guest needs neither.
    CheckIntelTool(@"/usr/bin/true",0,VIMLocalized(@"vimhostsupport.could-not-check-rosetta-for-installer-preparation-try-again", @"Could not check Rosetta for installer preparation. Try again."),completion);
}
void VIMPresentHostUpdateRequiredAlert(NSString *notice,NSWindow *window) {
    NSAlert *alert=[NSAlert new];alert.messageText=notice;
    alert.informativeText=VIMLocalized(@"app.check-the-virtual-intel-mac-releases-page-for-a-version", @"Check the Virtual Intel Mac releases page for a version compatible with this Mac.");
    [alert addButtonWithTitle:VIMLocalized(@"app.check-for-updates", @"Check for Updates")];[alert addButtonWithTitle:VIMLocalized(@"app.not-now", @"Not Now")];
    VIMPresentAlert(alert,window,^(NSModalResponse response){
        if(response==NSAlertFirstButtonReturn)[NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@"https://github.com/nfzerox/VirtualIntelMacOnAppleSilicon/releases"]];
    });
}
static void PresentRosettaRequiredAlert(NSWindow *window,NSString *title,NSString *detail) {
    NSCAssert(NSThread.isMainThread,@"Rosetta alerts belong to the main thread");
    static BOOL presenting=NO;
    if(presenting)return;
    presenting=YES;
    NSAlert *alert=[NSAlert new];alert.messageText=title;alert.informativeText=detail;
    [alert addButtonWithTitle:VIMLocalized(@"hostsupport.install-rosetta", @"Install Rosetta…")];[alert addButtonWithTitle:VIMLocalized(@"app.not-now", @"Not Now")];
    VIMPresentAlert(alert,window,^(NSModalResponse response){
        presenting=NO;
        if(response!=NSAlertFirstButtonReturn)return;
        // Launch Services owns Apple's normal Rosetta prompt, authentication
        // and license UI. No private updater path or silent CLI agreement.
        NSURL *installer=[NSBundle.mainBundle.bundleURL URLByAppendingPathComponent:@"Contents/Helpers/Rosetta Installer.app"];
        if(![NSFileManager.defaultManager fileExistsAtPath:installer.path]) {
            VIMPresentError([NSError errorWithDomain:@"VirtualIntelMac.Host" code:3 userInfo:@{
                NSLocalizedDescriptionKey:VIMLocalized(@"vimhostsupport.the-rosetta-installer-is-missing-reinstall-virtual-intel-mac", @"The Rosetta installer is missing. Reinstall Virtual Intel Mac, then try again.")}],window);
            return;
        }
        // The Intel trigger can report a launch failure while macOS is
        // correctly presenting its Rosetta installer. Stay on this page;
        // Continue/Start checks translation again after the user installs.
        [NSWorkspace.sharedWorkspace openApplicationAtURL:installer
            configuration:NSWorkspaceOpenConfiguration.configuration completionHandler:nil];
    });
}
void VIMPresentRosettaRequiredAlert(NSWindow *window,BOOL creating) {
    PresentRosettaRequiredAlert(window,creating?VIMLocalized(@"vimhostsupport.install-rosetta-to-create-this-virtual-mac", @"Install Rosetta to create this virtual Mac"):VIMLocalized(@"vimhostsupport.install-rosetta-to-start-this-virtual-mac", @"Install Rosetta to start this virtual Mac"),
        VIMLocalized(@"vimhostsupport.this-virtual-mac-needs-rosetta-for-graphics-acceleration-install", @"This virtual Mac needs Rosetta for graphics acceleration. Install Rosetta, then try again."));
}
void VIMPresentHostInstallerRosettaAlert(NSWindow *window) {
    PresentRosettaRequiredAlert(window,VIMLocalized(@"vimhostsupport.install-rosetta-to-prepare-this-virtual-mac", @"Install Rosetta to prepare this virtual Mac"),
        VIMLocalized(@"vimhostsupport.preparing-this-virtual-mac-uses-an-intel-tool-from", @"Preparing this virtual Mac uses an Intel tool from Apple. Install Rosetta, then try again."));
}
