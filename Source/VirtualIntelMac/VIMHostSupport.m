#import "VIMHostSupport.h"
#import "VIMAlerts.h"
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
NSString *VIMHostUpdateNotice(NSOperatingSystemVersion version) {
    if(version.majorVersion<28)return nil;
    NSString *number=[NSString stringWithFormat:@"%ld",version.majorVersion];
    if(version.minorVersion||version.patchVersion)number=[number stringByAppendingFormat:@".%ld",version.minorVersion];
    if(version.patchVersion)number=[number stringByAppendingFormat:@".%ld",version.patchVersion];
    return [NSString stringWithFormat:@"Virtual Intel Mac requires an update for macOS %@.",number];
}
static void CheckRosetta(BOOL required,void (^completion)(BOOL,NSError *)) {
#if !defined(__arm64__)
    int arm=0;size_t size=sizeof(arm);
    if(sysctlbyname("hw.optional.arm64",&arm,&size,NULL,0)||!arm){completion(NO,nil);return;}
#endif
    NSString *helper=[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Runtime/bin/apple-gfx-helper"];
    if(![NSFileManager.defaultManager isExecutableFileAtPath:helper]) {
        // A UI-only build can open its library, but must not start a VM on an
        // unverified host. Explicit source harnesses use their real helper.
        NSString *root=required?VIMDevelopmentSourceRoot():nil;
        helper=[root stringByAppendingPathComponent:@"artifacts/host-arm64/apple-gfx-helper-app"];
        if(!helper.length||![NSFileManager.defaultManager isExecutableFileAtPath:helper]) {
            NSError *error=required?[NSError errorWithDomain:@"VirtualIntelMac.Host" code:2 userInfo:@{NSLocalizedDescriptionKey:@"The Intel graphics helper is missing. Reinstall Virtual Intel Mac before starting a virtual Mac."}]:nil;
            completion(NO,error);return;
        }
    }
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
        if(code&&!missing)error=[NSError errorWithDomain:NSPOSIXErrorDomain code:code userInfo:@{NSLocalizedDescriptionKey:@"The Intel graphics helper could not be checked. Reinstall Virtual Intel Mac and try again."}];
        if(!code) {
            int status=0;pid_t result=0;double deadline=NSProcessInfo.processInfo.systemUptime+10;
            do {result=waitpid(child,&status,WNOHANG);if(result==0)usleep(20000);}
            while((result==0||(result<0&&errno==EINTR))&&NSProcessInfo.processInfo.systemUptime<deadline);
            if(result!=child||!WIFEXITED(status)||WEXITSTATUS(status)!=64) {
                if(result==0){kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}}
                error=[NSError errorWithDomain:@"VirtualIntelMac.Host" code:1 userInfo:@{NSLocalizedDescriptionKey:@"The Intel graphics helper could not finish its startup check. Reinstall Virtual Intel Mac or check for a macOS update."}];
            }
        }
        dispatch_async(dispatch_get_main_queue(),^{completion(missing,error);});
    });
}
void VIMCheckRosetta(void (^completion)(BOOL,NSError *)) {CheckRosetta(NO,completion);}
void VIMCheckRosettaForBoot(void (^completion)(BOOL,NSError *)) {CheckRosetta(YES,completion);}
void VIMPresentRosettaRequiredAlert(NSWindow *window) {
    NSCAssert(NSThread.isMainThread,@"Rosetta alerts belong to the main thread");
    static BOOL presenting=NO;
    if(presenting)return;
    presenting=YES;
    NSAlert *alert=[NSAlert new];alert.messageText=@"Install Rosetta to start your virtual Macs";
    alert.informativeText=@"Virtual Intel Mac needs Rosetta for Intel graphics support on this Apple silicon Mac. Install Rosetta using Apple’s installer, then try starting your virtual Mac again.";
    [alert addButtonWithTitle:@"Install Rosetta…"];[alert addButtonWithTitle:@"Not Now"];
    VIMPresentAlert(alert,window,^(NSModalResponse response){
        presenting=NO;
        if(response==NSAlertFirstButtonReturn)[NSWorkspace.sharedWorkspace openApplicationAtURL:
            [NSURL fileURLWithPath:@"/System/Library/CoreServices/Rosetta 2 Updater.app"]
            configuration:NSWorkspaceOpenConfiguration.configuration completionHandler:^(NSRunningApplication *app,NSError *failure){
                if(failure)dispatch_async(dispatch_get_main_queue(),^{VIMPresentError(failure,window);});
            }];
    });
}
