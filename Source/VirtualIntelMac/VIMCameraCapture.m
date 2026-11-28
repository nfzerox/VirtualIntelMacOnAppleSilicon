#import "VIMCameraCapture.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static uint8_t VIMCameraByte(int value) {
    if(value<0)return 0;
    if(value>255)return 255;
    return (uint8_t)value;
}
static void VIMCameraYUY2Sample(uint8_t r,uint8_t g,uint8_t b,uint8_t *y,uint8_t *u,uint8_t *v) {
    *y=VIMCameraByte(((66*r+129*g+25*b+128)/256)+16);
    *u=VIMCameraByte(((-38*r-74*g+112*b+128)/256)+128);
    *v=VIMCameraByte(((112*r-94*g-18*b+128)/256)+128);
}
NSData *VIMCameraYUY2FromBGRA(const uint8_t *pixels,NSInteger width,NSInteger height,NSInteger bytesPerRow) {
    if(!pixels||width<1||height<1||bytesPerRow<width*4)return nil;
    NSMutableData *frame=[NSMutableData dataWithLength:VIM_CAMERA_FRAME_BYTES];
    uint8_t *out=frame.mutableBytes;
    for(NSInteger y=0;y<VIM_CAMERA_HEIGHT;y++) {
        NSInteger sourceY=y*height/VIM_CAMERA_HEIGHT;
        const uint8_t *row=pixels+sourceY*bytesPerRow;
        for(NSInteger x=0;x<VIM_CAMERA_WIDTH;x+=2) {
            NSInteger sourceX0=x*width/VIM_CAMERA_WIDTH,sourceX1=(x+1)*width/VIM_CAMERA_WIDTH;
            const uint8_t *p0=row+sourceX0*4,*p1=row+sourceX1*4;
            uint8_t y0,y1,u,v;
            VIMCameraYUY2Sample(p0[2],p0[1],p0[0],&y0,&u,&v);
            VIMCameraYUY2Sample(p1[2],p1[1],p1[0],&y1,&u,&v);
            out[0]=y0;out[1]=u;out[2]=y1;out[3]=v;out+=4;
        }
    }
    return frame;
}

@class VIMCameraSampleSink;
@interface VIMCameraCapture () {
    VIMCameraStatus status;
    VIMCameraSlots slots;
    uint8_t wasStreaming[VIM_CAMERA_MAX_FUNCTIONS];
    unsigned preferred;
    unsigned functions;
    BOOL autosense;
    BOOL testPattern;
    BOOL enabled;
    BOOL autosenseActive;
    BOOL driverLoaded;
    unsigned patternPhase;
    int fd;
}
@property dispatch_queue_t queue;
@property dispatch_queue_t samples;
@property dispatch_source_t reader;
@property dispatch_source_t writer;
@property dispatch_source_t pattern;
@property NSMutableData *pending;
@property NSString *socketPath;
@property(atomic) BOOL stopped;
@property NSMutableDictionary<NSNumber *,AVCaptureSession *> *sessions;
@property NSMutableDictionary<NSNumber *,VIMCameraSampleSink *> *sinks;
@property NSMutableDictionary<NSNumber *,NSArray *> *observers;
@property(copy) void (^log)(NSString *message);
@property(copy) void (^guestOpened)(NSString *uid);
- (void)deliverFrame:(NSData *)frame slot:(unsigned)slot;
@end

@interface VIMCameraSampleSink : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(weak) VIMCameraCapture *owner;
@property unsigned slot;
@property CFAbsoluteTime previous;
@end
@implementation VIMCameraSampleSink
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sample fromConnection:(AVCaptureConnection *)connection {
    (void)output;(void)connection;
    CFAbsoluteTime now=CFAbsoluteTimeGetCurrent();
    unsigned fps=self.owner.frameRate?:VIM_CAMERA_FPS;
    if(self.previous&&now-self.previous<1.0/fps)return;
    CVPixelBufferRef image=CMSampleBufferGetImageBuffer(sample);
    if(!image||CVPixelBufferGetPixelFormatType(image)!=kCVPixelFormatType_32BGRA)return;
    if(CVPixelBufferLockBaseAddress(image,kCVPixelBufferLock_ReadOnly)!=kCVReturnSuccess)return;
    NSData *frame=VIMCameraYUY2FromBGRA(CVPixelBufferGetBaseAddress(image),(NSInteger)CVPixelBufferGetWidth(image),
        (NSInteger)CVPixelBufferGetHeight(image),(NSInteger)CVPixelBufferGetBytesPerRow(image));
    CVPixelBufferUnlockBaseAddress(image,kCVPixelBufferLock_ReadOnly);
    if(frame.length!=VIM_CAMERA_FRAME_BYTES)return;
    self.previous=now;
    [self.owner deliverFrame:frame slot:self.slot];
}
@end


@implementation VIMCameraCapture
- (instancetype)init {
    if((self=[super init])){
        fd=-1;
        functions=1;
        self.queue=dispatch_queue_create("com.mac.virtualintelmac.camera",DISPATCH_QUEUE_SERIAL);
        self.samples=dispatch_queue_create("com.mac.virtualintelmac.camera.samples",DISPATCH_QUEUE_SERIAL);
        self.sessions=[NSMutableDictionary new];
        self.sinks=[NSMutableDictionary new];
        self.observers=[NSMutableDictionary new];
        vim_camera_slots_init(&slots);
        vim_camera_status_init(&status,VIM_CAMERA_MAX_FUNCTIONS);
    }
    return self;
}
- (void)note:(NSString *)message {
    void (^log)(NSString *)=self.log;
    if(log)dispatch_async(dispatch_get_main_queue(),^{log(message);});
}

#pragma mark Socket

- (void)closeSocket {
    if(self.writer){dispatch_source_cancel(self.writer);self.writer=nil;}
    if(self.reader){dispatch_source_cancel(self.reader);self.reader=nil;}
    else if(fd>=0)close(fd);
    fd=-1;
    self.pending=nil;
}
- (void)reconnectLater {
    if(self.stopped)return;
    __weak VIMCameraCapture *weakSelf=self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,(int64_t)NSEC_PER_SEC),self.queue,^{[weakSelf openSocket];});
}
- (void)disconnect {
    [self closeSocket];
    vim_camera_status_init(&status,functions);
    [self applyStatus];
    [self reconnectLater];
}
- (void)openSocket {
    if(self.stopped||fd>=0||!self.socketPath.length)return;
    struct sockaddr_un address={0};
    if(self.socketPath.length>=sizeof address.sun_path)return;
    address.sun_family=AF_UNIX;
    [self.socketPath getFileSystemRepresentation:address.sun_path maxLength:sizeof address.sun_path];
    int socketFD=socket(AF_UNIX,SOCK_STREAM,0);
    if(socketFD<0){[self reconnectLater];return;}
    int on=1;
    setsockopt(socketFD,SOL_SOCKET,SO_NOSIGPIPE,&on,sizeof on);
    if(connect(socketFD,(struct sockaddr *)&address,sizeof address)!=0){close(socketFD);[self reconnectLater];return;}
    fcntl(socketFD,F_SETFL,fcntl(socketFD,F_GETFL)|O_NONBLOCK);
    fd=socketFD;
    __weak VIMCameraCapture *weakSelf=self;
    dispatch_source_t reader=dispatch_source_create(DISPATCH_SOURCE_TYPE_READ,(uintptr_t)socketFD,0,self.queue);
    dispatch_source_set_event_handler(reader,^{
        VIMCameraCapture *capture=weakSelf;
        uint8_t buffer[64];
        ssize_t got=read(socketFD,buffer,sizeof buffer);
        if(got>0){[capture consumeStatus:buffer length:(size_t)got];return;}
        if(got<0&&(errno==EAGAIN||errno==EINTR))return;
        if(capture&&capture->fd==socketFD)[capture disconnect];
    });
    dispatch_source_set_cancel_handler(reader,^{close(socketFD);});
    dispatch_resume(reader);
    self.reader=reader;
    [self publishDirectory];
    [self applyStatus];
}
- (BOOL)send:(NSData *)bytes {
    if(fd<0)return NO;
    if(self.pending.length){[self.pending appendData:bytes];return YES;}
    const uint8_t *cursor=bytes.bytes;size_t left=bytes.length;
    while(left) {
        ssize_t wrote=write(fd,cursor,left);
        if(wrote>0){cursor+=wrote;left-=(size_t)wrote;continue;}
        if(wrote<0&&errno==EINTR)continue;
        if(wrote<0&&errno==EAGAIN)break;
        [self disconnect];return NO;
    }
    if(!left)return YES;
    self.pending=[NSMutableData dataWithBytes:cursor length:left];
    __weak VIMCameraCapture *weakSelf=self;
    dispatch_source_t writer=dispatch_source_create(DISPATCH_SOURCE_TYPE_WRITE,(uintptr_t)fd,0,self.queue);
    dispatch_source_set_event_handler(writer,^{[weakSelf drain];});
    dispatch_resume(writer);
    self.writer=writer;
    return YES;
}
- (void)drain {
    while(self.pending.length&&fd>=0) {
        ssize_t wrote=write(fd,self.pending.bytes,self.pending.length);
        if(wrote>0){[self.pending replaceBytesInRange:NSMakeRange(0,(NSUInteger)wrote) withBytes:NULL length:0];continue;}
        if(wrote<0&&(errno==EAGAIN||errno==EINTR))return;
        [self disconnect];return;
    }
    if(self.writer){dispatch_source_cancel(self.writer);self.writer=nil;}
    self.pending=nil;
}
- (void)publishDirectory {
    uint8_t message[2048];
    int wrote=vim_camera_directory_write(message,sizeof message,&slots,autosense,preferred);
    if(wrote>0&&fd>=0)[self send:[NSData dataWithBytes:message length:(NSUInteger)wrote]];
}

#pragma mark Frames

- (void)deliverFrame:(NSData *)frame slot:(unsigned)slot {
    dispatch_async(self.queue,^{
        if(self.stopped||fd<0||self.pending.length)return;
        int function=vim_camera_function_for_slot(slot,preferred,functions);
        if(function<0)return;
        uint8_t header[VIM_CAMERA_FRAME_HEADER_BYTES];
        vim_camera_frame_header(header,(uint8_t)function);
        NSMutableData *message=[NSMutableData dataWithBytes:header length:sizeof header];
        [message appendData:frame];
        [self send:message];
    });
}
static NSData *VIMCameraBlackFrame(void) {
    static NSData *frame;static dispatch_once_t once;
    dispatch_once(&once,^{
        NSMutableData *black=[NSMutableData dataWithLength:VIM_CAMERA_FRAME_BYTES];
        uint8_t *out=black.mutableBytes;
        for(NSUInteger i=0;i<VIM_CAMERA_FRAME_BYTES;i+=4){out[i]=16;out[i+1]=128;out[i+2]=16;out[i+3]=128;}
        frame=black;
    });
    return frame;
}
- (NSData *)testPatternFrame {
    static const uint8_t bars[][3]={
        {255,255,255},{255,255,0},{0,255,255},{0,255,0},
        {255,0,255},{255,0,0},{0,0,255},{0,0,0}
    };
    NSMutableData *frame=[NSMutableData dataWithLength:VIM_CAMERA_FRAME_BYTES];
    uint8_t *out=frame.mutableBytes;
    unsigned phase=patternPhase++;
    for(NSInteger y=0;y<VIM_CAMERA_HEIGHT;y++) {
        for(NSInteger x=0;x<VIM_CAMERA_WIDTH;x+=2) {
            NSInteger bar=((x+(NSInteger)(phase*8))%VIM_CAMERA_WIDTH)/(VIM_CAMERA_WIDTH/8);
            const uint8_t *color=bars[bar];
            uint8_t y0,u,v;
            VIMCameraYUY2Sample(color[0],color[1],color[2],&y0,&u,&v);
            out[0]=y0;out[1]=u;out[2]=y0;out[3]=v;out+=4;
        }
    }
    return frame;
}
- (void)stopPattern {
    if(!self.pattern)return;
    dispatch_source_cancel(self.pattern);
    self.pattern=nil;
}
- (void)startPattern {
    if(self.pattern||self.stopped)return;
    dispatch_source_t timer=dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER,0,0,self.queue);
    uint64_t interval=(uint64_t)(NSEC_PER_SEC/(self.frameRate?:VIM_CAMERA_FPS));
    dispatch_source_set_timer(timer,dispatch_time(DISPATCH_TIME_NOW,(int64_t)interval),interval,interval/10);
    __weak VIMCameraCapture *weakSelf=self;
    dispatch_source_set_event_handler(timer,^{
        VIMCameraCapture *capture=weakSelf;
        if(!capture||capture.stopped||capture->fd<0||capture.pending.length)return;
        BOOL pattern=capture->testPattern&&capture->enabled;
        NSData *frame=pattern?[capture testPatternFrame]:VIMCameraBlackFrame();
        for(unsigned function=0;function<(pattern?1:capture->functions);function++) {
            if(!pattern&&!capture->status.streaming[function])continue;
            uint8_t header[VIM_CAMERA_FRAME_HEADER_BYTES];
            vim_camera_frame_header(header,(uint8_t)function);
            NSMutableData *message=[NSMutableData dataWithBytes:header length:sizeof header];
            [message appendData:frame];
            [capture send:message];
        }
    });
    dispatch_resume(timer);
    self.pattern=timer;
}

#pragma mark Host cameras

- (void)stopSession:(unsigned)index {
    AVCaptureSession *session=self.sessions[@(index)];
    if(!session)return;
    for(id token in self.observers[@(index)])[NSNotificationCenter.defaultCenter removeObserver:token];
    [self.observers removeObjectForKey:@(index)];
    [session stopRunning];
    [self.sessions removeObjectForKey:@(index)];
    [self.sinks removeObjectForKey:@(index)];
}
static void VIMCameraSetFrameRate(AVCaptureDevice *device,unsigned fps) {
    CMTime wanted=CMTimeMake(1,(int32_t)fps);
    for(AVFrameRateRange *range in device.activeFormat.videoSupportedFrameRateRanges) {
        if(range.minFrameRate>fps||range.maxFrameRate<fps)continue;
        if(![device lockForConfiguration:nil])return;
        @try {
            device.activeVideoMinFrameDuration=wanted;
            device.activeVideoMaxFrameDuration=wanted;
        } @catch(NSException *exception) {
            (void)exception;
        }
        [device unlockForConfiguration];
        return;
    }
}
- (void)startSession:(unsigned)index {
    if(index>=slots.count||self.stopped||self.sessions[@(index)])return;
    NSString *uid=[NSString stringWithUTF8String:slots.key[index]]?:@"";
    AVCaptureDevice *device=[AVCaptureDevice deviceWithUniqueID:uid];
    if(!device){[self note:[NSString stringWithFormat:@"Camera routing skipped: %@ is unavailable.",[NSString stringWithUTF8String:slots.name[index]]?:@"a camera"]];return;}
    NSError *error=nil;
    AVCaptureDeviceInput *input=[AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
    AVCaptureSession *session=[AVCaptureSession new];
    AVCaptureVideoDataOutput *output=[AVCaptureVideoDataOutput new];
    VIMCameraSampleSink *sink=[VIMCameraSampleSink new];
    sink.owner=self;sink.slot=index;
    output.alwaysDiscardsLateVideoFrames=YES;
    [output setSampleBufferDelegate:sink queue:self.samples];
    [session beginConfiguration];
    if([session canSetSessionPreset:AVCaptureSessionPreset640x480])session.sessionPreset=AVCaptureSessionPreset640x480;
    if(!input||![session canAddInput:input]||![session canAddOutput:output]){
        [session commitConfiguration];
        [self note:error.localizedDescription?:@"Camera routing skipped: the camera could not be opened."];return;
    }
    [session addInput:input];[session addOutput:output];
    output.videoSettings=@{(id)kCVPixelBufferPixelFormatTypeKey:@(kCVPixelFormatType_32BGRA)};
    VIMCameraSetFrameRate(device,self.frameRate?:VIM_CAMERA_FPS);
    [session commitConfiguration];
    __weak VIMCameraCapture *weakSelf=self;
    id failed=[NSNotificationCenter.defaultCenter addObserverForName:AVCaptureSessionRuntimeErrorNotification object:session queue:nil usingBlock:^(NSNotification *notification){
        NSError *runtime=notification.userInfo[AVCaptureSessionErrorKey];
        [weakSelf note:[NSString stringWithFormat:@"Camera routing: %@",runtime.localizedDescription?:@"the camera stopped"]];
    }];
    self.sessions[@(index)]=session;
    self.sinks[@(index)]=sink;
    self.observers[@(index)]=@[failed];
    [session startRunning];
}
- (void)stopAllSessions {
    for(NSNumber *index in self.sessions.allKeys)[self stopSession:index.unsignedIntValue];
}
- (void)noteDriver:(BOOL)loaded {
    if(loaded==driverLoaded)return;
    driverLoaded=loaded;
    void (^changed)(BOOL)=self.driverChanged;
    if(changed)dispatch_async(dispatch_get_main_queue(),^{changed(loaded);});
}
- (void)applyStatus {
    if(self.stopped)return;
    [self noteDriver:status.driver!=0];
    const char *mode=testPattern?"testpattern":(autosense?"autosense":"constant");
    if(testPattern||!enabled){
        BOOL streaming=NO;
        for(unsigned function=0;function<functions;function++)streaming=streaming||status.streaming[function];
        [self noteAutosenseActive:NO];
        [self stopAllSessions];
        if(enabled?slots.count&&vim_camera_function_should_run(mode,1,status.streaming[0]):streaming)[self startPattern];
        else [self stopPattern];
        return;
    }
    [self stopPattern];
    BOOL anyRunning=NO;
    for(unsigned index=0;index<slots.count;index++) {
        int function=vim_camera_function_for_slot(index,preferred,functions);
        uint8_t streaming=function>=0?status.streaming[function]:0;
        BOOL run=slots.present[index]&&enabled&&function>=0&&
            vim_camera_function_should_run(mode,index==preferred,streaming);
        if(run)[self startSession:index];
        else [self stopSession:index];
        anyRunning=anyRunning||run;
        if(streaming&&!wasStreaming[index]&&slots.present[index]) {
            NSString *uid=[NSString stringWithUTF8String:slots.key[index]]?:@"";
            void (^opened)(NSString *)=self.guestOpened;
            if(opened&&uid.length)dispatch_async(dispatch_get_main_queue(),^{opened(uid);});
        }
        wasStreaming[index]=streaming;
    }
    [self noteAutosenseActive:autosense&&anyRunning];
}
- (void)noteAutosenseActive:(BOOL)active {
    if(active==autosenseActive)return;
    autosenseActive=active;
    void (^changed)(BOOL)=self.autosenseActivityChanged;
    if(changed&&autosense&&enabled)dispatch_async(dispatch_get_main_queue(),^{changed(active);});
}
- (void)replaceCameras:(NSArray<NSDictionary<NSString *,NSString *> *> *)cameras mode:(NSString *)mode preferredUID:(NSString *)uid enabled:(BOOL)cameraEnabled {
    const char *keys[VIM_CAMERA_MAX_FUNCTIONS],*names[VIM_CAMERA_MAX_FUNCTIONS];
    char keyStore[VIM_CAMERA_MAX_FUNCTIONS][VIM_CAMERA_SLOT_KEY_BYTES];
    char nameStore[VIM_CAMERA_MAX_FUNCTIONS][VIM_CAMERA_NAME_BYTES];
    unsigned count=0,index;
    enabled=cameraEnabled;
    autosense=[mode isEqual:@"autosense"];
    testPattern=[mode isEqual:@"testpattern"];
    for(NSDictionary *camera in cameras?:@[]) {
        if(count>=VIM_CAMERA_MAX_FUNCTIONS)break;
        NSString *key=camera[@"UID"]?:@"",*name=camera[@"Name"]?:key;
        if(![key getCString:keyStore[count] maxLength:sizeof keyStore[count] encoding:NSUTF8StringEncoding])keyStore[count][0]=0;
        if(![name getCString:nameStore[count] maxLength:sizeof nameStore[count] encoding:NSUTF8StringEncoding])nameStore[count][0]=0;
        keys[count]=keyStore[count];names[count]=nameStore[count];count++;
    }
    vim_camera_slots_update(&slots,keys,names,count);
    if(!enabled)for(index=0;index<slots.count;index++)slots.present[index]=0;
    preferred=0;
    for(index=0;index<slots.count;index++)if(uid.length&&strcmp(slots.key[index],uid.UTF8String)==0)preferred=index;
    [self publishDirectory];
    [self applyStatus];
}
- (void)consumeStatus:(const uint8_t *)bytes length:(size_t)length {
    vim_camera_status_bytes(&status,bytes,length);
    [self applyStatus];
}

#pragma mark Public

- (void)startWithSocketPath:(NSString *)path functions:(NSUInteger)count guestOpened:(void (^)(NSString *))guestOpened log:(void (^)(NSString *))log {
    self.log=log;self.guestOpened=guestOpened;
    dispatch_async(self.queue,^{
        self.stopped=NO;self.socketPath=path;
        self->functions=count?(unsigned)MIN(count,(NSUInteger)VIM_CAMERA_MAX_FUNCTIONS):1;
        vim_camera_status_init(&self->status,self->functions);
        [self openSocket];
    });
}
- (void)updateWithCameras:(NSArray<NSDictionary<NSString *,NSString *> *> *)cameras mode:(NSString *)mode preferredUID:(NSString *)uid enabled:(BOOL)cameraEnabled {
    dispatch_async(self.queue,^{
        if(self.stopped)return;
        [self replaceCameras:cameras mode:mode preferredUID:uid enabled:cameraEnabled];
    });
}
- (void)stop {
    self.stopped=YES;
    dispatch_async(self.queue,^{
        [self noteDriver:NO];
        [self stopPattern];
        [self stopAllSessions];
        [self closeSocket];
    });
}
@end
