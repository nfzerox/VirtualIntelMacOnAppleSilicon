#import "VIMClipboardBridge.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const NSUInteger MaxText=1024*1024,MaxWire=8*1024*1024;
@interface VIMClipboardBridge () <NSStreamDelegate>
@property NSString *token;
@property NSInputStream *input;
@property NSOutputStream *output;
@property NSMutableData *incoming;
@property NSMutableData *outgoing;
@property NSTimer *timer;
@property NSDate *deadline;
@property BOOL authenticated;
@property BOOL wasAllowed;
@property NSInteger change;
@property NSUInteger sent;
@property NSUInteger received;
@property NSUInteger heartbeat;
@property NSUInteger awaitingHeartbeat;
@property NSTimeInterval lastHeartbeat;
@property NSTimeInterval heartbeatDeadline;
@end
@implementation VIMClipboardBridge
- (BOOL)isConnected {return self.authenticated;}
- (NSDictionary<NSString *,NSNumber *> *)diagnosticState {
    return @{@"SentText":@(self.sent),@"ReceivedText":@(self.received),
        @"HeartbeatSent":@(self.heartbeat),@"HeartbeatAwaiting":@(self.awaitingHeartbeat)};
}
- (BOOL)startWithSocketPath:(NSString *)path error:(NSError **)error {
    NSAssert(NSThread.isMainThread,@"Clipboard bridge is main-thread owned");
    if(self.input)return YES;
    struct sockaddr_un address={.sun_family=AF_UNIX};
    if(!path.isAbsolutePath||[path lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>=sizeof(address.sun_path)) {
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:EINVAL userInfo:nil];return NO;
    }
    strlcpy(address.sun_path,path.fileSystemRepresentation,sizeof(address.sun_path));
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    if(fd<0||fcntl(fd,F_SETFL,O_NONBLOCK)<0||fcntl(fd,F_SETFD,FD_CLOEXEC)<0||
       connect(fd,(struct sockaddr *)&address,sizeof(address))) {
        int saved=errno;if(fd>=0)close(fd);
        if(error)*error=[NSError errorWithDomain:NSPOSIXErrorDomain code:saved userInfo:nil];return NO;
    }
    CFReadStreamRef read=NULL;CFWriteStreamRef write=NULL;
    CFStreamCreatePairWithSocket(NULL,fd,&read,&write);
    if(!read||!write){if(read)CFRelease(read);if(write)CFRelease(write);close(fd);return NO;}
    CFReadStreamSetProperty(read,kCFStreamPropertyShouldCloseNativeSocket,kCFBooleanTrue);
    CFWriteStreamSetProperty(write,kCFStreamPropertyShouldCloseNativeSocket,kCFBooleanTrue);
    self.input=CFBridgingRelease(read);self.output=CFBridgingRelease(write);
    self.incoming=[NSMutableData data];self.outgoing=[NSMutableData data];
    // QEMU connects before the host finishes staging the GUI helper. Do not
    // time out legitimate TCG preparation as an unauthenticated network client.
    self.deadline=NSDate.distantFuture;
    self.token=[[[NSUUID.UUID.UUIDString stringByAppendingString:NSUUID.UUID.UUIDString]
        stringByReplacingOccurrencesOfString:@"-" withString:@""] lowercaseString];
    __weak VIMClipboardBridge *weakSelf=self;
    for(NSStream *stream in @[self.input,self.output]) {
        stream.delegate=self;[stream scheduleInRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];[stream open];
    }
    [self.timer invalidate];
    self.timer=[NSTimer timerWithTimeInterval:self.displayPowerOnly?1:0.2 repeats:YES block:^(NSTimer *timer){[weakSelf tick];}];
    [NSRunLoop.mainRunLoop addTimer:self.timer forMode:NSRunLoopCommonModes];return YES;
}
- (void)disconnect {
    NSMutableArray<NSStream *> *streams=[NSMutableArray array];
    if(self.input)[streams addObject:self.input];if(self.output)[streams addObject:self.output];
    for(NSStream *stream in streams) {
        stream.delegate=nil;[stream close];[stream removeFromRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }
    self.input=nil;self.output=nil;self.incoming=nil;self.outgoing=nil;
    self.authenticated=NO;self.wasAllowed=NO;self.sent=0;self.received=0;
    self.heartbeat=0;self.awaitingHeartbeat=0;self.lastHeartbeat=0;self.heartbeatDeadline=0;
}
- (void)fail {
    BOOL hadConnection=self.input!=nil;[self disconnect];
    // Defer owner recovery until the stream callback has finished unwinding.
    // Explicit stop clears input and never schedules recovery.
    if(hadConnection&&self.connectionLost)dispatch_async(dispatch_get_main_queue(),self.connectionLost);
}
- (void)awaitGuestHandshake {if(!self.authenticated)self.deadline=[NSDate dateWithTimeIntervalSinceNow:30];}
- (void)stop {
    // Darwin's VirtIO serial provider need not deliver EOF to the GUI helper
    // when only QEMU's host socket closes. Tell the authenticated peer to exit
    // before closing, rather than leaving its pasteboard timer running while
    // sharing is disabled. Best effort only: never block teardown or append
    // this control frame in the middle of an outstanding text frame.
    if(self.authenticated&&!self.outgoing.length&&self.output.hasSpaceAvailable) {
        static const uint8_t stop[]="{\"type\":\"stop\"}\n";
        [self.output write:stop maxLength:sizeof(stop)-1];
    }
    [self.timer invalidate];self.timer=nil;[self disconnect];
    self.token=nil;
}
- (void)dealloc { [self.timer invalidate];[self.input close];[self.output close]; }
- (void)flush {
    while(self.output.hasSpaceAvailable&&self.outgoing.length) {
        NSInteger n=[self.output write:self.outgoing.bytes maxLength:self.outgoing.length];
        if(n<0){[self fail];return;}if(!n)return;
        [self.outgoing replaceBytesInRange:NSMakeRange(0,n) withBytes:NULL length:0];
    }
}
- (void)send:(NSDictionary *)message {
    NSData *data=[NSJSONSerialization dataWithJSONObject:message options:0 error:NULL];
    if(!data||data.length+self.outgoing.length+1>MaxWire){[self fail];return;}
    [self.outgoing appendData:data];[self.outgoing appendBytes:"\n" length:1];[self flush];
}
- (BOOL)allowed {return self.exchangeAllowed&&self.exchangeAllowed();}
- (void)receive:(NSDictionary *)message {
    if(!self.authenticated) {
        if(![message[@"type"] isEqual:@"hello"]||![message[@"protocol"] isEqual:@2]||![message[@"token"] isEqual:self.token]){[self fail];return;}
        self.authenticated=YES;if(!self.displayPowerOnly)self.change=NSPasteboard.generalPasteboard.changeCount;
        self.lastHeartbeat=NSProcessInfo.processInfo.systemUptime;
        [self send:@{@"type":@"ready",@"protocol":@2}];return;
    }
    if([message[@"type"] isEqual:@"pong"]) {
        id sequence=message[@"sequence"];
        if(!self.awaitingHeartbeat||![sequence isKindOfClass:NSNumber.class]||
           [sequence doubleValue]!=(double)self.awaitingHeartbeat){[self fail];return;}
        self.awaitingHeartbeat=0;self.lastHeartbeat=NSProcessInfo.processInfo.systemUptime;return;
    }
    if(self.displayPowerOnly) {
        id asleep=message[@"sleeping"];
        if(![message[@"type"] isEqual:@"display-power"]||!asleep||CFGetTypeID((__bridge CFTypeRef)asleep)!=CFBooleanGetTypeID()) {[self fail];return;}
        if(self.displayPowerChanged)self.displayPowerChanged([asleep boolValue]);return;
    }
    id sequence=message[@"sequence"],text=message[@"text"];
    if(![message[@"type"] isEqual:@"text"]||![sequence isKindOfClass:NSNumber.class]||![text isKindOfClass:NSString.class]){[self fail];return;}
    double value=[sequence doubleValue];
    if(value<1||value>9007199254740991.0||floor(value)!=value||value<=self.received||[text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]>MaxText){[self fail];return;}
    self.received=(NSUInteger)value;
    if(![self allowed])return;
    NSPasteboard *pasteboard=NSPasteboard.generalPasteboard;
    [pasteboard clearContents];[pasteboard setString:text forType:NSPasteboardTypeString];self.change=pasteboard.changeCount;
}
- (void)tick {
    if(!self.authenticated){if(self.input&&self.deadline.timeIntervalSinceNow<0)[self fail];return;}
    // QEMU keeps a serial socket alive after the guest's GUI process exits.
    // Detect that case without polling QGA or inspecting pasteboard contents.
    // Run independently of foreground eligibility, including idle/background VMs.
    NSTimeInterval now=NSProcessInfo.processInfo.systemUptime;
    if(self.awaitingHeartbeat) {
        if(now>=self.heartbeatDeadline){[self fail];return;}
    }else if(now-self.lastHeartbeat>=5) {
        self.awaitingHeartbeat=++self.heartbeat;self.heartbeatDeadline=now+30;
        [self send:@{@"type":@"ping",@"sequence":@(self.awaitingHeartbeat)}];
        if(!self.authenticated)return;
    }
    if(self.displayPowerOnly)return;
    if(![self allowed]){self.wasAllowed=NO;return;}
    NSPasteboard *pasteboard=NSPasteboard.generalPasteboard;
    // Keep the baseline while another app is active, so copying there and
    // returning to this VM works. Still never export pre-connection contents.
    self.wasAllowed=YES;
    if(self.change==pasteboard.changeCount)return;self.change=pasteboard.changeCount;
    NSString *text=[pasteboard stringForType:NSPasteboardTypeString];
    if(text&&[text lengthOfBytesUsingEncoding:NSUTF8StringEncoding]<=MaxText)
        [self send:@{@"type":@"text",@"sequence":@(++self.sent),@"text":text}];
}
- (void)stream:(NSStream *)stream handleEvent:(NSStreamEvent)event {
    if(event&(NSStreamEventErrorOccurred|NSStreamEventEndEncountered)){[self fail];return;}
    if(event==NSStreamEventHasSpaceAvailable){[self flush];return;}
    if(event!=NSStreamEventHasBytesAvailable)return;
    uint8_t buffer[16384];NSInteger n=[self.input read:buffer maxLength:sizeof(buffer)];
    if(n<=0){[self fail];return;}[self.incoming appendBytes:buffer length:n];
    if(self.incoming.length>MaxWire){[self fail];return;}
    while(self.incoming.length) {
        const uint8_t *start=self.incoming.bytes,*end=memchr(start,'\n',self.incoming.length);if(!end)break;
        NSUInteger length=end-start;NSData *line=[self.incoming subdataWithRange:NSMakeRange(0,length)];
        [self.incoming replaceBytesInRange:NSMakeRange(0,length+1) withBytes:NULL length:0];
        id message=[NSJSONSerialization JSONObjectWithData:line options:0 error:NULL];
        if(![message isKindOfClass:NSDictionary.class]){[self fail];return;}[self receive:message];
    }
}
@end
