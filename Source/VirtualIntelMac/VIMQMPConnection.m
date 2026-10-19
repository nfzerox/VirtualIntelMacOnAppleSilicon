#import "VIMQMPConnection.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static NSError *VIMQMPError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.Control" code:1
        userInfo:@{NSLocalizedDescriptionKey:message}];
}
@interface VIMQMPConnection ()
@property NSString *path;
@property dispatch_queue_t queue;
@property NSLock *lock;
@property int descriptor;
@property BOOL stopped;
@property BOOL ready;
@property NSMutableData *buffer;
@property uint64_t sequence;
@property BOOL guestAgent;
@property BOOL awaitingSentinel;
@property double responseDeadline;
@end
@implementation VIMQMPConnection
- (instancetype)initWithSocketPath:(NSString *)path {
    if ((self=[super init])) {
        _path=[path copy]; _descriptor=-1; _lock=[NSLock new]; _buffer=[NSMutableData data];
        _queue=dispatch_queue_create("com.mac.virtualintelmac.control",DISPATCH_QUEUE_SERIAL);
    }
    return self;
}
- (instancetype)initWithGuestAgentSocketPath:(NSString *)path {
    if((self=[self initWithSocketPath:path]))_guestAgent=YES;
    return self;
}
- (void)connectWithCompletion:(void (^)(NSError *))completion {
    dispatch_async(self.queue, ^{
        NSError *error=nil;
        if (!self.ready) {
            struct sockaddr_un address={.sun_family=AF_UNIX};
            if (strlen(self.path.fileSystemRepresentation)>=sizeof(address.sun_path)) {
                error=VIMQMPError(@"The control connection path is too long.");
            } else {
                strlcpy(address.sun_path,self.path.fileSystemRepresentation,sizeof(address.sun_path));
                // QEMU creates this socket shortly after process launch. Bounded retry,
                // off the UI thread; close cancels the retry without starting another VM.
                for (unsigned attempt=0;attempt<100;attempt++) {
                    [self.lock lock]; BOOL stopped=self.stopped; [self.lock unlock];
                    if(stopped) break;
                    int fd=socket(AF_UNIX,SOCK_STREAM,0);
                    if(fd<0) break;
                    if(!connect(fd,(struct sockaddr *)&address,sizeof(address))) {
                        uid_t uid;gid_t gid;
                        if(getpeereid(fd,&uid,&gid)||uid!=getuid()) { close(fd);break; }
                        fcntl(fd,F_SETFD,FD_CLOEXEC);
                        struct timeval timeout={.tv_sec=5};int one=1;
                        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
                        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
                        setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
                        [self.lock lock];
                        if(self.stopped) close(fd); else self.descriptor=fd;
                        [self.lock unlock]; break;
                    }
                    close(fd); usleep(100000);
                }
                if(self.descriptor<0) error=VIMQMPError(@"The virtual machine’s control connection did not become available.");
                else {
                    if(self.guestAgent) {
                        uint64_t nonce=0;arc4random_buf(&nonce,sizeof(nonce));
                        NSNumber *token=@(nonce & INT64_MAX);
                        self.awaitingSentinel=YES;
                        id reply=[self request:@"guest-sync-delimited" arguments:@{@"id":token} error:&error];
                        if(!error && ![reply isEqual:token])error=VIMQMPError(@"Guest-agent synchronization failed.");
                    } else {
                        self.responseDeadline=NSProcessInfo.processInfo.systemUptime+5;
                        NSDictionary *greeting=[self readObject:&error];
                        if(greeting && ![greeting[@"QMP"] isKindOfClass:NSDictionary.class])
                            error=VIMQMPError(@"The virtual machine sent an invalid control greeting.");
                        if(!error)[self request:@"qmp_capabilities" arguments:@{} error:&error];
                    }
                    self.ready=error==nil;
                }
            }
        }
        if(error) [self discardDescriptor];
        dispatch_async(dispatch_get_main_queue(), ^{ completion(error); });
    });
}
- (NSDictionary *)readObject:(NSError **)error {
    while(YES) {
        const uint8_t *bytes=self.buffer.bytes;
        if(self.awaitingSentinel) {
            const uint8_t *sentinel=memchr(bytes,0xff,self.buffer.length);
            if(sentinel) {
                [self.buffer replaceBytesInRange:NSMakeRange(0,sentinel-bytes+1) withBytes:NULL length:0];
                self.awaitingSentinel=NO;continue;
            }
        }
        const uint8_t *newline=self.awaitingSentinel?NULL:memchr(bytes,'\n',self.buffer.length);
        if(newline) {
            NSUInteger length=newline-bytes+1;
            NSData *line=[self.buffer subdataWithRange:NSMakeRange(0,length)];
            [self.buffer replaceBytesInRange:NSMakeRange(0,length) withBytes:NULL length:0];
            id object=[NSJSONSerialization JSONObjectWithData:line options:0 error:error];
            if(![object isKindOfClass:NSDictionary.class]) {
                if(error) *error=VIMQMPError(@"Invalid control response.");return nil;
            }
            return object;
        }
        if(self.buffer.length>=1024*1024) {
            if(error)*error=VIMQMPError(@"The control response exceeded its size limit.");return nil;
        }
        uint8_t chunk[4096];ssize_t n;
        double left=self.responseDeadline-NSProcessInfo.processInfo.systemUptime;
        if(left<=0) { if(error)*error=VIMQMPError(@"The control response timed out.");return nil; }
        struct timeval timeout={.tv_sec=(time_t)left,.tv_usec=(suseconds_t)((left-(time_t)left)*1000000)};
        if(!timeout.tv_sec&&!timeout.tv_usec)timeout.tv_usec=1;
        setsockopt(self.descriptor,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        do { n=read(self.descriptor,chunk,sizeof(chunk)); } while(n<0&&errno==EINTR);
        if(n<=0) { if(error)*error=VIMQMPError(@"The control connection ended or timed out.");return nil; }
        [self.buffer appendBytes:chunk length:n];
    }
}
- (id)request:(NSString *)command arguments:(NSDictionary *)arguments error:(NSError **)error {
    // Large staging writes execute inside a potentially busy emulated guest.
    // Other control operations retain their short responsiveness deadline.
    self.responseDeadline=NSProcessInfo.processInfo.systemUptime+
        ((self.guestAgent&&[command isEqual:@"guest-file-write"])?30:5);
    NSNumber *identifier=@(++self.sequence);
    NSData *json=[NSJSONSerialization dataWithJSONObject:@{@"execute":command,@"arguments":arguments,@"id":identifier}
        options:0 error:error];
    if(!json)return nil;
    NSMutableData *wire=[NSMutableData data];
    if(self.guestAgent&&[command isEqual:@"guest-sync-delimited"]) {
        const uint8_t sentinel=0xff;[wire appendBytes:&sentinel length:1];
    }
    [wire appendData:json];[wire appendBytes:"\n" length:1];
    const uint8_t *bytes=wire.bytes;size_t remaining=wire.length;
    while(remaining) {
        ssize_t n=write(self.descriptor,bytes,remaining);
        if(n<0&&errno==EINTR)continue;
        if(n<=0) { if(error)*error=VIMQMPError(@"Could not send a virtual machine command.");return nil; }
        bytes+=n;remaining-=n;
    }
    for(unsigned events=0;events<1024;events++) {
        NSDictionary *response=[self readObject:error];if(!response)return nil;
        if(response[@"event"]) {
            if(!self.guestAgent&&[@[@"RESET",@"SHUTDOWN",@"SUSPEND",@"WAKEUP",@"GUEST_PANICKED",@"WATCHDOG",@"DEVICE_TRAY_MOVED"] containsObject:response[@"event"]]) {
                void (^handler)(NSDictionary *)=self.lifecycleEventHandler;
                if(handler)dispatch_async(dispatch_get_main_queue(),^{handler(response);});
            }
            continue;
        }
        // A stopped/busy emulator can answer after the caller's deadline. That
        // command has an uncertain outcome and must not be replayed, but its
        // obsolete reply must not poison every subsequent request either.
        // QMP echoes our monotonically increasing IDs. Discard only older
        // positive integral IDs; future/missing/malformed IDs still fail below. QGA's
        // legacy no-ID transport retains its separate resynchronization rules.
        if(!self.guestAgent&&[response[@"id"] isKindOfClass:NSNumber.class]&&
           CFGetTypeID((__bridge CFTypeRef)response[@"id"])!=CFBooleanGetTypeID()&&
           [response[@"id"] unsignedLongLongValue]>0&&
           [response[@"id"] isEqual:@([response[@"id"] unsignedLongLongValue])]&&
           [response[@"id"] compare:identifier]==NSOrderedAscending)continue;
        if(self.guestAgent&&[command isEqual:@"guest-sync-delimited"]&&
           ![response[@"return"] isEqual:arguments[@"id"]])continue;
        if(![response[@"id"] isEqual:identifier]&&!(self.guestAgent&&!response[@"id"])) {
            if(error)*error=VIMQMPError(@"An unexpected control response was received.");return nil;
        }
        if(response[@"error"]) {
            id detail=[response[@"error"] isKindOfClass:NSDictionary.class]?response[@"error"][@"desc"]:nil;
            if(error)*error=[detail isEqual:@"VIMInputQueueFull"]?
                [NSError errorWithDomain:@"VirtualIntelMac.Control" code:2
                    userInfo:@{NSLocalizedDescriptionKey:@"The guest keyboard is busy."}]:
                VIMQMPError([detail isKindOfClass:NSString.class]?detail:@"The virtual machine rejected this command.");
            return nil;
        }
        if(!response[@"return"]) { if(error)*error=VIMQMPError(@"The control response was incomplete.");return nil; }
        return response[@"return"];
    }
    if(error)*error=VIMQMPError(@"Too many unsolicited control events.");return nil;
}
- (void)execute:(NSString *)command arguments:(NSDictionary *)arguments
    completion:(void (^)(id,NSError *))completion {
    dispatch_async(self.queue, ^{
        NSError *error=nil;id result=nil;
        if(!self.ready)error=VIMQMPError(@"The virtual machine is not ready for commands.");
        else {
            // The emulator rejects a full keyboard batch before dispatching any
            // of it. Keep ordering on this serial queue and never block AppKit.
            double deadline=NSProcessInfo.processInfo.systemUptime+5.0;
            do {
                error=nil;
                result=[self request:command arguments:arguments error:&error];
                if(![command isEqual:@"input-send-event"]||error.code!=2||
                   ![error.domain isEqual:@"VirtualIntelMac.Control"])break;
                [self.lock lock];BOOL stopped=self.stopped;[self.lock unlock];
                if(stopped||NSProcessInfo.processInfo.systemUptime>=deadline)break;
                usleep(10000);
            } while(YES);
            // A timed-out QGA stream must synchronize again before reuse.
            if(error&&self.guestAgent)[self discardDescriptor];
        }
        dispatch_async(dispatch_get_main_queue(), ^{ completion(result,error); });
    });
}
- (void)drainLifecycleEventsAfterExitWithCompletion:(void (^)(void))completion {
    dispatch_async(self.queue,^{
        if(!self.guestAgent&&self.descriptor>=0) {
            self.responseDeadline=NSProcessInfo.processInfo.systemUptime+.5;
            for(NSUInteger i=0;i<4096;i++) {
                NSDictionary *event=[self readObject:NULL];if(!event)break;
                void (^handler)(NSDictionary *)=self.lifecycleEventHandler;
                if([event[@"event"] isKindOfClass:NSString.class]&&handler)
                    dispatch_async(dispatch_get_main_queue(),^{handler(event);});
            }
        }
        // Same serial/main queues as regular request dispatch: every observed
        // SHUTDOWN reason reaches the owner before its termination decision.
        dispatch_async(dispatch_get_main_queue(),completion);
    });
}
- (void)discardDescriptor {
    [self.lock lock];
    if(self.descriptor>=0) { close(self.descriptor);self.descriptor=-1; }
    self.ready=NO;[self.buffer setLength:0];[self.lock unlock];
}
- (void)close {
    [self.lock lock];self.stopped=YES;
    if(self.descriptor>=0)shutdown(self.descriptor,SHUT_RDWR);
    [self.lock unlock];
    dispatch_async(self.queue, ^{ [self discardDescriptor]; });
}
- (void)dealloc { if(_descriptor>=0)close(_descriptor); }
@end
