// Non-root GUI-session side of the opt-in text clipboard bridge.
// Protocol: newline-delimited JSON, protocol 2; authenticate before any text.
#import <AppKit/AppKit.h>
#ifdef VIM_OPENGL_GUEST
#import "opengl-system-version.h"
#else
#import "guest-system-version.h"
#endif
#import <IOKit/IOKitLib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
#include <libproc.h>
#else
#include "opengl-boot-identity.h"
#endif
#include <termios.h>
#include <sys/ioctl.h>
#include <pwd.h>
#include <sys/select.h>
#import "guest-console-state.h"
#import "guest-clipboard-publication.h"
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
#include <mach/mach_time.h>
#define NSPasteboardTypeString NSStringPboardType
#ifndef CFBridgingRelease
#define CFBridgingRelease(value) ([(id)(value) autorelease])
#endif
#endif
static NSTimeInterval ClipboardUptime(void){
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    static mach_timebase_info_data_t timebase={0,0};
    if(!timebase.denom)mach_timebase_info(&timebase);
    return (double)mach_absolute_time()*(double)timebase.numer/(double)timebase.denom/1e9;
#else
    return NSProcessInfo.processInfo.systemUptime;
#endif
}

#if defined(VIM_OPENGL_GUEST) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1050
static NSString *ConsoleFinderPID(uid_t uid){
    pid_t pids[2048];int n=proc_listpids(PROC_UID_ONLY,uid,pids,sizeof(pids));
    if(n<=0||n>(int)sizeof(pids))return nil;
    for(int i=0;i<n/(int)sizeof(pid_t);i++){char path[PROC_PIDPATHINFO_MAXSIZE]={0};
        if(proc_pidpath(pids[i],path,sizeof(path))>0&&!strcmp(path,"/System/Library/CoreServices/Finder.app/Contents/MacOS/Finder"))return @(pids[i]).stringValue;
    }return nil;
}
#endif

static NSDictionary *ClipboardDevices(BOOL powerOnly) {
#ifdef VIM_OPENGL_GUEST
    if(powerOnly)return nil;
    io_service_t guard=IOServiceGetMatchingService(MACH_PORT_NULL,IOServiceMatching("VMsvga2"));if(!guard)return nil;IOObjectRelease(guard);
    // COM1 is dedicated to clipboard on these guests; COM2 remains root-only
    // QGA. No raw network listener, second QGA reader or graphics traffic.
    return @{@"Callout":@"/dev/cu.serial1",@"Dialin":@"/dev/tty.serial1"};
#else
    // A short basename avoids IOSerialBSDClient's bounded device-name limit.
    // Discover the BSD suffix instead of depending on a specific port index.
    io_iterator_t clients=0;NSDictionary *paths=nil;BOOL duplicate=NO;
    if(IOServiceGetMatchingServices(MACH_PORT_NULL,IOServiceMatching("IOSerialBSDClient"),&clients)!=KERN_SUCCESS)return nil;
    NSString *name=powerOnly?@"org.vim.display":@"org.vim.clipboard";
    for(io_service_t client;(client=IOIteratorNext(clients));IOObjectRelease(client)) {
        id base=CFBridgingRelease(IORegistryEntryCreateCFProperty(client,CFSTR("IOTTYBaseName"),NULL,0));
        io_registry_entry_t parent=0;
        if(![base isEqual:name]||IORegistryEntryGetParentEntry(client,kIOServicePlane,&parent)!=KERN_SUCCESS)continue;
        // Big Sur renamed ConsoleDevice to AgentDevice. Match the observed
        // controller class instead of depending on either property spelling.
        io_registry_entry_t controller=0;
        BOOL virtio=IORegistryEntryGetParentEntry(parent,kIOServicePlane,&controller)==KERN_SUCCESS&&
            IOObjectConformsTo(controller,"AppleVirtIOConsole");
        if(controller)IOObjectRelease(controller);IOObjectRelease(parent);
        id candidate=CFBridgingRelease(IORegistryEntryCreateCFProperty(client,CFSTR("IOCalloutDevice"),NULL,0));
        id dialin=CFBridgingRelease(IORegistryEntryCreateCFProperty(client,CFSTR("IODialinDevice"),NULL,0));
        if(virtio&&[candidate isKindOfClass:NSString.class]&&[candidate hasPrefix:[@"/dev/cu." stringByAppendingString:name]]&&
           [dialin isKindOfClass:NSString.class]&&[dialin hasPrefix:[@"/dev/tty." stringByAppendingString:name]]) {
            if(paths)duplicate=YES;paths=@{@"Callout":candidate,@"Dialin":dialin};
        }
    }IOObjectRelease(clients);return duplicate?nil:paths;
#endif
}

// Provision through the authenticated root guest-agent channel, then run only
// in the user's Aqua session. No pasteboard access or saved user credentials
// in the root side; the token never appears in a process argument or log.
static BOOL RootDirectory(NSString *path) {
    BOOL created=mkdir(path.fileSystemRepresentation,0755)==0;
    if(!created&&errno!=EEXIST)return NO;
    struct stat st;if(lstat(path.fileSystemRepresentation,&st)||!S_ISDIR(st.st_mode)||st.st_uid||st.st_mode&022)return NO;
    // The root UART bootstrap deliberately uses umask 077. Its public GUI
    // job folder still needs traversal by the console user; the token itself
    // remains that user's 0600 file. Repair only our own earlier 0700 folder.
    if(created||(([path isEqual:@"/private/var/run/com.virtualintelmac.clipboard"]||
                  [path isEqual:@"/private/var/db/com.virtualintelmac.clipboard"])&&(st.st_mode&0777)==0700))
        return chmod(path.fileSystemRepresentation,0755)==0;
    return (st.st_mode&0111)==0111;
}
static BOOL Publish(NSString *path,NSData *data,uid_t owner,mode_t mode) {
    // NSUUID and keyed subscripting are 10.8 APIs. This shared helper also
    // runs on Lion; use the native CF UUID and ordinary dictionary methods.
    CFUUIDRef identifier=CFUUIDCreate(NULL);if(!identifier)return NO;
    NSString *name=CFBridgingRelease(CFUUIDCreateString(NULL,identifier));CFRelease(identifier);
    if(!name)return NO;
    NSString *temporary=[path.stringByDeletingLastPathComponent stringByAppendingPathComponent:name];
    int fd=open(temporary.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return NO;
    const uint8_t *bytes=data.bytes;NSUInteger left=data.length;BOOL ok=YES;
    while(left){ssize_t n=write(fd,bytes,left);if(n<0&&errno==EINTR)continue;if(n<=0){ok=NO;break;}bytes+=n;left-=n;}
    if(fchown(fd,owner,0)||fchmod(fd,mode)||fsync(fd))ok=NO;close(fd);
    if(ok&&rename(temporary.fileSystemRepresentation,path.fileSystemRepresentation)==0)return YES;
    unlink(temporary.fileSystemRepresentation);return NO;
}
static int LaunchControl(NSArray *arguments) {
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    NSTask *task=[NSTask new];task.launchPath=@"/Library/Application Support/Virtual Intel Mac/GuestTools/opengl-tiger-console";
    task.arguments=[@[@"--",@"/bin/launchctl"] arrayByAddingObjectsFromArray:arguments];
#else
    NSTask *task=[NSTask new];task.launchPath=@"/bin/launchctl";task.arguments=arguments;
#endif
    task.standardInput=NSFileHandle.fileHandleWithNullDevice;
    task.standardOutput=NSFileHandle.fileHandleWithNullDevice;task.standardError=NSFileHandle.fileHandleWithNullDevice;
    @try{[task launch];}@catch(NSException *exception){return -1;}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    [task waitUntilExit];return task.terminationStatus;
#else
    double deadline=ClipboardUptime()+10;
    while([task isRunning]&&ClipboardUptime()<deadline)usleep(10000);
    if([task isRunning]){[task terminate];return -1;}return task.terminationStatus;
#endif
}
static int Provision(NSString *path) {
    if(geteuid()!=0)return 77;
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *release=[version objectForKey:@"ProductVersion"];
#ifdef VIM_OPENGL_GUEST
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    if(![release hasPrefix:@"10.4."])return 77;
#else
    if(!VIMOpenGLGuestForSystemVersion(release))return 77;
#endif
#else
    if(![@[@"mojave",@"catalina",@"bigsur"] containsObject:VIMGuestForSystemVersion(release)?:@""])return 77;
#endif
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||(st.st_mode&077)||st.st_size>4096){if(fd>=0)close(fd);return 77;}
    NSMutableData *data=[NSMutableData dataWithLength:st.st_size];ssize_t count=read(fd,data.mutableBytes,data.length);close(fd);
    if(count!=st.st_size)return 74;
    id config=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    if(![config isKindOfClass:NSDictionary.class])return 65;
    NSNumber *user=[config objectForKey:@"UID"];NSString *token=[config objectForKey:@"Token"];
    BOOL powerOnly=[[config objectForKey:@"DisplayPowerOnly"] isEqual:@(YES)];
    NSString *channel=powerOnly?@"display":@"clipboard";
    if(![[config objectForKey:@"Transport"] isEqual:
#ifdef VIM_OPENGL_GUEST
        @"isa-serial"
#else
        @"virtio-serial"
#endif
        ]||![user isKindOfClass:NSNumber.class]||(!powerOnly&&user.unsignedIntValue<501)||
       ![token isKindOfClass:NSString.class]||token.length!=64||[token rangeOfCharacterFromSet:[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"] invertedSet]].location!=NSNotFound)return 65;
    struct stat console={0};if(!powerOnly&&(stat("/dev/console",&console)||console.st_uid!=user.unsignedIntValue))return 75;
    // Metadata is not tied to a logged-in user: locks/logout can leave console
    // UID 0. A fixed unprivileged system account observes IOKit, never AppKit
    // pasteboards. Root is used only for the existing one-shot provisioning.
    struct passwd *unprivileged=powerOnly?getpwnam("nobody"):NULL;
    if(powerOnly&&(!unprivileged||!unprivileged->pw_uid))return 77;
    uid_t owner=powerOnly?unprivileged->pw_uid:user.unsignedIntValue;
    gid_t group=powerOnly?unprivileged->pw_gid:console.st_gid;
    NSDictionary *devices=ClipboardDevices(powerOnly);NSString *devicePath=[devices objectForKey:@"Callout"];if(!devices)return 69;
#ifdef VIM_OPENGL_GUEST
    // Stock period systems legitimately give admin group write access to
    // /Library and daemon group write access to /var/run. Do not weaken our
    // ownership guard or chmod Apple's folders. Use the protected native db
    // tree for this private job and executable; provisioning rotates the token.
    NSString *root=@"/private/var/db/com.virtualintelmac.clipboard";
    NSArray *directories=@[@"/private",@"/private/var",@"/private/var/db",root];
#else
    NSString *root=@"/private/var/run/com.virtualintelmac.clipboard";
    NSArray *directories=@[@"/Library",@"/Library/Application Support",@"/Library/Application Support/Virtual Intel Mac",root];
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    NSEnumerator *directoryNames=[directories objectEnumerator];NSString *directory;
    while((directory=[directoryNames nextObject]))
#else
    for(NSString *directory in directories)
#endif
        if(!RootDirectory(directory))return 77;
    NSString *tokenPath=[root stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.token",channel,user]];
    NSString *jobPath=[root stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.plist",channel,user]];
#ifdef VIM_OPENGL_GUEST
    NSString *executable=[root stringByAppendingPathComponent:@"guest-clipboard"];
#else
    NSString *executable=@"/Library/Application Support/Virtual Intel Mac/guest-clipboard";
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    NSString *selfPath=VIMOpenGLProcessExecutable(getpid());if(!selfPath)return 74;
    NSData *code=[NSData dataWithContentsOfFile:selfPath];
#else
    char selfPath[PROC_PIDPATHINFO_MAXSIZE];if(proc_pidpath(getpid(),selfPath,sizeof(selfPath))<=0)return 74;
    NSData *code=[NSData dataWithContentsOfFile:@(selfPath)];
#endif
    if(!code||!Publish(executable,code,0,0755)||!Publish(tokenPath,[token dataUsingEncoding:NSUTF8StringEncoding],owner,0600))return 74;
    NSString *label=[@"com.virtualintelmac." stringByAppendingString:channel];
    NSMutableDictionary *job=[@{@"Label":label,@"RunAtLoad":@(YES),
        @"ProgramArguments":@[executable,powerOnly?@"--power-serial":@"--serial",devicePath,tokenPath]} mutableCopy];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1070
    [job setObject:@"Interactive" forKey:@"ProcessType"];
#endif
    if(powerOnly)[job setObject:@"nobody" forKey:@"UserName"];
#ifdef VIM_OPENGL_GUEST
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    if(powerOnly)return 75;
    // Tiger's native child enters the real console bootstrap port and
    // permanently drops root before invoking its original launchctl.
    LaunchControl(@[@"unload",jobPath]);
#else
    NSString *finder=ConsoleFinderPID(owner);if(!finder||powerOnly)return 75;
    // Period sudo predates -n. -S with /dev/null input cannot prompt or
    // consume configuration bytes; root still drops to the real console UID.
    // Darwin9 liblaunch deliberately skips the Aqua bootstrap socket when
    // SUDO_COMMAND is inherited. Its env(1) also predates -u: use a clean
    // child-only environment, not launchctl setenv or a guessed socket path.
    NSArray *aqua=@[@"bsexec",finder,@"/usr/bin/sudo",@"-S",@"-u",[@"#" stringByAppendingString:user.stringValue],
        @"/usr/bin/env",@"-i",@"PATH=/usr/bin:/bin:/usr/sbin:/sbin",@"/bin/launchctl"];
    // Unload the old manifest before replacing it: earlier development jobs
    // lacked a session type and were admitted into Background instead of Aqua.
    LaunchControl([aqua arrayByAddingObjectsFromArray:@[@"unload",jobPath]]);
    LaunchControl([aqua arrayByAddingObjectsFromArray:@[@"unload",@"-S",@"Aqua",jobPath]]);
    [job setObject:@"Aqua" forKey:@"LimitLoadToSessionType"];
#endif
#else
    NSString *domain=powerOnly?@"system":[NSString stringWithFormat:@"gui/%@",user];
    if(powerOnly&&user.unsignedIntValue>=501)LaunchControl(@[@"bootout",[NSString stringWithFormat:@"gui/%@/%@",user,label]]);
    LaunchControl(@[@"bootout",[domain stringByAppendingFormat:@"/%@",label]]);
#endif
    NSData *plist=[NSPropertyListSerialization dataWithPropertyList:job format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    if(!plist||!Publish(jobPath,plist,0,0644))return 74;
    // Only this fixed dedicated device is granted to its service account. The
    // privileged QGA port is never touched and no arbitrary path is accepted.
    int serial=open(devicePath.fileSystemRepresentation,O_RDWR|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat device;BOOL secured=serial>=0&&fstat(serial,&device)==0&&S_ISCHR(device.st_mode)&&
        fchown(serial,owner,group)==0&&fchmod(serial,0600)==0;
    if(serial>=0)close(serial);if(!secured)return 77;
    // The dial-in alias reaches the same serial provider. Protect it too,
    // rather than leaving a second world-writable path around the callout ACL.
    // /dev is kernel-owned; neither path comes from the incoming JSON.
    NSString *dialin=[devices objectForKey:@"Dialin"];struct stat alias;
    if(lstat(dialin.fileSystemRepresentation,&alias)||!S_ISCHR(alias.st_mode)||
       chown(dialin.fileSystemRepresentation,owner,group)||chmod(dialin.fileSystemRepresentation,0600))return 77;
#ifdef VIM_OPENGL_GUEST
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    return LaunchControl(@[@"load",jobPath])==0?0:69;
#else
    return LaunchControl([aqua arrayByAddingObjectsFromArray:@[@"load",@"-S",@"Aqua",jobPath]])==0?0:69;
#endif
#else
    return LaunchControl(@[@"bootstrap",domain,jobPath])==0?0:69;
#endif
}

#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
typedef unsigned long long VIMClipboardSequence;
#else
typedef NSUInteger VIMClipboardSequence;
#endif
static const NSUInteger TextLimit=1024*1024,WireLimit=8*1024*1024;
static BOOL ActiveConsoleUser(void) {
    struct stat console;return !stat("/dev/console",&console)&&console.st_uid==getuid();
}
@interface ClipboardAgent : NSObject
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
{
    int _descriptor,_selectWakeRead,_selectWakeWrite;
    NSMutableData *_incoming,*_outgoing;NSString *_token;
    BOOL _authenticated,_displayPowerOnly;NSInteger _observedChange;
    VIMClipboardSequence _localSequence,_remoteSequence,_remoteHeartbeat;
    NSTimeInterval _lastHeartbeat;NSDate *_deadline;NSNumber *_lastDisplaySleep;
    VIMClipboardPendingPublication _publication;
    io_service_t _displayWrangler;io_object_t _powerNotifier;IONotificationPortRef _powerPort;
}
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
#define VIM_CLIPBOARD_ATOMIC nonatomic
#else
#define VIM_CLIPBOARD_ATOMIC atomic
#endif
@property(VIM_CLIPBOARD_ATOMIC) int descriptor;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
@property dispatch_source_t reader;
@property dispatch_source_t writer;
#endif
#if defined(VIM_OPENGL_GUEST) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
@property(VIM_CLIPBOARD_ATOMIC) int selectWakeRead;
@property(VIM_CLIPBOARD_ATOMIC) int selectWakeWrite;
- (BOOL)startSerialReadiness;
#endif
@property(VIM_CLIPBOARD_ATOMIC,retain) NSMutableData *incoming;
@property(VIM_CLIPBOARD_ATOMIC,retain) NSMutableData *outgoing;
@property(VIM_CLIPBOARD_ATOMIC,retain) NSString *token;
@property(VIM_CLIPBOARD_ATOMIC) BOOL authenticated;
@property(VIM_CLIPBOARD_ATOMIC) NSInteger observedChange;
@property(VIM_CLIPBOARD_ATOMIC) VIMClipboardPendingPublication publication;
@property(VIM_CLIPBOARD_ATOMIC) VIMClipboardSequence localSequence;
@property(VIM_CLIPBOARD_ATOMIC) VIMClipboardSequence remoteSequence;
@property(VIM_CLIPBOARD_ATOMIC) VIMClipboardSequence remoteHeartbeat;
@property(VIM_CLIPBOARD_ATOMIC) NSTimeInterval lastHeartbeat;
@property(VIM_CLIPBOARD_ATOMIC,retain) NSDate *deadline;
@property(VIM_CLIPBOARD_ATOMIC) BOOL displayPowerOnly;
@property(VIM_CLIPBOARD_ATOMIC,retain) NSNumber *lastDisplaySleep;
@property(VIM_CLIPBOARD_ATOMIC) io_service_t displayWrangler;
@property(VIM_CLIPBOARD_ATOMIC) io_object_t powerNotifier;
@property(VIM_CLIPBOARD_ATOMIC) IONotificationPortRef powerPort;
- (void)emitDisplayPower;
- (void)tick:(NSTimer *)timer;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
- (void)serialReadinessWorker:(id)unused;
- (void)readinessOnMain:(NSNumber *)events;
- (void)pendingOnMain:(NSMutableArray *)result;
#endif
@end
@implementation ClipboardAgent
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
@synthesize descriptor=_descriptor,selectWakeRead=_selectWakeRead,selectWakeWrite=_selectWakeWrite;
@synthesize authenticated=_authenticated,observedChange=_observedChange,publication=_publication;
@synthesize localSequence=_localSequence,remoteSequence=_remoteSequence,remoteHeartbeat=_remoteHeartbeat,lastHeartbeat=_lastHeartbeat;
@synthesize displayPowerOnly=_displayPowerOnly;
@synthesize displayWrangler=_displayWrangler,powerNotifier=_powerNotifier,powerPort=_powerPort;
// The ObjC1 runtime predates objc_getProperty/objc_setProperty. All queue and
// pasteboard state is main-run-loop owned, including worker handoffs above.
- (NSMutableData *)incoming{return _incoming;}
- (void)setIncoming:(NSMutableData *)value{if(_incoming!=value){[_incoming release];_incoming=[value retain];}}
- (NSMutableData *)outgoing{return _outgoing;}
- (void)setOutgoing:(NSMutableData *)value{if(_outgoing!=value){[_outgoing release];_outgoing=[value retain];}}
- (NSString *)token{return _token;}
- (void)setToken:(NSString *)value{if(_token!=value){[_token release];_token=[value retain];}}
- (NSDate *)deadline{return _deadline;}
- (void)setDeadline:(NSDate *)value{if(_deadline!=value){[_deadline release];_deadline=[value retain];}}
- (NSNumber *)lastDisplaySleep{return _lastDisplaySleep;}
- (void)setLastDisplaySleep:(NSNumber *)value{if(_lastDisplaySleep!=value){[_lastDisplaySleep release];_lastDisplaySleep=[value retain];}}
#endif
- (NSDictionary *)displayPowerState {
    return CFBridgingRelease(IORegistryEntryCreateCFProperty(self.displayWrangler,CFSTR("IOPowerManagement"),NULL,0));
}
- (void)emitDisplayPower {
    if(!self.authenticated)return;
    NSDictionary *power=[self displayPowerState];
    id state=[power isKindOfClass:NSDictionary.class]?[power objectForKey:@"CurrentPowerState"]:nil;
    if(![state isKindOfClass:NSNumber.class])return;
    // IODisplayWrangler states 3/4 are dim/on; 0/1/2 have no usable display.
    BOOL sleeping=[state unsignedIntValue]<3;
    NSNumber *sleep=@(sleeping);
    if([sleep isEqual:self.lastDisplaySleep])return;
    self.lastDisplaySleep=sleep;[self send:@{@"type":@"display-power",@"sleeping":sleep}];
}
- (NSInteger)consoleLockState {
#if defined(VIM_OPENGL_GUEST) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    // Snow does not publish IOConsoleLocked. Product policy explicitly permits
    // clipboard exchange while locked on this release. The authenticated
    // channel, console-user ownership and foreground-VM gates still apply;
    // never infer an unlocked state for any other OS from a missing property.
    return 0;
#else
    return VIMConsoleLockState();
#endif
}
- (NSPasteboard *)pasteboard {return NSPasteboard.generalPasteboard;}
- (void)stop {close(self.descriptor);exit(75);}
#if defined(VIM_OPENGL_GUEST) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
- (void)wakeSerialReadiness {
    uint8_t byte=1;ssize_t n;
    do {n=write(self.selectWakeWrite,&byte,1);}while(n<0&&errno==EINTR);
    // A full nonblocking notification pipe already guarantees a wakeup.
    if(n!=1&&!(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)))[self stop];
}
- (BOOL)startSerialReadiness {
    // Darwin 10 spec_kqfilter only accepts PTYs (and BPF), not IOSerialBSD
    // UARTs. IOSerialFamily-56 instead supplies iossselect -> ttyselect.
    // Block in that actual driver readiness path, never periodically poll.
    // All protocol/pasteboard/queue work stays serialized on the main thread.
    int wake[2];if(pipe(wake))return NO;
    if(self.descriptor>=FD_SETSIZE||wake[0]>=FD_SETSIZE||
       fcntl(wake[0],F_SETFL,O_NONBLOCK)||fcntl(wake[1],F_SETFL,O_NONBLOCK)||
       fcntl(wake[0],F_SETFD,FD_CLOEXEC)||fcntl(wake[1],F_SETFD,FD_CLOEXEC)){
        close(wake[0]);close(wake[1]);return NO;
    }
    self.selectWakeRead=wake[0];self.selectWakeWrite=wake[1];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    [NSThread detachNewThreadSelector:@selector(serialReadinessWorker:) toTarget:self withObject:nil];
#else
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),^{
        // This worker owns the agent for its process lifetime, as the GCD
        // reader does on later systems. stop exits rather than closing a live
        // FD underneath another reader. There is exactly one UART reader.
        for(;;){@autoreleasepool {
            __block BOOL pending=NO;
            dispatch_sync(dispatch_get_main_queue(),^{pending=self.outgoing.length!=0;});
            fd_set reads,writes;FD_ZERO(&reads);FD_ZERO(&writes);
            FD_SET(self.descriptor,&reads);FD_SET(self.selectWakeRead,&reads);
            if(pending)FD_SET(self.descriptor,&writes);
            int ready=select(MAX(self.descriptor,self.selectWakeRead)+1,&reads,&writes,NULL,NULL);
            if(ready<0){if(errno==EINTR)continue;dispatch_sync(dispatch_get_main_queue(),^{[self stop];});return;}
            if(FD_ISSET(self.selectWakeRead,&reads)){
                uint8_t bytes[128];ssize_t n;
                do {n=read(self.selectWakeRead,bytes,sizeof(bytes));}while(n>0||(n<0&&errno==EINTR));
                if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)){
                    dispatch_sync(dispatch_get_main_queue(),^{[self stop];});return;
                }
            }
            BOOL readable=FD_ISSET(self.descriptor,&reads),writable=FD_ISSET(self.descriptor,&writes);
            if(readable||writable)dispatch_sync(dispatch_get_main_queue(),^{
                if(readable)[self readAvailable];if(writable)[self flush];
            });
        }}
    });
#endif
    return YES;
}
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
- (void)pendingOnMain:(NSMutableArray *)result { @autoreleasepool {
    [result addObject:[NSNumber numberWithBool:self.outgoing.length!=0]];
}}
- (void)readinessOnMain:(NSNumber *)events { @autoreleasepool {
    unsigned mask=[events unsignedIntValue];
    if(mask&4){[self stop];return;}
    if(mask&1)[self readAvailable];if(mask&2)[self flush];
}}
- (void)serialReadinessWorker:(id)unused {
    (void)unused;
    // Same bounded single-reader protocol as Snow. Leopard's native
    // NSThread/main-run-loop handoff replaces GCD, not the UART or wire ABI.
    for(;;){NSAutoreleasePool *pool=[NSAutoreleasePool new];
        NSMutableArray *state=[NSMutableArray array];
        [self performSelectorOnMainThread:@selector(pendingOnMain:) withObject:state waitUntilDone:YES];
        fd_set reads,writes;FD_ZERO(&reads);FD_ZERO(&writes);
        FD_SET(self.descriptor,&reads);FD_SET(self.selectWakeRead,&reads);
        if([[state objectAtIndex:0] boolValue])FD_SET(self.descriptor,&writes);
        int ready=select(MAX(self.descriptor,self.selectWakeRead)+1,&reads,&writes,NULL,NULL);
        unsigned events=0;
        if(ready<0&&errno!=EINTR)events=4;
        if(ready>0){
            if(FD_ISSET(self.selectWakeRead,&reads)){
                uint8_t bytes[128];ssize_t n;
                do{n=read(self.selectWakeRead,bytes,sizeof(bytes));}while(n>0||(n<0&&errno==EINTR));
                if(n==0||(n<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK))events=4;
            }
            if(FD_ISSET(self.descriptor,&reads))events|=1;
            if(FD_ISSET(self.descriptor,&writes))events|=2;
        }
        if(events)[self performSelectorOnMainThread:@selector(readinessOnMain:)
            withObject:[NSNumber numberWithUnsignedInt:events] waitUntilDone:YES];
        [pool drain];
    }
}
#endif
#endif
- (void)flush {
    NSUInteger budget=65536;
    while(budget&&self.outgoing.length) {
        ssize_t n=write(self.descriptor,self.outgoing.bytes,MIN(budget,self.outgoing.length));
        if(n<0&&errno==EINTR)continue;
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
        if(n<=0){[self stop];return;}budget-=n;
        [self.outgoing replaceBytesInRange:NSMakeRange(0,n) withBytes:NULL length:0];
    }
#if defined(VIM_OPENGL_GUEST) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    if(self.selectWakeWrite>=0){if(self.outgoing.length)[self wakeSerialReadiness];return;}
#endif
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ >= 1060
    if(self.outgoing.length&&!self.writer) {
        self.writer=dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE,self.descriptor,0,dispatch_get_main_queue());
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
        // The main reader's event block owns this agent until process exit.
        // Snow Leopard has no zeroing weak runtime. This secondary writer
        // must not create another retain cycle around a short-lived source.
        __unsafe_unretained ClipboardAgent *weakSelf=self;
#else
        __weak ClipboardAgent *weakSelf=self;
#endif
        dispatch_source_set_event_handler(self.writer,^{[weakSelf flush];});dispatch_resume(self.writer);
    }else if(!self.outgoing.length&&self.writer){
        dispatch_source_cancel(self.writer);
#if !OS_OBJECT_USE_OBJC
        dispatch_release(self.writer);
#endif
        self.writer=nil;
    }
#endif
}
- (void)send:(NSDictionary *)message {
    NSData *data=[NSJSONSerialization dataWithJSONObject:message options:0 error:NULL];
    if(!data||data.length>WireLimit||self.outgoing.length+data.length+1>WireLimit){[self stop];return;}
    [self.outgoing appendData:data];[self.outgoing appendBytes:"\n" length:1];[self flush];
}
- (void)receive:(NSDictionary *)message {
    // Check at the actual receive boundary too, not only the 200 ms timer:
    // a fast-user switch must not apply host text to the old user's pasteboard.
    if(!self.displayPowerOnly&&!ActiveConsoleUser()){[self stop];return;}
    if(!self.authenticated) {
        if(![[message objectForKey:@"type"] isEqual:@"ready"]||![[message objectForKey:@"protocol"] isEqual:@2]){[self stop];return;}
        self.authenticated=YES;
        self.lastHeartbeat=ClipboardUptime();
        // Do not export old pasteboard contents merely because sharing starts.
        if(self.displayPowerOnly)[self emitDisplayPower];else self.observedChange=self.pasteboard.changeCount;
        return;
    }
    if([[message objectForKey:@"type"] isEqual:@"stop"]){[self stop];return;}
    if([[message objectForKey:@"type"] isEqual:@"ping"]) {
        id sequence=[message objectForKey:@"sequence"];
        double value=[sequence isKindOfClass:NSNumber.class]?[sequence doubleValue]:0;
        if(value<1||value>9007199254740991.0||floor(value)!=value||value<=self.remoteHeartbeat){[self stop];return;}
        self.remoteHeartbeat=(VIMClipboardSequence)value;
        self.lastHeartbeat=ClipboardUptime();
        [self send:@{@"type":@"pong",@"sequence":@(self.remoteHeartbeat)}];return;
    }
    if(self.displayPowerOnly){[self stop];return;}
    if(![[message objectForKey:@"type"] isEqual:@"text"]||![[message objectForKey:@"sequence"] isKindOfClass:NSNumber.class]||
       ![[message objectForKey:@"text"] isKindOfClass:NSString.class]){[self stop];return;}
    double sequence=[[message objectForKey:@"sequence"] doubleValue];
    if(sequence<1||sequence>9007199254740991.0||floor(sequence)!=sequence||sequence<=self.remoteSequence||
       [[message objectForKey:@"text"] lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>TextLimit){[self stop];return;}
    self.remoteSequence=(VIMClipboardSequence)sequence;
    // Keep heartbeats and sequence validation live while locked, but never
    // apply host text there. Consuming the sequence prevents replay on unlock.
    if([self consoleLockState]!=0)return;
    NSPasteboard *pasteboard=self.pasteboard;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    [pasteboard declareTypes:[NSArray arrayWithObject:NSStringPboardType] owner:nil];
#else
    [pasteboard clearContents];
#endif
    [pasteboard setString:[message objectForKey:@"text"] forType:NSPasteboardTypeString];
    self.observedChange=pasteboard.changeCount; // suppress reflection to host
}
- (void)readAvailable { @autoreleasepool {
    uint8_t bytes[16384];NSUInteger budget=65536;
    while(budget) {
        ssize_t n=read(self.descriptor,bytes,MIN(sizeof(bytes),budget));
        if(n<0&&errno==EINTR)continue;
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
        if(n<=0){[self stop];return;}budget-=n;[self.incoming appendBytes:bytes length:n];
        if(self.incoming.length>WireLimit){[self stop];return;}
        while(self.incoming.length) {
            const uint8_t *start=self.incoming.bytes;
            const uint8_t *end=memchr(start,'\n',self.incoming.length);if(!end)break;
            NSUInteger length=end-start;
            NSData *line=[self.incoming subdataWithRange:NSMakeRange(0,length)];
            [self.incoming replaceBytesInRange:NSMakeRange(0,length+1) withBytes:NULL length:0];
            id message=[NSJSONSerialization JSONObjectWithData:line options:0 error:NULL];
            if(![message isKindOfClass:NSDictionary.class]){[self stop];return;}[self receive:message];
        }
    }
}}
- (void)tick:(NSTimer *)timer { @autoreleasepool {
    (void)timer;
    if(!self.displayPowerOnly&&!ActiveConsoleUser())[self stop];
    if(!self.authenticated){if(self.deadline.timeIntervalSinceNow<0)[self stop];return;}
    // A host crash/backpressured close may not deliver serial EOF or the
    // best-effort stop frame. Retire this GUI helper rather than polling the
    // pasteboard forever. Normal authenticated pings arrive every 5 seconds.
    if(ClipboardUptime()-self.lastHeartbeat>=45){[self stop];return;}
    if(self.displayPowerOnly)return;
    NSPasteboard *pasteboard=self.pasteboard;
    NSInteger change=pasteboard.changeCount;
    if(change==self.observedChange)return;
    NSTimeInterval now=ClipboardUptime();
    VIMClipboardPendingPublication pending=self.publication;
    // No registry query in the unchanged 200 ms idle path. Observe/discard a
    // changed count while locked/unknown, before reading any clipboard text.
    // Unlock by itself must not export content accumulated behind the lock.
    if([self consoleLockState]!=0){self.observedChange=change;self.publication=(VIMClipboardPendingPublication){0};return;}
    if(!VIMClipboardPublicationDue(&pending,change,now)){self.publication=pending;return;}
    NSString *text=[pasteboard stringForType:NSPasteboardTypeString];
    if(!text){
        if(pasteboard.types.count&&![pasteboard availableTypeFromArray:@[NSPasteboardTypeString]]){
            self.observedChange=change;self.publication=(VIMClipboardPendingPublication){0};
        }else{VIMClipboardPublicationPending(&pending,now);self.publication=pending;}
        return;
    }
    self.observedChange=change;self.publication=(VIMClipboardPendingPublication){0};
    if([text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>TextLimit)return;
    [self send:@{@"type":@"text",@"sequence":@(++self.localSequence),@"text":text}];
}}
@end

static void DisplayInterest(void *context,io_service_t service,natural_t type,void *argument) {
    (void)service;(void)type;(void)argument;
    [(__bridge ClipboardAgent *)context emitDisplayPower];
}

int main(int argc,const char **argv){@autoreleasepool {
#ifdef VIM_OPENGL_GUEST
    if(argc==2&&!strcmp(argv[1],"--provision-stdin")){
        if(geteuid()!=0)return 77;
        NSData *input=[NSFileHandle.fileHandleWithStandardInput readDataOfLength:4097];if(!input.length||input.length>4096)return 65;
        char name[]="/private/var/run/VIMClipboard.XXXXXX";int fd=mkstemp(name);if(fd<0)return 74;
        const uint8_t *bytes=input.bytes;NSUInteger left=input.length;BOOL ok=YES;
        while(left){ssize_t n=write(fd,bytes,left);if(n<0&&errno==EINTR)continue;if(n<=0){ok=NO;break;}bytes+=n;left-=n;}
        if(fchmod(fd,0600)||fsync(fd))ok=NO;close(fd);
        int result=ok?Provision(@(name)):74;unlink(name);return result;
    }
#endif
    if(argc==3&&!strcmp(argv[1],"--provision"))return Provision(@(argv[2]));
    BOOL powerOnly=argc==4&&!strcmp(argv[1],"--power-serial");
    if(argc!=4||getuid()==0||(!powerOnly&&strcmp(argv[1],"--serial"))||![@(argv[2]) hasPrefix:@"/dev/"]) {
        fprintf(stderr,"Run guest-clipboard --serial with its dedicated device in the logged-in GUI session.\n");return 64;
    }
    if(!powerOnly&&!ActiveConsoleUser())return 75;
    int fd=open(argv[3],O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=getuid()||
       (st.st_mode&077)!=0||st.st_nlink!=1||st.st_size<32||st.st_size>128){if(fd>=0)close(fd);return 77;}
    char token[129]={0};ssize_t count=read(fd,token,(size_t)st.st_size);close(fd);
    if(count!=st.st_size)return 77;
    NSString *secret=[[NSString alloc] initWithBytes:token length:count encoding:NSUTF8StringEncoding];
    NSCharacterSet *invalid=[[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdefABCDEF"] invertedSet];
    if(!secret||[secret rangeOfCharacterFromSet:invalid].location!=NSNotFound)return 77;
    ClipboardAgent *agent=[ClipboardAgent new];agent.token=secret;
#if defined(VIM_OPENGL_GUEST) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    agent.selectWakeRead=-1;agent.selectWakeWrite=-1;
#endif
    agent.displayPowerOnly=powerOnly;
    if(powerOnly) {
        agent.displayWrangler=IOServiceGetMatchingService(MACH_PORT_NULL,IOServiceMatching("IODisplayWrangler"));
        agent.powerPort=IONotificationPortCreate(MACH_PORT_NULL);io_object_t notifier=0;
        if(!agent.displayWrangler||!agent.powerPort||IOServiceAddInterestNotification(agent.powerPort,agent.displayWrangler,kIOGeneralInterest,DisplayInterest,(__bridge void *)agent,&notifier))return 69;
        agent.powerNotifier=notifier;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
        CFRunLoopAddSource(CFRunLoopGetCurrent(),IONotificationPortGetRunLoopSource(agent.powerPort),kCFRunLoopCommonModes);
#else
        CFRunLoopAddSource(CFRunLoopGetMain(),IONotificationPortGetRunLoopSource(agent.powerPort),kCFRunLoopCommonModes);
#endif
    }
    agent.incoming=[NSMutableData data];agent.outgoing=[NSMutableData data];
    agent.deadline=[NSDate dateWithTimeIntervalSinceNow:10];
    // Provisioning always selects the dedicated VirtIO device. Keeping this non-root
    // transport adapter usable with an owned private TTY also permits a PTY
    // handshake fixture; owner/mode/type validation is identical in both cases.
    agent.descriptor=open(argv[2],O_RDWR|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat device;struct termios settings;
    if(agent.descriptor<0||fstat(agent.descriptor,&device)||!S_ISCHR(device.st_mode)||device.st_uid!=getuid()||
       (device.st_mode&077)||ioctl(agent.descriptor,TIOCEXCL)||tcgetattr(agent.descriptor,&settings))return 77;
    cfmakeraw(&settings);settings.c_cflag|=CLOCAL|CREAD;
#ifdef VIM_OPENGL_GUEST
    cfsetspeed(&settings,B115200);settings.c_cflag&=~(CCTS_OFLOW|CRTS_IFLOW);
#endif
    if(tcsetattr(agent.descriptor,TCSANOW,&settings))return 69;
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1060
    // Leopard has no dispatch sources, including for the private PTY fixture.
    if(![agent startSerialReadiness])return 69;
#else
#if defined(VIM_OPENGL_GUEST) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1070
    if(!strcmp(argv[2],"/dev/cu.serial1")){
        if(![agent startSerialReadiness])return 69;
    }else
#endif
    {
        agent.reader=dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,agent.descriptor,0,dispatch_get_main_queue());
        dispatch_source_set_event_handler(agent.reader,^{[agent readAvailable];});dispatch_resume(agent.reader);
    }
#endif
    [agent send:@{@"type":@"hello",@"protocol":@2,@"token":secret}];
    [NSTimer scheduledTimerWithTimeInterval:powerOnly?1:0.2 target:agent selector:@selector(tick:) userInfo:nil repeats:YES];
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
    [[NSRunLoop currentRunLoop] run];return 0;
#else
    [NSRunLoop.mainRunLoop run];return 0;
#endif
}}
