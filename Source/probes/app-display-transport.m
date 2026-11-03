// Bounded smoke test of production IPC + real Metal presentation, without a VM.
#import <Cocoa/Cocoa.h>
#import "VIMDisplayConnection.h"
#import "VIMMetalView.h"
#import <QuartzCore/QuartzCore.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <servers/bootstrap.h>
#include "../PassthroughOpenGL/display-surface-message.h"
typedef struct { uint32_t magic, operation; uint64_t sequence;
    uint32_t width,height,stride,reserved; } Message;
static void check(BOOL value, const char *message) {
    if (!value) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}
@interface VIMSleepRaceView : VIMMetalView
@property BOOL sleepDuringNextFrame;
@end
@implementation VIMSleepRaceView
- (void)presentWidth:(NSUInteger)width height:(NSUInteger)height stride:(NSUInteger)stride completion:(void (^)(BOOL))completion {
    [super presentWidth:width height:height stride:stride completion:completion];
    if(self.sleepDuringNextFrame) {
        self.sleepDuringNextFrame=NO;
        // Completion is dispatched to the main queue, so this deterministically
        // crosses a sleep/wake boundary after submit but before acceptance.
        self.displaySleeping=YES;self.displaySleeping=NO;
    }
}
@end
int main(void) { @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,640,400)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    VIMSleepRaceView *view = [[VIMSleepRaceView alloc] initWithFrame:NSMakeRect(0,0,640,400)];
    // Exercise cursor policy without capturing input, installing an event tap
    // or depending on where the human user's pointer currently is.
    [view setValue:@YES forKey:@"inputCaptured"];view.softwareCursor=YES;
    check([[view cursorDiagnostics][@"EffectiveCursorHidden"] boolValue],"VGA keeps its software cursor without a duplicate host arrow");
    view.softwareCursor=NO;[view setGuestCursorVisible:NO];
    check([[view cursorDiagnostics][@"EffectiveCursorHidden"] boolValue],"honor an explicit guest cursor hide");
    [view setGuestCursorVisible:YES];
    check(![[view cursorDiagnostics][@"EffectiveCursorHidden"] boolValue],"show restores the native cursor even before a glyph arrives");
    [view setValue:@NO forKey:@"inputCaptured"];[view updateTrackingAreas];
    check((view.trackingAreas.lastObject.options&NSTrackingCursorUpdate)!=0,"AppKit cursor updates are tracked");
    window.contentView = view; window.title = @"Display transport smoke test";
    [window center]; [window orderFront:nil];
    [view layout];NSUInteger layouts=[[view frameDiagnostics][@"LayoutUpdates"] unsignedIntegerValue];
    [view layout];[view layout];
    check([[view frameDiagnostics][@"LayoutUpdates"] unsignedIntegerValue]==layouts,"unchanged layout does not commit a Core Animation transaction");
    NSError *error = nil;
    VIMDisplayConnection *connection = [[VIMDisplayConnection alloc] initWithView:view
        failure:^(NSError *failure) { fprintf(stderr,"FAIL: %s\n",failure.description.UTF8String); exit(1); }
        error:&error];
    check(connection != nil,"create listener");
    [connection requestWidth:800 height:600 density:2];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0), ^{
        int channel = socket(AF_UNIX,SOCK_STREAM,0);
        struct sockaddr_un address = {.sun_family=AF_UNIX};
        strlcpy(address.sun_path,connection.socketPath.fileSystemRepresentation,sizeof(address.sun_path));
        check(!connect(channel,(struct sockaddr *)&address,sizeof(address)),"connect");
        NSString *name = [NSString stringWithFormat:@"/vim-test-%d",getpid()];
        int frame = shm_open(name.UTF8String,O_RDWR|O_CREAT|O_EXCL,0600);
        check(frame >= 0,"anonymous memory"); shm_unlink(name.UTF8String);
        size_t length = 8192ULL*8192*4;
        check(!ftruncate(frame,length),"size shared memory");
        uint32_t *pixels = mmap(NULL,length,PROT_READ|PROT_WRITE,MAP_SHARED,frame,0);
        check(pixels != MAP_FAILED,"map shared memory");
        Message hello = {.magic=0x56494d31,.operation=1};
        char control[CMSG_SPACE(sizeof(int))]={0};
        struct iovec iov={&hello,sizeof(hello)};
        struct msghdr msg={.msg_iov=&iov,.msg_iovlen=1,.msg_control=control,.msg_controllen=sizeof(control)};
        struct cmsghdr *c=CMSG_FIRSTHDR(&msg);
        c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c),&frame,sizeof(frame));
        check(sendmsg(channel,&msg,0)==sizeof(hello),"send shared descriptor");
        close(frame);
        Message resize={0};size_t resizeBytes=0;
        while(resizeBytes<sizeof(resize)) {
            ssize_t n=read(channel,(char *)&resize+resizeBytes,sizeof(resize)-resizeBytes);
            check(n>0,"idle resize received");resizeBytes+=n;
        }
        check(resize.magic==hello.magic && resize.operation==6 && resize.width==800 && resize.height==600 &&
            resize.stride==2 && !resize.reserved,"resize and density before first frame");
        Message visibility={.magic=0x56494d31,.operation=4,.width=1};
        check(write(channel,&visibility,sizeof(visibility))==sizeof(visibility),"cursor visibility");
        for (uint64_t sequence=1;sequence<=7;sequence++) {
            if(sequence==5||sequence==6) {
                Message power={.magic=0x56494d31,.operation=7,.sequence=sequence-4,.width=sequence==5};
                check(write(channel,&power,sizeof(power))==sizeof(power),"display power event");
            }
            // Exercise scaled -> Retina -> scaled without another allocation.
            unsigned scale=sequence==2?2:1,w=640*scale,h=400*scale,glyph=16*scale;
            uint32_t expected=0xff285580+(uint32_t)(sequence-1)*0x000a0000;
            for(size_t i=0;i<w*h;i++)pixels[i]=expected;
            Message cursor={.magic=0x56494d31,.operation=5,.width=glyph,.height=glyph};
            uint32_t cursorPixels[32*32];
            for(unsigned i=0;i<glyph*glyph;i++)cursorPixels[i]=0xffff0000;
            check(write(channel,&cursor,sizeof(cursor))==sizeof(cursor),"cursor header");
            check(write(channel,cursorPixels,glyph*glyph*4)==glyph*glyph*4,"cursor pixels");
            Message event={.magic=0x56494d31,.operation=sequence>=5?8:2,.sequence=sequence,
                .width=w,.height=h,.stride=w*4,.reserved=sequence==7?2:0};
            if(sequence==3)dispatch_sync(dispatch_get_main_queue(),^{view.sleepDuringNextFrame=YES;});
            check(write(channel,&event,sizeof(event))==sizeof(event),"send frame");
            Message ack={0};size_t received=0;
            while(received<sizeof(ack)) { ssize_t n=read(channel,(char *)&ack+received,sizeof(ack)-received);
                check(n>0,"receive acknowledgment");received+=n; }
            check(ack.magic==event.magic && ack.operation==3 && ack.sequence==sequence,"ordered acknowledgment");
            // Verify the presented texture, not just a metadata ACK. Keep our
            // producer idle throughout capture so the shared frame stays owned.
            dispatch_semaphore_t captured=dispatch_semaphore_create(0);
            dispatch_async(dispatch_get_main_queue(),^{
                CALayer *layer=[view valueForKey:@"presentationLayer"];
                if(sequence==5||sequence==6) {
                    check(view.guestDisplaySleeping==(sequence==5),"independent guest display state");
                    view.displaySleeping=NO;
                    check(view.displaySleeping==(sequence==5),"running system does not clear display sleep");
                    check(layer.hidden,"old-epoch frame stays hidden both asleep and after wake");
                    [view capturePNG:^(NSData *png){check(!png,"old power epoch is not captured");dispatch_semaphore_signal(captured);}];
                    return;
                }
                if(sequence==3) {
                    check(layer.hidden,"in-flight pre-sleep frame must not reappear after wake");
                    [view capturePNG:^(NSData *png){check(!png,"old in-flight frame cannot become the current capture");dispatch_semaphore_signal(captured);}];
                    return;
                }
                check(!layer.hidden,"fresh completed frame makes the display visible");
                NSCursor *actual=[view valueForKey:@"guestCursor"];
                check(fabs(actual.image.size.width-16)<.01,"cursor stays 16 points in scaled and Retina modes");
                [view capturePNG:^(NSData *png){
                    NSBitmapImageRep *image=[[NSBitmapImageRep alloc] initWithData:png];
                    check(image.pixelsWide==w&&image.pixelsHigh==h,"captured backing size");
                    // Compare encoded RGB samples, not an NSColor conversion
                    // into the host monitor's device-dependent color space.
                    NSUInteger samples[5]={0};[image getPixel:samples atX:w/2 y:h/2];
                    NSUInteger first=(image.bitmapFormat&NSBitmapFormatAlphaFirst)?1:0;
                    check(image.bitsPerSample==8&&image.samplesPerPixel>=3&&
                        samples[first]==((expected>>16)&255)&&samples[first+1]==((expected>>8)&255)&&
                        samples[first+2]==(expected&255),"real Metal pixels match shared storage");
                    if(sequence==1) {
                        view.displaySleeping=YES;view.displaySleeping=NO;
                        check(layer.hidden,"waking alone must not expose the last frame");
                        [view capturePNG:^(NSData *old){check(!old,"pre-sleep capture is retired");dispatch_semaphore_signal(captured);}];
                    } else dispatch_semaphore_signal(captured);
                }];
            });
            check(dispatch_semaphore_wait(captured,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0,"GPU capture finishes");
        }
        dispatch_semaphore_t gpuSurfaceDone=dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_main_queue(), ^{
            NSDictionary *properties=@{(id)kIOSurfaceWidth:@640,(id)kIOSurfaceHeight:@480,
                (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@2560,
                (id)kIOSurfacePixelFormat:@(0x42475241)};
            IOSurfaceRef surface=IOSurfaceCreate((__bridge CFDictionaryRef)properties);
            check(surface!=NULL,"allocate GPU-source test surface");
            check(IOSurfaceLock(surface,0,NULL)==0,"lock test source");
            uint32_t *base=IOSurfaceGetBaseAddress(surface);
            for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x)
                base[y*(IOSurfaceGetBytesPerRow(surface)/4)+x]=0xff21ab65;
            check(IOSurfaceUnlock(surface,0,NULL)==0,"publish test source");
            view.displaySleeping=NO;
            NSString *service=[connection enableSurfaceTransportForRendererPID:getpid() error:NULL];
            check(service!=nil,"create app surface listener");
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
                mach_port_t endpoint=0,reply=0;
                check(!bootstrap_look_up(bootstrap_port,(char *)service.UTF8String,&endpoint),"find app surface listener");
                check(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,&reply),"create release receiver");
                check(!VIMSurfaceFrameSend(endpoint,reply,surface,1,1,0,640,480,5000),"send GPU surface to app");
                CFRelease(surface);
                uint32_t released=VIMSurfaceFailed;
                check(!VIMSurfaceReleaseReceive(reply,1,1,0,&released,5000) && released==VIMSurfaceGPUCompleted,
                    "app releases source only after GPU completion");
                mach_port_destroy(mach_task_self(),reply);mach_port_deallocate(mach_task_self(),endpoint);
                dispatch_async(dispatch_get_main_queue(),^{
                [view capturePNG:^(NSData *png){
                    NSBitmapImageRep *image=[[NSBitmapImageRep alloc] initWithData:png];
                    NSUInteger samples[5]={0};[image getPixel:samples atX:320 y:240];
                    NSUInteger first=(image.bitmapFormat&NSBitmapFormatAlphaFirst)?1:0;
                    check(image.pixelsWide==640 && image.pixelsHigh==480 &&
                        samples[first]==0x21 && samples[first+1]==0xab && samples[first+2]==0x65,
                        "GPU surface Metal output pixels");
                    dispatch_semaphore_signal(gpuSurfaceDone);
                }];
                });
            });
        });
        check(dispatch_semaphore_wait(gpuSurfaceDone,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC))==0,
            "GPU surface completion");
        for(unsigned fallback=0;fallback<2;++fallback){
            if(fallback)dispatch_sync(dispatch_get_main_queue(),^{[connection disableSurfaceTransport];});
            for(unsigned i=0;i<640*480;++i)((uint32_t *)pixels)[i]=0xff556677;
            Message event={.magic=0x56494d31,.operation=8,.sequence=8+fallback,
                .width=640,.height=480,.stride=2560,.reserved=2};
            check(write(channel,&event,sizeof(event))==sizeof(event),"send fallback arbitration frame");
            Message ack={0};size_t received=0;
            while(received<sizeof(ack)){ssize_t n=read(channel,(char *)&ack+received,sizeof(ack)-received);
                check(n>0,"fallback arbitration ACK");received+=n;}
            check(ack.sequence==event.sequence && ack.operation==3,"fallback ordered release");
            dispatch_semaphore_t verified=dispatch_semaphore_create(0);
            dispatch_async(dispatch_get_main_queue(),^{
                [view capturePNG:^(NSData *png){
                    NSBitmapImageRep *image=[[NSBitmapImageRep alloc] initWithData:png];
                    NSUInteger samples[5]={0};[image getPixel:samples atX:320 y:240];
                    NSUInteger first=(image.bitmapFormat&NSBitmapFormatAlphaFirst)?1:0;
                    check(image!=nil && samples[first]==(fallback?0x55:0x21) &&
                        samples[first+1]==(fallback?0x66:0xab) && samples[first+2]==(fallback?0x77:0x65),
                        "GPU excludes CPU frames until explicit fallback");
                    dispatch_semaphore_signal(verified);
                }];
            });
            check(!dispatch_semaphore_wait(verified,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC)),"arbitration pixels");
        }
        dispatch_semaphore_t exited=dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_main_queue(),^{
            NSTask *renderer=[NSTask new];renderer.executableURL=[NSURL fileURLWithPath:@"/bin/sleep"];
            renderer.arguments=@[@"30"];
            check([renderer launchAndReturnError:NULL],"launch disposable renderer-lifetime test");
            check([connection enableSurfaceTransportForRendererPID:renderer.processIdentifier error:NULL]!=nil,
                "observe renderer lifetime");
            [renderer terminate];
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC),dispatch_get_main_queue(),^{
                check([connection valueForKey:@"surfaceListener"]==nil &&
                    ![[connection valueForKey:@"surfaceActive"] boolValue],"renderer exit restores CPU eligibility");
                dispatch_semaphore_signal(exited);
            });
        });
        check(!dispatch_semaphore_wait(exited,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC)),"renderer exit handling");
        dispatch_sync(dispatch_get_main_queue(), ^{
            [connection stop];[view discardFrameStorage];
            check([[view frameDiagnostics][@"FrameStorageBytes"] unsignedIntegerValue]==0,"stopped view releases its shared frame mapping");
        });
        munmap(pixels,length);close(channel);
        puts("PASS: shared descriptor, resize, scaled/Retina cursors, fresh Metal pixels, independent display power, old power epoch and in-flight retirement, ordered GPU-completion ACKs, direct IOSurface pixels/lifetime, clean stop");
        exit(0);
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,15*NSEC_PER_SEC),dispatch_get_main_queue(), ^{
        fprintf(stderr,"FAIL: display test timeout\n");exit(1);
    });
    [NSApp run];
    return 1;
} }
