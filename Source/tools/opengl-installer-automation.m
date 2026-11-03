/* The genuine 13F34 minstall parser selects/checks the disk but its native
 * PreInstall pane does not advance itself. Observe actual pane readiness and
 * invoke its actual Continue button action once. No timer, title, pixel, index,
 * guessed address, private account credential or installer-check bypass. */
#import <AppKit/AppKit.h>
#import "opengl-system-version.h"
#import <objc/runtime.h>
#import <objc/message.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include "opengl-installer-paths.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#import <DiskArbitration/DiskArbitration.h>
#endif
static NSDictionary *authorization;
static char Active,Queued,Performed;
static IMP originalUpdate,originalEnter;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
static IMP originalSuccessEnter,originalSuccessFinish;
#endif
// Leopard has neither associated-object APIs nor libdispatch/Blocks. Keep
// state on these installer panes only; never add an AppKit-wide side table.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
static NSMutableArray *paneStates;
static NSMutableDictionary *PaneState(id pane,BOOL create){
    for(NSDictionary *entry in paneStates)
        if([entry objectForKey:@"Pane"]==pane)return [entry objectForKey:@"Values"];
    if(!create||!pane||[paneStates count]>=32)return nil;
    if(!paneStates)paneStates=[NSMutableArray new];
    NSMutableDictionary *state=[NSMutableDictionary dictionary];
    [paneStates addObject:[NSDictionary dictionaryWithObjectsAndKeys:pane,@"Pane",state,@"Values",nil]];
    return state;
}
static id PaneFlag(id pane,const void *key){return [PaneState(pane,NO) objectForKey:[NSValue valueWithPointer:key]];}
static void SetPaneFlag(id pane,const void *key,id value){
    NSMutableDictionary *state=PaneState(pane,value!=nil);NSValue *name=[NSValue valueWithPointer:key];
    if(value)[state setObject:value forKey:name];else [state removeObjectForKey:name];
}
@interface VIMInstallerContinuation : NSObject
+ (void)advance:(id)pane;
+ (void)bundleLoaded:(NSNotification *)notification;
@end
#else
static id PaneFlag(id pane,void *key){return objc_getAssociatedObject(pane,key);}
static void SetPaneFlag(id pane,void *key,id value){objc_setAssociatedObject(pane,key,value,OBJC_ASSOCIATION_RETAIN_NONATOMIC);}
#endif
// At most 32 short state transitions for this one installer process. No
// credentials, object descriptions, account names or per-frame/poll logging.
static void Record(const char *stage){
    static const char *last;static unsigned count;
    if(!authorization||count>=32||(last&&!strcmp(last,stage)))return;
    last=stage;count++;
    int fd=open("/var/tmp/VirtualIntelMac-InstallerContinuation.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW,0600);
    struct stat st;
    if(fd>=0){if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0077)&&st.st_nlink==1)
        (void)write(fd,stage,strlen(stage));(void)write(fd,"\n",1);close(fd);}
}
static id Send(id object,NSString *selector){SEL method=NSSelectorFromString(selector);return [object respondsToSelector:method]?((id(*)(id,SEL))objc_msgSend)(object,method):nil;}
static BOOL SendBool(id object,NSString *selector){SEL method=NSSelectorFromString(selector);return [object respondsToSelector:method]&&((BOOL(*)(id,SEL))objc_msgSend)(object,method);}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
static NSButton *ContinueButton(id controller){
    // Assistant.framework's actual AFAssistantController owns this outlet.
    // autopilotNextButton is a VOID action scheduling continue:, not a getter.
    // Resolve the named, typed ivar through runtime metadata; no fixed offset.
    Ivar outlet=class_getInstanceVariable([controller class],"continueButton");
    const char *type=outlet?ivar_getTypeEncoding(outlet):NULL;
    if(!type||strcmp(type,"@\"NSButton\""))return nil;
    id button=object_getIvar(controller,outlet);
    return [button isKindOfClass:[NSButton class]]&&[button target]==controller&&
        [button action]==NSSelectorFromString(@"continue:")?button:nil;
}
static NSString *TargetUUID(NSString *path){
    if(![path isKindOfClass:[NSString class]]||![path hasPrefix:@"/Volumes/"])return nil;
    DASessionRef session=DASessionCreate(NULL);if(!session)return nil;
    DADiskRef disk=DADiskCreateFromVolumePath(NULL,session,(CFURLRef)[NSURL fileURLWithPath:path]);
    NSDictionary *info=disk?(NSDictionary *)DADiskCopyDescription(disk):nil;
    CFTypeRef uuid=(CFTypeRef)[info objectForKey:(NSString *)kDADiskDescriptionVolumeUUIDKey];
    NSString *value=uuid&&CFGetTypeID(uuid)==CFUUIDGetTypeID()?[(NSString *)CFUUIDCreateString(NULL,(CFUUIDRef)uuid) autorelease]:nil;
    [info release];if(disk)CFRelease(disk);CFRelease(session);return value;
}
static void SelectInstalledStartup(id pane);
static void SuccessEntered(id pane,SEL selector,unsigned direction){
    ((void(*)(id,SEL,unsigned))originalSuccessEnter)(pane,selector,direction);
    SelectInstalledStartup(pane);
    // 9J3050's own countdown advances only while this window is key. Keep
    // the real success pane active; do not substitute a timer or reboot on
    // the mere presence of SystemVersion.plist during package installation.
    id controller=Send(Send(pane,@"section"),@"controller");
    NSWindow *window=[ContinueButton(controller) window];
    if(window){[NSApp activateIgnoringOtherApps:YES];[window makeKeyAndOrderFront:nil];}
}
static void SelectInstalledStartup(id pane){
    static char Selected;
    if([PaneFlag(pane,&Selected) boolValue])return;
    NSDictionary *session=Send(Send(Send(pane,@"section"),@"controller"),@"sessionData");
    NSString *root=Send(Send([session objectForKey:@"Install Controller"],@"target"),@"volumePath");
    // This method belongs to the actual success pane, not the common error
    // pane. Apple's native completed transaction and our original target UUID
    // must agree before changing a startup disk. Retain the installer media.
    if(![session objectForKey:@"Installation Error"]&&
       [[TargetUUID(root) uppercaseString] isEqual:[[authorization objectForKey:@"VolumeUUID"] uppercaseString]]&&
       [[[NSDictionary dictionaryWithContentsOfFile:[root stringByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]]
          objectForKey:@"ProductVersion"] hasPrefix:@"10.5."]&&
       [[NSFileManager defaultManager] fileExistsAtPath:[root stringByAppendingPathComponent:@"System/Library/CoreServices/boot.efi"]]){
        NSTask *task=[[[NSTask alloc] init] autorelease];
        [task setLaunchPath:@"/usr/sbin/bless"];
        [task setArguments:[NSArray arrayWithObjects:@"--mount",root,@"--setBoot",nil]];
        [task setStandardInput:[NSFileHandle fileHandleWithNullDevice]];
        [task setStandardOutput:[NSFileHandle fileHandleWithNullDevice]];
        [task setStandardError:[NSFileHandle fileHandleWithNullDevice]];
        @try{[task launch];[task waitUntilExit];
            if([task terminationStatus])Record("installed-startup-disk-failed");
            else {SetPaneFlag(pane,&Selected,[NSNumber numberWithBool:YES]);Record("installed-startup-disk-selected");}}
        @catch(NSException *exception){Record("installed-startup-disk-failed");}
    }else Record("installed-startup-disk-not-authorized");
}
static void SuccessFinished(id pane,SEL selector){
    SelectInstalledStartup(pane);
    ((void(*)(id,SEL))originalSuccessFinish)(pane,selector);
}
static void InstallSuccessHooks(void){
    static BOOL installed;Class pane=NSClassFromString(@"OSIPostInstallSuccessPanel");
    if(installed||!pane)return;
    Method enter=class_getInstanceMethod(pane,NSSelectorFromString(@"didEnterPaneDispatchee:"));
    Method finish=class_getInstanceMethod(pane,NSSelectorFromString(@"willFinishAssistant"));
    // Verified in the DVD's Objective-C metadata: void(unsigned) and void().
    if(!enter||!finish||strcmp(method_getTypeEncoding(enter),"v12@0:4I8")||
       strcmp(method_getTypeEncoding(finish),"v8@0:4"))return;
    originalSuccessEnter=method_getImplementation(enter);originalSuccessFinish=method_getImplementation(finish);
    method_setImplementation(enter,(IMP)SuccessEntered);method_setImplementation(finish,(IMP)SuccessFinished);
    installed=YES;Record("native-success-hooks-installed");
}
#endif
static void Advance(id pane){@autoreleasepool{
        SetPaneFlag(pane,&Queued,nil);
        id controller=Send(Send(pane,@"section"),@"controller");
        // These are the actual runtime strings, not Ghidra's sanitized
        // cf_AutomationSpecifier / cf_InstallController symbol labels.
        NSDictionary *session=Send(controller,@"sessionData");id specifier=[session objectForKey:@"Automation Specifier"];
        if(!specifier||[session objectForKey:@"Installation Error"]||!SendBool(specifier,@"installIsFullyAutomated")||
           !SendBool(controller,@"automatedChecksWerePerformed")){Record("waiting-for-native-automated-checks");return;}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        NSButton *button=ContinueButton(controller);
#else
        NSButton *button=Send(controller,@"continueButton");
#endif
        if(![button isKindOfClass:[NSButton class]]||![button isEnabled]||![[button window] isVisible]||[[button window] attachedSheet]){Record("waiting-for-native-continue-button");return;}
        id install=[session objectForKey:@"Install Controller"],target=Send(install,@"target");
        NSString *path=Send(target,@"volumePath");
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
        NSString *uuid=TargetUUID(path);
#else
        NSString *uuid=Send(Send(target,@"volume"),@"UUID");
#endif
        // Same session-data contract used by Apple's pane itself. Target
        // selection/checking still belongs to Apple; no object/struct offsets.
        if(![uuid isKindOfClass:[NSString class]]||![uuid isEqual:[authorization objectForKey:@"VolumeUUID"]]||
           ![path isKindOfClass:[NSString class]]||![path hasPrefix:@"/Volumes/"]||
           [[NSFileManager defaultManager] fileExistsAtPath:[path stringByAppendingPathComponent:@"System/Library/CoreServices/SystemVersion.plist"]]){Record("target-authorization-not-matched");return;}
        SetPaneFlag(pane,&Performed,[NSNumber numberWithBool:YES]);
        [NSApp activateIgnoringOtherApps:YES];[[button window] makeKeyAndOrderFront:nil];
        Record("invoking-native-continue");
        [button performClick:nil]; // Same AppKit target/action and native checks as a user.
}}
static void AdvanceIfReady(id pane){
    if(![PaneFlag(pane,&Active) boolValue]||[PaneFlag(pane,&Performed) boolValue]||
       [PaneFlag(pane,&Queued) boolValue])return;
    SetPaneFlag(pane,&Queued,[NSNumber numberWithBool:YES]);
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    [VIMInstallerContinuation performSelectorOnMainThread:@selector(advance:) withObject:pane waitUntilDone:NO];
#else
    dispatch_async(dispatch_get_main_queue(),^{Advance(pane);});
#endif
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
static void Updated(id self,SEL selector,id notification){
    ((void(*)(id,SEL,id))originalUpdate)(self,selector,notification);AdvanceIfReady(self);
}
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
static void UpdatedNoArgument(id self,SEL selector){
    ((void(*)(id,SEL))originalUpdate)(self,selector);AdvanceIfReady(self);
}
#endif
static void Entered(id self,SEL selector,unsigned direction){
    ((void(*)(id,SEL,unsigned))originalEnter)(self,selector,direction);
    SetPaneFlag(self,&Active,[NSNumber numberWithBool:YES]);AdvanceIfReady(self);
}
static void InstallHooks(void){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    InstallSuccessHooks();
#endif
    static BOOL installed;
    Class pane=NSClassFromString(@"OSIPreInstallPanel");if(installed||!pane)return;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    // Actual OSXInstallAssistant-119: no notification argument. The button
    // is an inherited typed outlet, not OSIDocumentController's void action.
    Method update=class_getInstanceMethod(pane,NSSelectorFromString(@"_updateUI"));
    const unsigned updateArguments=2;
#else
    Method update=class_getInstanceMethod(pane,NSSelectorFromString(@"_updateUI:"));
    const unsigned updateArguments=3;NSString *nextButton=@"continueButton";
#endif
    Method enter=class_getInstanceMethod(pane,NSSelectorFromString(@"didEnterPane:"));
    Class controller=NSClassFromString(@"OSIDocumentController");
    if(!update||!enter||method_getNumberOfArguments(update)!=updateArguments||method_getNumberOfArguments(enter)!=3||
       ![controller instancesRespondToSelector:NSSelectorFromString(@"automatedChecksWerePerformed")])return;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    Ivar outlet=class_getInstanceVariable(controller,"continueButton");
    if(!outlet||strcmp(ivar_getTypeEncoding(outlet),"@\"NSButton\""))return;
#else
    if(![controller instancesRespondToSelector:NSSelectorFromString(nextButton)])return;
#endif
    originalUpdate=method_getImplementation(update);originalEnter=method_getImplementation(enter);
    // Both selectors belong to this concrete Apple pane, not a global AppKit
    // superclass. No methods in another process or manual setup are changed.
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    method_setImplementation(update,(IMP)UpdatedNoArgument);
#else
    method_setImplementation(update,(IMP)Updated);
#endif
    method_setImplementation(enter,(IMP)Entered);installed=YES;Record("native-pane-hooks-installed");
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
@implementation VIMInstallerContinuation
+ (void)advance:(id)pane{Advance(pane);}
+ (void)bundleLoaded:(NSNotification *)notification{
    (void)notification;
    // Actual bundle-load events, not a timer, cause a guarded hook attempt.
    [self performSelectorOnMainThread:@selector(install:) withObject:nil waitUntilDone:NO];
}
+ (void)install:(id)unused{(void)unused;InstallHooks();}
@end
#endif
__attribute__((constructor)) static void Start(void){@autoreleasepool{
    // dyld has already loaded us. Do not propagate injection into runner,
    // package scripts, helpers or anything else spawned by Apple's installer.
    unsetenv("DYLD_INSERT_LIBRARIES");
    NSString *version=[[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"] objectForKey:@"ProductVersion"];
    NSString *expected=[NSString stringWithUTF8String:VIMOpenGLAutomaticInstaller([version hasPrefix:@"10.5."]||[version hasPrefix:@"10.6."]||[version hasPrefix:@"10.7."])];
    if(geteuid()!=0||![[[[NSProcessInfo processInfo] arguments] objectAtIndex:0] isEqual:expected])return;
    NSString *path=@"/var/tmp/VirtualIntelMac-InstallerAuthorization.plist";struct stat info;
    if(lstat([path fileSystemRepresentation],&info)||!S_ISREG(info.st_mode)||info.st_uid!=0||(info.st_mode&0077)||info.st_nlink!=1)return;
    NSDictionary *policy=[NSDictionary dictionaryWithContentsOfFile:path];
    NSString *guest=VIMOpenGLGuestForSystemVersion(version);
    if(![[policy objectForKey:@"Enabled"] boolValue]||![[policy objectForKey:@"Guest"] isEqual:guest]||
       ![[policy objectForKey:@"Kind"] isEqual:@"VirtualIntelMacLegacySetup"])return;
    authorization=[policy copy];
    Record("authorized-adapter-loaded");
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    [[NSNotificationCenter defaultCenter] addObserver:[VIMInstallerContinuation class] selector:@selector(bundleLoaded:) name:NSBundleDidLoadNotification object:nil];
#else
    [[NSNotificationCenter defaultCenter] addObserverForName:NSBundleDidLoadNotification object:nil queue:nil usingBlock:^(NSNotification *note){(void)note;InstallHooks();}];
#endif
    InstallHooks();
}}
