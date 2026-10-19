// Native Mojave/Catalina accessory UI, with a root CLI and an unprivileged GUI.
#import <Cocoa/Cocoa.h>
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/event.h>
#include <math.h>
#include <libproc.h>
#include "guest-progress-state.h"

static void PrintJSON(NSDictionary *value) {
    NSData *data=[NSJSONSerialization dataWithJSONObject:value options:0 error:NULL];
    VIMProgressCheck(data!=nil,@"Could not encode progress receipt.");fwrite(data.bytes,1,data.length,stdout);puts("");
}
static NSDictionary *StartUI(NSString *icon) {
    VIMProgressCheck(geteuid()==0,@"Guest progress setup requires the privileged guest agent.");
    struct stat console;VIMProgressCheck(!stat("/dev/console",&console)&&console.st_uid>=501,@"Sign in to the desktop before finishing setup.");
    struct passwd *user=getpwuid(console.st_uid);VIMProgressCheck(user!=NULL,@"The desktop account is unavailable.");
    NSString *username=@(user->pw_name);
    char temporary[]="/private/var/tmp/VirtualIntelMacUI.XXXXXX";
    VIMProgressCheck(mkdtemp(temporary)!=NULL,@"Could not create guest progress state.");NSString *path=@(temporary);
    uint32_t length=0;_NSGetExecutablePath(NULL,&length);NSMutableData *bytes=[NSMutableData dataWithLength:length];
    VIMProgressCheck(!_NSGetExecutablePath(bytes.mutableBytes,&length),@"Could not find the progress executable.");
    NSString *executable=[[NSString stringWithUTF8String:(const char *)bytes.bytes] stringByResolvingSymlinksInPath];
    NSFileManager *fm=NSFileManager.defaultManager;NSError *error=nil;
    NSString *copied=[path stringByAppendingPathComponent:@"GuestProgress"];
    VIMProgressCheck([fm copyItemAtPath:executable toPath:copied error:&error],error.localizedDescription);
    VIMProgressCheck([fm copyItemAtPath:icon toPath:[path stringByAppendingPathComponent:@"Icon.png"] error:&error],error.localizedDescription);
    VIMProgressCheck(!chmod(copied.fileSystemRepresentation,0755)&&!chmod([path stringByAppendingPathComponent:@"Icon.png"].fileSystemRepresentation,0644),@"Could not prepare guest progress artwork.");
    NSString *control=[path stringByAppendingPathComponent:@"Control"];
    VIMProgressCheck(!mkdir(control.fileSystemRepresentation,0700)&&!chown(control.fileSystemRepresentation,console.st_uid,console.st_gid),@"Could not prepare guest progress controls.");
    VIMProgressCheck(!chmod(path.fileSystemRepresentation,0755),@"Could not expose read-only progress state.");
    int directory=VIMProgressDirectory(path);VIMProgressCheck(directory>=0,@"Could not validate guest progress state.");
    @try {VIMProgressWrite(directory,@"State.plist",@{@"FormatVersion":@1,@"Mode":@"working",@"Fraction":@0,
        @"Message":@"Preparing your virtual Mac…",@"Revision":NSUUID.UUID.UUIDString,@"ConsoleUID":@(console.st_uid)},0644);}
    @finally {close(directory);}
    // Never run AppKit as root. Detach all inherited QGA output pipes so the
    // short bootstrap command can finish while the accessory window stays up.
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/bin/launchctl"];
    task.arguments=@[@"asuser",[@(console.st_uid) stringValue],@"/usr/bin/sudo",@"-u",username,copied,@"--ui",path];
    NSFileHandle *null=[NSFileHandle fileHandleForUpdatingAtPath:@"/dev/null"];
    task.standardInput=null;task.standardOutput=null;task.standardError=null;
    VIMProgressCheck([task launchAndReturnError:&error],error.localizedDescription);
    BOOL ready=NO;NSNumber *guiPID=nil;
    int controls=open(control.fileSystemRepresentation,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    VIMProgressCheck(controls>=0,@"Could not monitor the setup window.");
    @try {for(unsigned i=0;i<300&&task.running;i++) {
        NSDictionary *ack=VIMProgressRead(controls,@"Ready.plist",console.st_uid);
        if([ack[@"Visible"] isEqual:@YES]&&[ack[@"PID"] isKindOfClass:NSNumber.class]){ready=YES;guiPID=ack[@"PID"];break;}
        usleep(100000);
    }} @finally {close(controls);}
    VIMProgressCheck(ready,@"The guest setup window could not open. No installation was started.");
    return @{@"Directory":path,@"ConsoleUID":@(console.st_uid),@"PID":guiPID,@"Started":@YES};
}
static NSDictionary *WaitAction(NSString *path) {
    VIMProgressCheck(geteuid()==0,@"Restart coordination requires the guest agent.");
    int directory=VIMProgressDirectory(path);VIMProgressCheck(directory>=0,@"Guest progress state is unavailable.");
    int control=-1,events=-1;
    @try {
        NSDictionary *state=VIMProgressRead(directory,@"State.plist",0);
        VIMProgressCheck([state[@"Mode"] isEqual:@"restart"]&&[state[@"Deadline"] isKindOfClass:NSDate.class],@"No restart countdown is active.");
        uid_t uid=[state[@"ConsoleUID"] unsignedIntValue];
        control=openat(directory,"Control",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
        VIMProgressCheck(control>=0&&!fstat(control,&st)&&st.st_uid==uid&&(st.st_mode&0777)==0700,@"Invalid restart controls.");
        events=kqueue();VIMProgressCheck(events>=0,@"Could not monitor restart controls.");
        struct kevent change;EV_SET(&change,control,EVFILT_VNODE,EV_ADD|EV_CLEAR,NOTE_WRITE,0,NULL);
        VIMProgressCheck(!kevent(events,&change,1,NULL,0,NULL),@"Could not monitor restart controls.");
        for(;;) {
            NSDictionary *request=VIMProgressRead(control,@"Request.plist",uid);
            NSString *action=request[@"Action"];
            if([request[@"Revision"] isEqual:state[@"Revision"]]&&[@[@"restart",@"postpone"] containsObject:action])
                return @{@"Action":action,@"Revision":state[@"Revision"]};
            NSDictionary *ready=VIMProgressRead(control,@"Ready.plist",uid);struct proc_bsdinfo process={0};
            pid_t pid=[ready[@"PID"] intValue];
            if(pid<=1||proc_pidinfo(pid,PROC_PIDTBSDINFO,0,&process,sizeof(process))!=sizeof(process)||process.pbi_uid!=uid)
                return @{@"Action":@"postpone",@"Revision":state[@"Revision"],@"Reason":@"The setup window closed before a choice was confirmed."};
            if([state[@"Deadline"] timeIntervalSinceNow]<=0)return @{@"Action":@"restart",@"Revision":state[@"Revision"]};
            struct timespec timeout={.tv_sec=1};struct kevent event;
            int result=kevent(events,NULL,0,&event,1,&timeout);
            VIMProgressCheck(result>=0||errno==EINTR,@"Restart controls were interrupted.");
        }
    } @finally {if(events>=0)close(events);if(control>=0)close(control);close(directory);}
}

@interface GuestProgress : NSObject <NSApplicationDelegate,NSWindowDelegate>
@property NSString *directory;
@property NSWindow *window;
@property NSTextField *titleLabel,*message,*detail;
@property NSProgressIndicator *progress;
@property NSButton *restart,*postpone;
@property NSImage *icon;
@property NSDictionary *state;
@property BOOL permittedToClose;
@property id activity;
@property dispatch_source_t stateWatcher;
@property NSTimer *countdownTimer;
@end
@implementation GuestProgress
- (NSTextField *)label:(NSString *)text font:(NSFont *)font frame:(NSRect)frame {
    NSTextField *label=[NSTextField labelWithString:text];label.font=font;label.frame=frame;label.selectable=NO;
    label.lineBreakMode=NSLineBreakByWordWrapping;label.maximumNumberOfLines=3;[self.window.contentView addSubview:label];return label;
}
- (void)applicationDidFinishLaunching:(NSNotification *)note {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    self.icon=[[NSImage alloc] initWithContentsOfFile:[self.directory stringByAppendingPathComponent:@"Icon.png"]];
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,500,198) styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"Virtual Intel Mac Setup";self.window.delegate=self;self.window.releasedWhenClosed=NO;
    NSImageView *image=[[NSImageView alloc] initWithFrame:NSMakeRect(25,96,64,64)];image.image=self.icon;[self.window.contentView addSubview:image];
    self.titleLabel=[self label:@"Finishing Setup" font:[NSFont boldSystemFontOfSize:20] frame:NSMakeRect(100,133,376,28)];
    self.message=[self label:@"Preparing your virtual Mac…" font:[NSFont systemFontOfSize:13] frame:NSMakeRect(100,83,371,44)];
    self.progress=[[NSProgressIndicator alloc] initWithFrame:NSMakeRect(26,54,448,16)];self.progress.minValue=0;self.progress.maxValue=1;self.progress.indeterminate=YES;[self.progress startAnimation:nil];[self.window.contentView addSubview:self.progress];
    self.detail=[self label:@"Setup may take up to 30 minutes. Keep this Mac running." font:[NSFont systemFontOfSize:12] frame:NSMakeRect(26,14,448,32)];self.detail.textColor=NSColor.secondaryLabelColor;
    // Keep the header/progress group anchored to the top. Only the countdown
    // and completion states need an extra row for their bottom buttons.
    for(NSView *view in @[image,self.titleLabel,self.message,self.progress,self.detail])view.autoresizingMask=NSViewMinYMargin;
    self.restart=[NSButton buttonWithTitle:@"Restart Now" target:self action:@selector(restartNow:)];self.restart.frame=NSMakeRect(248,15,112,32);self.restart.keyEquivalent=@"\r";[self.window.contentView addSubview:self.restart];
    self.postpone=[NSButton buttonWithTitle:@"Postpone" target:self action:@selector(postpone:)];self.postpone.frame=NSMakeRect(368,15,112,32);[self.window.contentView addSubview:self.postpone];
    [self.window center];[self.window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    // Atomic status publication replaces State.plist. Watch its containing
    // directory, not the old file inode, so setup has no recurring idle poll.
    int watched=VIMProgressDirectory(self.directory);
    VIMProgressCheck(watched>=0,@"Invalid setup window state.");
    self.stateWatcher=dispatch_source_create(DISPATCH_SOURCE_TYPE_VNODE,watched,
        DISPATCH_VNODE_WRITE|DISPATCH_VNODE_DELETE|DISPATCH_VNODE_REVOKE,dispatch_get_main_queue());
    if(!self.stateWatcher){close(watched);VIMProgressCheck(NO,@"Setup status notifications are unavailable.");}
    __weak GuestProgress *weakSelf=self;
    dispatch_source_set_event_handler(self.stateWatcher,^{[weakSelf refresh];});
    dispatch_source_set_cancel_handler(self.stateWatcher,^{close(watched);});
    dispatch_resume(self.stateWatcher);[self refresh];
    int parent=VIMProgressDirectory(self.directory);VIMProgressCheck(parent>=0,@"Invalid setup window state.");
    int control=openat(parent,"Control",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(parent);
    VIMProgressCheck(control>=0,@"Setup window controls are unavailable.");
    @try {VIMProgressWrite(control,@"Ready.plist",@{@"Visible":@YES,@"PID":@(getpid())},0600);} @finally {close(control);}
}
- (void)refresh {
    @try {
        int fd=VIMProgressDirectory(self.directory);if(fd<0)return;
        NSDictionary *state=VIMProgressRead(fd,@"State.plist",0);close(fd);if(!state)return;
        self.state=state;NSString *mode=state[@"Mode"];
        if([mode isEqual:@"close"]){self.permittedToClose=YES;[NSApp terminate:nil];return;}
        BOOL countdown=[mode isEqual:@"restart"],finished=[@[@"complete",@"failed"] containsObject:mode];
        CGFloat height=countdown||finished?242:198;
        if(fabs(NSHeight(self.window.contentView.bounds)-height)>.5) {
            NSRect frame=self.window.frame;CGFloat change=height-NSHeight(self.window.contentView.bounds);
            frame.origin.y-=change;frame.size.height+=change;[self.window setFrame:frame display:YES];
        }
        if(countdown&&!self.countdownTimer) {
            __weak GuestProgress *weakSelf=self;
            self.countdownTimer=[NSTimer timerWithTimeInterval:1 repeats:YES block:^(NSTimer *timer){(void)timer;[weakSelf refresh];}];
            [NSRunLoop.mainRunLoop addTimer:self.countdownTimer forMode:NSRunLoopCommonModes];
        } else if(!countdown) {[self.countdownTimer invalidate];self.countdownTimer=nil;}
        if(!finished&&!self.activity)self.activity=[NSProcessInfo.processInfo beginActivityWithOptions:NSActivityUserInitiated|NSActivityIdleDisplaySleepDisabled reason:@"Finishing virtual Mac setup"];
        if(finished&&self.activity){[NSProcessInfo.processInfo endActivity:self.activity];self.activity=nil;}
        self.titleLabel.stringValue=countdown?@"Restart to Finish Setup":([mode isEqual:@"failed"]?@"Setup Needs Attention":finished?@"Setup Is Ready":@"Finishing Setup");
        self.message.stringValue=state[@"Message"]?:@"Preparing your virtual Mac…";
        // Fractions currently describe phase boundaries, not measured work.
        // Native preparation/cache building can take many minutes on TCG;
        // never show a motionless 70% bar as if it were an elapsed-time estimate.
        BOOL working=!countdown&&!finished;self.progress.indeterminate=working;
        if(working)[self.progress startAnimation:nil];else {[self.progress stopAnimation:nil];self.progress.doubleValue=countdown||[mode isEqual:@"complete"]?1:0;}
        self.restart.hidden=!countdown;self.postpone.hidden=!countdown&&!finished;
        self.postpone.title=countdown?@"Postpone":@"Close";
        if(countdown&&[state[@"Deadline"] isKindOfClass:NSDate.class]) {
            NSInteger seconds=MAX(0,(NSInteger)ceil([state[@"Deadline"] timeIntervalSinceNow]));
            self.detail.font=[NSFont monospacedDigitSystemFontOfSize:12 weight:NSFontWeightRegular];
            self.detail.stringValue=[NSString stringWithFormat:@"Your virtual Mac will restart in %ld:%02ld. Save any open work.",(long)seconds/60,(long)seconds%60];
            self.detail.textColor=NSColor.labelColor;
        } else {self.detail.font=[NSFont systemFontOfSize:12];self.detail.stringValue=finished?@"You can close this window.":@"Setup may take up to 30 minutes. Keep this Mac running.";self.detail.textColor=NSColor.secondaryLabelColor;}
    } @catch(NSException *exception) {self.message.stringValue=@"Waiting for setup status…";}
}
- (void)request:(NSString *)action {
    @try {
        int fd=VIMProgressDirectory(self.directory);VIMProgressCheck(fd>=0,@"Progress state is unavailable.");
        int control=openat(fd,"Control",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(fd);
        VIMProgressCheck(control>=0,@"Restart controls are unavailable.");
        @try {VIMProgressWrite(control,@"Request.plist",@{@"Action":action,@"Revision":self.state[@"Revision"]},0600);}
        @finally {close(control);}
        self.permittedToClose=YES;[NSApp terminate:nil];
    } @catch(NSException *exception) {
        NSAlert *alert=[NSAlert new];alert.icon=self.icon;alert.messageText=@"Couldn’t Send Your Choice";alert.informativeText=@"Try again, or use Restart in the Virtual Intel Mac app.";[alert beginSheetModalForWindow:self.window completionHandler:nil];
    }
}
- (void)restartNow:(id)sender {[self request:@"restart"];}
- (void)postpone:(id)sender {if([self.state[@"Mode"] isEqual:@"restart"])[self request:@"postpone"];else {self.permittedToClose=YES;[NSApp terminate:nil];}}
- (BOOL)windowShouldClose:(NSWindow *)sender {
    if(self.permittedToClose)return YES;
    if([self.state[@"Mode"] isEqual:@"restart"]){[self request:@"postpone"];return NO;}
    if([@[@"complete",@"failed"] containsObject:self.state[@"Mode"]]){self.permittedToClose=YES;[NSApp terminate:nil];return YES;}
    if(!self.window.attachedSheet){NSAlert *alert=[NSAlert new];alert.icon=self.icon;alert.messageText=@"Setup Is Still in Progress";
        alert.informativeText=@"Keep this window and your virtual Mac open until setup finishes. Interrupting installation could leave macOS unable to start.";
        [alert addButtonWithTitle:@"Continue Setup"];[alert beginSheetModalForWindow:self.window completionHandler:nil];}return NO;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender {return [self windowShouldClose:self.window]?NSTerminateNow:NSTerminateCancel;}
- (void)applicationWillTerminate:(NSNotification *)note {
    [self.countdownTimer invalidate];self.countdownTimer=nil;
    if(self.stateWatcher){dispatch_source_cancel(self.stateWatcher);self.stateWatcher=nil;}
    if(self.activity){[NSProcessInfo.processInfo endActivity:self.activity];self.activity=nil;}
}
@end
int main(int argc,const char **argv) {@autoreleasepool {
    @try {
        if(argc==3&&!strcmp(argv[1],"--ui")) {
            VIMProgressCheck(geteuid()!=0,@"The setup window must run as the desktop user.");
            [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            __attribute__((objc_precise_lifetime)) GuestProgress *controller=[GuestProgress new];controller.directory=@(argv[2]);
            NSApp.delegate=controller;[NSApp run];return 0;
        }
        if(argc==3&&!strcmp(argv[1],"--start")){PrintJSON(StartUI(@(argv[2])));return 0;}
        if(argc==6&&!strcmp(argv[1],"--update")){VIMProgressUpdate(@(argv[2]),@(argv[3]),atof(argv[4]),@(argv[5]));PrintJSON(@{@"Updated":@YES});return 0;}
        if(argc==3&&!strcmp(argv[1],"--wait")){PrintJSON(WaitAction(@(argv[2])));return 0;}
        fprintf(stderr,"usage: guest-progress --start ICON | --update DIRECTORY MODE FRACTION MESSAGE | --wait DIRECTORY\n");return 64;
    } @catch(NSException *exception){fprintf(stderr,"%s\n",exception.reason.UTF8String);return 65;}
}}
