#import "VIMDisplayConnection.h"
#import "VIMMetalView.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

// Matches QEMU hw/display/apple-gfx-app-wire.h, private same-host ABI v1.
typedef struct {
    uint32_t magic, operation;
    uint64_t sequence;
    uint32_t width, height, stride, reserved;
} VIMDisplayMessage;
_Static_assert(sizeof(VIMDisplayMessage) == 32, "Display wire ABI");
static const uint32_t VIMDisplayMagic = 0x56494d31;
static BOOL VIMTransfer(int fd, void *bytes, size_t length, BOOL writing) {
    uint8_t *cursor = bytes;
    while (length) {
        ssize_t n = writing ? write(fd, cursor, length) : read(fd, cursor, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return NO;
        cursor += n; length -= n;
    }
    return YES;
}
static NSError *VIMDisplayError(NSString *message) {
    return [NSError errorWithDomain:@"VirtualIntelMac.Display" code:3
        userInfo:@{NSLocalizedDescriptionKey:message}];
}
@interface VIMDisplayConnection ()
@property(nonatomic) NSString *socketPath;
@property(nonatomic) NSString *directory;
@property(nonatomic) dispatch_source_t listener;
@property(nonatomic) NSLock *lock;
@property(nonatomic) int peer;
@property(nonatomic) BOOL stopped;
@property(nonatomic, weak) VIMMetalView *view;
@property(nonatomic, copy) void (^failure)(NSError *);
@property(nonatomic) dispatch_queue_t writer;
@property(nonatomic) NSUInteger requestedWidth;
@property(nonatomic) NSUInteger requestedHeight;
@property(nonatomic) uint32_t powerEpoch;
@end

@implementation VIMDisplayConnection
- (instancetype)initWithView:(VIMMetalView *)view failure:(void (^)(NSError *))failure error:(NSError **)error {
    NSAssert(NSThread.isMainThread, @"Main-thread lifecycle");
    if (!(self = [super init])) return nil;
    _peer = -1; _lock = [NSLock new]; _view = view; _failure = [failure copy];
    _writer = dispatch_queue_create("com.mac.virtualintelmac.display.requests", DISPATCH_QUEUE_SERIAL);
    // Short path avoids sockaddr_un's 104-byte limit even with long home paths.
    char directory[] = "/tmp/vim-display.XXXXXX";
    if (!mkdtemp(directory)) {
        if (error) *error = VIMDisplayError(@"Could not create a private display connection.");
        return nil;
    }
    _directory = @(directory); _socketPath = [_directory stringByAppendingPathComponent:@"display"];
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address = {.sun_family=AF_UNIX};
    strlcpy(address.sun_path, _socketPath.fileSystemRepresentation, sizeof(address.sun_path));
    if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 1)) {
        if (fd >= 0) close(fd);
        unlink(_socketPath.fileSystemRepresentation); rmdir(directory);
        if (error) *error = VIMDisplayError(@"Could not listen for the guest display.");
        return nil;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, O_NONBLOCK);
    _listener = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, fd, 0,
        dispatch_get_main_queue());
    __weak VIMDisplayConnection *weakSelf = self;
    dispatch_source_set_event_handler(_listener, ^{ [weakSelf acceptFrom:fd]; });
    dispatch_source_set_cancel_handler(_listener, ^{ close(fd); });
    dispatch_resume(_listener);
    return self;
}
- (void)acceptFrom:(int)listener {
    int fd = accept(listener, NULL, NULL);
    if (fd < 0) return;
    uid_t uid; gid_t gid;
    if (getpeereid(fd, &uid, &gid) || uid != getuid()) { close(fd); return; }
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, 0);
    int one = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
    struct timeval sendTimeout = {.tv_sec=2};
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
    [self.lock lock];
    if (self.stopped || self.peer >= 0) { [self.lock unlock]; close(fd); return; }
    self.peer = fd; [self.lock unlock];
    dispatch_source_cancel(self.listener); self.listener = nil;
    unlink(self.socketPath.fileSystemRepresentation);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        NSError *failure = [self receiveFrames:fd];
        [self.lock lock];
        self.peer = -1; close(fd); BOOL stopped = self.stopped;
        [self.lock unlock];
        if (!stopped) dispatch_async(dispatch_get_main_queue(), ^{
            if (!self.stopped) { [self stop]; self.failure(failure); }
        });
    });
}
- (NSError *)receiveFrames:(int)fd {
    struct timeval handshakeTimeout = {.tv_sec=30};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &handshakeTimeout, sizeof(handshakeTimeout));
    VIMDisplayMessage hello = {0};
    // Room for multiple descriptors so malformed peers cannot leak received FDs.
    char control[CMSG_SPACE(sizeof(int) * 8)] = {0};
    struct iovec iov = {&hello, sizeof(hello)};
    struct msghdr message = {.msg_iov=&iov, .msg_iovlen=1,
        .msg_control=control, .msg_controllen=sizeof(control)};
    ssize_t n;
    do { n = recvmsg(fd, &message, 0); } while (n < 0 && errno == EINTR);
    int frameFD = -1; unsigned descriptors = 0;
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&message); n > 0 && c; c = CMSG_NXTHDR(&message,c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS || c->cmsg_len < CMSG_LEN(0)) continue;
        size_t count = (c->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int *fds = (int *)CMSG_DATA(c);
        for (size_t i=0; i<count; i++) {
            if (++descriptors == 1) frameFD = fds[i]; else close(fds[i]);
        }
    }
    BOOL valid = n > 0 && !(message.msg_flags & (MSG_CTRUNC | MSG_TRUNC)) && descriptors == 1;
    if (valid && (size_t)n < sizeof(hello)) valid = VIMTransfer(fd, (char *)&hello+n, sizeof(hello)-n, NO);
    valid = valid && hello.magic == VIMDisplayMagic && hello.operation == 1 &&
        !hello.sequence && !hello.width && !hello.height && !hello.stride && !hello.reserved;
    if (!valid) {
        if (frameFD >= 0) close(frameFD);
        return VIMDisplayError(@"The guest display sent an invalid connection header.");
    }
    // A guest may remain idle indefinitely; only the handshake has a deadline.
    struct timeval noTimeout = {0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &noTimeout, sizeof(noTimeout));
    fcntl(frameFD, F_SETFD, FD_CLOEXEC);
    __block NSError *installationError = nil;
    __block BOOL installed = NO;
    dispatch_sync(dispatch_get_main_queue(), ^{
        VIMMetalView *view = self.view;
        if (!self.stopped && view) installed = [view installFrameDescriptor:frameFD
            length:8192ULL*8192*4 error:&installationError];
        else close(frameFD);
    });
    if (!installed) return installationError ?: VIMDisplayError(@"The guest display was closed.");
    dispatch_async(dispatch_get_main_queue(), ^{
        if(self.requestedWidth)[self requestWidth:self.requestedWidth height:self.requestedHeight];
    });
    uint64_t sequence = 0;
    VIMDisplayMessage frame;
    while (VIMTransfer(fd, &frame, sizeof(frame), NO)) {
        if(frame.magic==VIMDisplayMagic&&frame.operation==7&&frame.sequence&&frame.sequence<=UINT32_MAX&&
           frame.width<=1&&!frame.height&&!frame.stride&&!frame.reserved) {
            VIMDisplayMessage power=frame;
            dispatch_async(dispatch_get_main_queue(),^{
                if(self.stopped||power.sequence<=self.powerEpoch)return;
                self.powerEpoch=(uint32_t)power.sequence;
                self.view.guestDisplaySleeping=power.width!=0;
            });continue;
        }
        if(frame.magic==VIMDisplayMagic && frame.operation==4 && !frame.sequence &&
            frame.width<=1 && !frame.height && !frame.stride && !frame.reserved) {
            dispatch_async(dispatch_get_main_queue(), ^{ if(!self.stopped)[self.view setGuestCursorVisible:frame.width!=0]; });
            continue;
        }
        if(frame.magic==VIMDisplayMagic && frame.operation==5 && !frame.sequence && !frame.reserved &&
            frame.width && frame.width<=256 && frame.height && frame.height<=256 &&
            (frame.stride&0xffff)<frame.width && (frame.stride>>16)<frame.height) {
            NSMutableData *pixels=[NSMutableData dataWithLength:frame.width*frame.height*4];
            if(!VIMTransfer(fd,pixels.mutableBytes,pixels.length,NO))break;
            dispatch_async(dispatch_get_main_queue(), ^{
                if(!self.stopped)[self.view setGuestCursorPixels:pixels width:frame.width height:frame.height
                    hotspot:NSMakePoint(frame.stride&0xffff,frame.stride>>16)];
            });
            continue;
        }
        if (frame.magic != VIMDisplayMagic || (frame.operation != 2 && frame.operation != 8) ||
            frame.sequence <= sequence || (frame.operation==2&&frame.reserved) || frame.width < 320 || frame.width > 8192 ||
            frame.height < 200 || frame.height > 8192 || frame.stride < frame.width*4 ||
            frame.stride % 256 || frame.stride > (8192ULL*8192*4)/frame.height)
            return VIMDisplayError(@"The guest display sent an invalid frame.");
        sequence = frame.sequence;
        dispatch_semaphore_t completed = dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_main_queue(), ^{
            VIMMetalView *view = self.view;
            BOOL current=frame.operation==2?self.powerEpoch==0:frame.reserved==self.powerEpoch;
            if (self.stopped || !view || !current) { dispatch_semaphore_signal(completed); return; }
            [view presentWidth:frame.width height:frame.height stride:frame.stride completion:^(BOOL presented) {
                dispatch_semaphore_signal(completed);
            }];
        });
        dispatch_semaphore_wait(completed, DISPATCH_TIME_FOREVER);
        VIMDisplayMessage ack = {.magic=VIMDisplayMagic, .operation=3, .sequence=sequence};
        if (![self sendMessage:ack]) break;
    }
    return VIMDisplayError(@"The guest display connection ended.");
}
- (BOOL)sendMessage:(VIMDisplayMessage)message {
    // Lock covers short writes and close. No main-thread caller performs I/O.
    [self.lock lock];
    BOOL sent=!self.stopped && self.peer>=0 && VIMTransfer(self.peer,&message,sizeof(message),YES);
    if(!sent && self.peer>=0)shutdown(self.peer,SHUT_RDWR);
    [self.lock unlock];return sent;
}
- (void)requestWidth:(NSUInteger)width height:(NSUInteger)height {
    NSAssert(NSThread.isMainThread,@"Main-thread lifecycle");
    if(width<320||height<200||width>8192||height>8192||self.stopped)return;
    self.requestedWidth=width;self.requestedHeight=height;
    dispatch_async(self.writer, ^{
        [self.lock lock];BOOL connected=self.peer>=0;[self.lock unlock];
        if(!connected)return;
        VIMDisplayMessage message={.magic=VIMDisplayMagic,.operation=6,.width=(uint32_t)width,.height=(uint32_t)height};
        [self sendMessage:message];
    });
}
- (void)requestDisplaySleeping:(BOOL)sleeping {
    NSAssert(NSThread.isMainThread,@"Main-thread lifecycle");
    if(self.stopped)return;
    dispatch_async(self.writer,^{
        VIMDisplayMessage message={.magic=VIMDisplayMagic,.operation=9,.width=sleeping};
        [self sendMessage:message];
    });
}
- (void)stop {
    NSAssert(NSThread.isMainThread, @"Main-thread lifecycle");
    [self.lock lock]; self.stopped = YES;
    // Reader owns close. Shutdown wakes it without an FD-reuse race.
    if (self.peer >= 0) shutdown(self.peer, SHUT_RDWR);
    [self.lock unlock];
    if (self.listener) { dispatch_source_cancel(self.listener); self.listener = nil; }
    unlink(self.socketPath.fileSystemRepresentation); rmdir(self.directory.fileSystemRepresentation);
}
- (void)dealloc {
    if (_listener) dispatch_source_cancel(_listener);
    if (_socketPath) unlink(_socketPath.fileSystemRepresentation);
    if (_directory) rmdir(_directory.fileSystemRepresentation);
}
@end
