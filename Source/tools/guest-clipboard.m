// Non-root GUI-session side of the opt-in text clipboard bridge.
// Protocol: newline-delimited JSON, protocol 2; authenticate before any text.
#import <AppKit/AppKit.h>
#import <IOKit/IOKitLib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libproc.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <pwd.h>
#import "guest-console-state.h"

static NSDictionary *ClipboardDevices(BOOL powerOnly) {
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
}

// Provision through the authenticated root guest-agent channel, then run only
// in the user's Aqua session. No pasteboard access or saved user credentials
// in the root side; the token never appears in a process argument or log.
static BOOL RootDirectory(NSString *path) {
    if(mkdir(path.fileSystemRepresentation,0755)&&errno!=EEXIST)return NO;
    struct stat st;return lstat(path.fileSystemRepresentation,&st)==0&&S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&022);
}
static BOOL Publish(NSString *path,NSData *data,uid_t owner,mode_t mode) {
    NSString *temporary=[path.stringByDeletingLastPathComponent stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    int fd=open(temporary.fileSystemRepresentation,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return NO;
    const uint8_t *bytes=data.bytes;NSUInteger left=data.length;BOOL ok=YES;
    while(left){ssize_t n=write(fd,bytes,left);if(n<0&&errno==EINTR)continue;if(n<=0){ok=NO;break;}bytes+=n;left-=n;}
    if(fchown(fd,owner,0)||fchmod(fd,mode)||fsync(fd))ok=NO;close(fd);
    if(ok&&rename(temporary.fileSystemRepresentation,path.fileSystemRepresentation)==0)return YES;
    unlink(temporary.fileSystemRepresentation);return NO;
}
static int LaunchControl(NSArray *arguments) {
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@"/bin/launchctl"];task.arguments=arguments;
    task.standardOutput=NSFileHandle.fileHandleWithNullDevice;task.standardError=NSFileHandle.fileHandleWithNullDevice;
    if(![task launchAndReturnError:NULL])return -1;
    double deadline=NSProcessInfo.processInfo.systemUptime+10;
    while(task.running&&NSProcessInfo.processInfo.systemUptime<deadline)usleep(10000);
    if(task.running){[task terminate];return -1;}return task.terminationStatus;
}
static int Provision(NSString *path) {
    if(geteuid()!=0)return 77;
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *release=version[@"ProductVersion"];
    if(!([release hasPrefix:@"10.14."]||[release hasPrefix:@"10.15."]||[release hasPrefix:@"11."]))return 77;
    int fd=open(path.fileSystemRepresentation,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid||st.st_nlink!=1||(st.st_mode&077)||st.st_size>4096){if(fd>=0)close(fd);return 77;}
    NSMutableData *data=[NSMutableData dataWithLength:st.st_size];ssize_t count=read(fd,data.mutableBytes,data.length);close(fd);
    if(count!=st.st_size)return 74;
    id config=[NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
    if(![config isKindOfClass:NSDictionary.class])return 65;
    NSNumber *user=config[@"UID"];NSString *token=config[@"Token"];
    BOOL powerOnly=[config[@"DisplayPowerOnly"] isEqual:@YES];
    NSString *channel=powerOnly?@"display":@"clipboard";
    if(![config[@"Transport"] isEqual:@"virtio-serial"]||![user isKindOfClass:NSNumber.class]||(!powerOnly&&user.unsignedIntValue<501)||
       ![token isKindOfClass:NSString.class]||token.length!=64||[token rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789abcdef"].invertedSet].location!=NSNotFound)return 65;
    struct stat console={0};if(!powerOnly&&(stat("/dev/console",&console)||console.st_uid!=user.unsignedIntValue))return 75;
    // Metadata is not tied to a logged-in user: locks/logout can leave console
    // UID 0. A fixed unprivileged system account observes IOKit, never AppKit
    // pasteboards. Root is used only for the existing one-shot provisioning.
    struct passwd *unprivileged=powerOnly?getpwnam("nobody"):NULL;
    if(powerOnly&&(!unprivileged||!unprivileged->pw_uid))return 77;
    uid_t owner=powerOnly?unprivileged->pw_uid:user.unsignedIntValue;
    gid_t group=powerOnly?unprivileged->pw_gid:console.st_gid;
    NSDictionary *devices=ClipboardDevices(powerOnly);NSString *devicePath=devices[@"Callout"];if(!devices)return 69;
    for(NSString *directory in @[@"/Library",@"/Library/Application Support",@"/Library/Application Support/Virtual Intel Mac",@"/private/var/run/com.virtualintelmac.clipboard"])
        if(!RootDirectory(directory))return 77;
    NSString *root=@"/private/var/run/com.virtualintelmac.clipboard";
    NSString *tokenPath=[root stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.token",channel,user]];
    NSString *jobPath=[root stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.plist",channel,user]];
    NSString *executable=@"/Library/Application Support/Virtual Intel Mac/guest-clipboard";
    char selfPath[PROC_PIDPATHINFO_MAXSIZE];if(proc_pidpath(getpid(),selfPath,sizeof(selfPath))<=0)return 74;
    NSData *code=[NSData dataWithContentsOfFile:@(selfPath)];
    if(!code||!Publish(executable,code,0,0755)||!Publish(tokenPath,[token dataUsingEncoding:NSUTF8StringEncoding],owner,0600))return 74;
    NSString *label=[@"com.virtualintelmac." stringByAppendingString:channel];
    NSMutableDictionary *job=[@{@"Label":label,@"RunAtLoad":@YES,
        @"ProgramArguments":@[executable,powerOnly?@"--power-serial":@"--serial",devicePath,tokenPath],@"ProcessType":@"Interactive"} mutableCopy];
    if(powerOnly)job[@"UserName"]=@"nobody";
    NSData *plist=[NSPropertyListSerialization dataWithPropertyList:job format:NSPropertyListXMLFormat_v1_0 options:0 error:NULL];
    if(!plist||!Publish(jobPath,plist,0,0644))return 74;
    NSString *domain=powerOnly?@"system":[NSString stringWithFormat:@"gui/%@",user];
    if(powerOnly&&user.unsignedIntValue>=501)LaunchControl(@[@"bootout",[NSString stringWithFormat:@"gui/%@/%@",user,label]]);
    LaunchControl(@[@"bootout",[domain stringByAppendingFormat:@"/%@",label]]);
    // Only this fixed dedicated device is granted to its service account. The
    // privileged QGA port is never touched and no arbitrary path is accepted.
    int serial=open(devicePath.fileSystemRepresentation,O_RDWR|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat device;BOOL secured=serial>=0&&fstat(serial,&device)==0&&S_ISCHR(device.st_mode)&&
        fchown(serial,owner,group)==0&&fchmod(serial,0600)==0;
    if(serial>=0)close(serial);if(!secured)return 77;
    // The dial-in alias reaches the same serial provider. Protect it too,
    // rather than leaving a second world-writable path around the callout ACL.
    // /dev is kernel-owned; neither path comes from the incoming JSON.
    NSString *dialin=devices[@"Dialin"];struct stat alias;
    if(lstat(dialin.fileSystemRepresentation,&alias)||!S_ISCHR(alias.st_mode)||
       chown(dialin.fileSystemRepresentation,owner,group)||chmod(dialin.fileSystemRepresentation,0600))return 77;
    return LaunchControl(@[@"bootstrap",domain,jobPath])==0?0:69;
}

static const NSUInteger TextLimit=1024*1024,WireLimit=8*1024*1024;
static BOOL ActiveConsoleUser(void) {
    struct stat console;return !stat("/dev/console",&console)&&console.st_uid==getuid();
}
@interface ClipboardAgent : NSObject
@property int descriptor;
@property dispatch_source_t reader;
@property dispatch_source_t writer;
@property NSMutableData *incoming;
@property NSMutableData *outgoing;
@property NSString *token;
@property BOOL authenticated;
@property NSInteger observedChange;
@property NSUInteger localSequence;
@property NSUInteger remoteSequence;
@property NSUInteger remoteHeartbeat;
@property NSTimeInterval lastHeartbeat;
@property NSDate *deadline;
@property BOOL displayPowerOnly;
@property NSNumber *lastDisplaySleep;
@property io_service_t displayWrangler;
@property io_object_t powerNotifier;
@property IONotificationPortRef powerPort;
- (void)emitDisplayPower;
- (void)tick:(NSTimer *)timer;
@end
@implementation ClipboardAgent
- (NSDictionary *)displayPowerState {
    return CFBridgingRelease(IORegistryEntryCreateCFProperty(self.displayWrangler,CFSTR("IOPowerManagement"),NULL,0));
}
- (void)emitDisplayPower {
    if(!self.authenticated)return;
    NSDictionary *power=[self displayPowerState];
    id state=[power isKindOfClass:NSDictionary.class]?power[@"CurrentPowerState"]:nil;
    if(![state isKindOfClass:NSNumber.class])return;
    // IODisplayWrangler states 3/4 are dim/on; 0/1/2 have no usable display.
    BOOL sleeping=[state unsignedIntValue]<3;
    NSNumber *sleep=@(sleeping);
    if([sleep isEqual:self.lastDisplaySleep])return;
    self.lastDisplaySleep=sleep;[self send:@{@"type":@"display-power",@"sleeping":sleep}];
}
- (NSInteger)consoleLockState {return VIMConsoleLockState();}
- (NSPasteboard *)pasteboard {return NSPasteboard.generalPasteboard;}
- (void)stop {close(self.descriptor);exit(75);}
- (void)flush {
    NSUInteger budget=65536;
    while(budget&&self.outgoing.length) {
        ssize_t n=write(self.descriptor,self.outgoing.bytes,MIN(budget,self.outgoing.length));
        if(n<0&&errno==EINTR)continue;
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
        if(n<=0){[self stop];return;}budget-=n;
        [self.outgoing replaceBytesInRange:NSMakeRange(0,n) withBytes:NULL length:0];
    }
    if(self.outgoing.length&&!self.writer) {
        self.writer=dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE,self.descriptor,0,dispatch_get_main_queue());
        __weak ClipboardAgent *weakSelf=self;
        dispatch_source_set_event_handler(self.writer,^{[weakSelf flush];});dispatch_resume(self.writer);
    }else if(!self.outgoing.length&&self.writer){dispatch_source_cancel(self.writer);self.writer=nil;}
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
        if(![message[@"type"] isEqual:@"ready"]||![message[@"protocol"] isEqual:@2]){[self stop];return;}
        self.authenticated=YES;
        self.lastHeartbeat=NSProcessInfo.processInfo.systemUptime;
        // Do not export old pasteboard contents merely because sharing starts.
        if(self.displayPowerOnly)[self emitDisplayPower];else self.observedChange=self.pasteboard.changeCount;
        return;
    }
    if([message[@"type"] isEqual:@"stop"]){[self stop];return;}
    if([message[@"type"] isEqual:@"ping"]) {
        id sequence=message[@"sequence"];
        double value=[sequence isKindOfClass:NSNumber.class]?[sequence doubleValue]:0;
        if(value<1||value>9007199254740991.0||floor(value)!=value||value<=self.remoteHeartbeat){[self stop];return;}
        self.remoteHeartbeat=(NSUInteger)value;
        self.lastHeartbeat=NSProcessInfo.processInfo.systemUptime;
        [self send:@{@"type":@"pong",@"sequence":@(self.remoteHeartbeat)}];return;
    }
    if(self.displayPowerOnly){[self stop];return;}
    if(![message[@"type"] isEqual:@"text"]||![message[@"sequence"] isKindOfClass:NSNumber.class]||
       ![message[@"text"] isKindOfClass:NSString.class]){[self stop];return;}
    double sequence=[message[@"sequence"] doubleValue];
    if(sequence<1||sequence>9007199254740991.0||floor(sequence)!=sequence||sequence<=self.remoteSequence||
       [message[@"text"] lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>TextLimit){[self stop];return;}
    self.remoteSequence=(NSUInteger)sequence;
    // Keep heartbeats and sequence validation live while locked, but never
    // apply host text there. Consuming the sequence prevents replay on unlock.
    if([self consoleLockState]!=0)return;
    NSPasteboard *pasteboard=self.pasteboard;
    [pasteboard clearContents];[pasteboard setString:message[@"text"] forType:NSPasteboardTypeString];
    self.observedChange=pasteboard.changeCount; // suppress reflection to host
}
- (void)readAvailable {
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
}
- (void)tick:(NSTimer *)timer {
    if(!self.displayPowerOnly&&!ActiveConsoleUser())[self stop];
    if(!self.authenticated){if(self.deadline.timeIntervalSinceNow<0)[self stop];return;}
    // A host crash/backpressured close may not deliver serial EOF or the
    // best-effort stop frame. Retire this GUI helper rather than polling the
    // pasteboard forever. Normal authenticated pings arrive every 5 seconds.
    if(NSProcessInfo.processInfo.systemUptime-self.lastHeartbeat>=45){[self stop];return;}
    if(self.displayPowerOnly)return;
    NSPasteboard *pasteboard=self.pasteboard;
    if(pasteboard.changeCount==self.observedChange)return;
    self.observedChange=pasteboard.changeCount;
    // No registry query in the unchanged 200 ms idle path. Observe/discard a
    // changed count while locked/unknown, before reading any clipboard text.
    // Unlock by itself must not export content accumulated behind the lock.
    if([self consoleLockState]!=0)return;
    NSString *text=[pasteboard stringForType:NSPasteboardTypeString];
    if(!text||[text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>TextLimit)return;
    [self send:@{@"type":@"text",@"sequence":@(++self.localSequence),@"text":text}];
}
@end

static void DisplayInterest(void *context,io_service_t service,natural_t type,void *argument) {
    (void)service;(void)type;(void)argument;
    [(__bridge ClipboardAgent *)context emitDisplayPower];
}

int main(int argc,const char **argv){@autoreleasepool {
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
    agent.displayPowerOnly=powerOnly;
    if(powerOnly) {
        agent.displayWrangler=IOServiceGetMatchingService(MACH_PORT_NULL,IOServiceMatching("IODisplayWrangler"));
        agent.powerPort=IONotificationPortCreate(MACH_PORT_NULL);io_object_t notifier=0;
        if(!agent.displayWrangler||!agent.powerPort||IOServiceAddInterestNotification(agent.powerPort,agent.displayWrangler,kIOGeneralInterest,DisplayInterest,(__bridge void *)agent,&notifier))return 69;
        agent.powerNotifier=notifier;
        CFRunLoopAddSource(CFRunLoopGetMain(),IONotificationPortGetRunLoopSource(agent.powerPort),kCFRunLoopCommonModes);
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
    if(tcsetattr(agent.descriptor,TCSANOW,&settings))return 69;
    agent.reader=dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,agent.descriptor,0,dispatch_get_main_queue());
    dispatch_source_set_event_handler(agent.reader,^{[agent readAvailable];});dispatch_resume(agent.reader);
    [agent send:@{@"type":@"hello",@"protocol":@2,@"token":secret}];
    [NSTimer scheduledTimerWithTimeInterval:powerOnly?1:0.2 target:agent selector:@selector(tick:) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop run];return 0;
}}
