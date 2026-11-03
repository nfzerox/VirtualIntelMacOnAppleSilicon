/* Real production receiver + Metal blits, without a VM. Drain three valid
 * queued Mach frames in one main-thread turn so GPU-completion callbacks
 * cannot run between receives. This is deterministic legal backpressure,
 * not a mocked GPU or a presentation-throughput benchmark. */
#import <Cocoa/Cocoa.h>
#import "VIMDisplayConnection.h"
#import "VIMMetalView.h"
#include <servers/bootstrap.h>
#include "../PassthroughOpenGL/display-surface-message.h"
@interface VIMDisplayConnection (VIMBurstProbe)
- (void)receiveSurface:(mach_port_t)port;
@end
static unsigned failures;
static void check(BOOL okay,const char *name)
{fprintf(stderr,"%s %s\n",okay?"PASS":"FAIL",name);failures+=!okay;}
struct Burst {
    IOSurfaceRef surfaces[3];mach_port_t replies[3],endpoint,port;
    BOOL early;uint32_t earlyStatus;
};
static void fill(IOSurfaceRef surface,uint32_t color)
{
    check(!IOSurfaceLock(surface,0,NULL),"lock owned producer surface");
    uint32_t *p=IOSurfaceGetBaseAddress(surface);size_t row=IOSurfaceGetBytesPerRow(surface)/4;
    for(unsigned y=0;y<480;++y)for(unsigned x=0;x<640;++x)p[y*row+x]=color;
    check(!IOSurfaceUnlock(surface,0,NULL),"publish owned producer pixels");
}
static void capture(VIMMetalView *view,BOOL hidden,uint32_t expected)
{
    dispatch_semaphore_t done=dispatch_semaphore_create(0);
    dispatch_async(dispatch_get_main_queue(),^{
        [view capturePNG:^(NSData *png){
            if(hidden)check(!png,"retired burst does not reappear after wake/invalidation");
            else {
                NSBitmapImageRep *image=png?[[NSBitmapImageRep alloc] initWithData:png]:nil;
                NSUInteger samples[5]={0};if(image)[image getPixel:samples atX:320 y:240];
                NSUInteger first=(image.bitmapFormat&NSBitmapFormatAlphaFirst)?1:0;
                BOOL good=image.pixelsWide==640&&image.pixelsHigh==480&&
                    samples[first]==((expected>>16)&255)&&samples[first+1]==((expected>>8)&255)&&samples[first+2]==(expected&255);
                fprintf(stderr,"GPU center RGB=%lu,%lu,%lu expected=%u,%u,%u\n",samples[first],samples[first+1],samples[first+2],
                    (expected>>16)&255,(expected>>8)&255,expected&255);
                check(good,"last accepted frame reaches real Metal pixels without another producer update");
            }
            dispatch_semaphore_signal(done);
        }];
    });
    check(!dispatch_semaphore_wait(done,dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC)),"owned GPU capture completes");
}
static void phase(VIMMetalView *view,unsigned mode)
{
    struct Burst *b=calloc(1,sizeof(*b));if(!b)exit(2);
    __block VIMDisplayConnection *connection=nil;
    dispatch_sync(dispatch_get_main_queue(),^{
        view.displaySleeping=NO;view.guestDisplaySleeping=NO;[view invalidatePresentation];
        connection=[[VIMDisplayConnection alloc] initWithView:view failure:^(NSError *error){
            fprintf(stderr,"unexpected receiver failure: %s\n",error.localizedDescription.UTF8String);exit(1);
        } error:NULL];
        check(connection!=nil,"create production display connection");
        NSString *service=[connection enableSurfaceTransportForRendererPID:getpid() error:NULL];
        check(service!=nil,"create authenticated surface endpoint");
        check(!bootstrap_look_up(bootstrap_port,(char *)service.UTF8String,&b->endpoint),"resolve own surface capability");
        dispatch_source_t listener=[connection valueForKey:@"surfaceListener"];
        b->port=(mach_port_t)dispatch_source_get_handle(listener);
        const uint32_t colors[]={0xffd93030,0xff22b854,0xff2456d4};
        for(unsigned i=0;i<3;++i) {
            NSDictionary *props=@{(id)kIOSurfaceWidth:@640,(id)kIOSurfaceHeight:@480,
                (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@2560,(id)kIOSurfacePixelFormat:@(0x42475241)};
            b->surfaces[i]=IOSurfaceCreate((__bridge CFDictionaryRef)props);check(b->surfaces[i]!=NULL,"create immutable test version");
            fill(b->surfaces[i],colors[i]);
            check(!mach_port_allocate(mach_task_self(),MACH_PORT_RIGHT_RECEIVE,b->replies+i),"allocate one-use frame release endpoint");
            check(!VIMSurfaceFrameSend(b->endpoint,b->replies[i],b->surfaces[i],1,i+1,i,640,480,0),"enqueue valid burst frame");
        }
        /* All actual messages pass the production audit-token decoder. The
         * main turn cannot execute a GPU completion between these receives. */
        for(unsigned i=0;i<3;++i)[connection receiveSurface:b->port];
        kern_return_t early=VIMSurfaceReleaseReceive(b->replies[2],1,3,2,&b->earlyStatus,0);
        b->early=early==KERN_SUCCESS;
        check(early==MACH_RCV_TIMED_OUT,"newest frame stays owned while an older GPU blit is in flight");
        if(mode==1){view.displaySleeping=YES;view.displaySleeping=NO;}
        if(mode==2)[view invalidatePresentation];
    });
    for(unsigned i=0;i<3;++i) {
        uint32_t status=VIMSurfaceFailed;
        kern_return_t kr=0;
        if(i==2&&b->early)status=b->earlyStatus;
        else kr=VIMSurfaceReleaseReceive(b->replies[i],1,i+1,i,&status,5000);
        uint32_t expected=mode||i==1?VIMSurfaceSkipped:VIMSurfaceGPUCompleted;
        fprintf(stderr,"mode=%u ticket=%u release=%u expected=%u kr=%x\n",mode,i+1,status,expected,kr);
        check(!kr&&status==expected,"source ownership ends at completion, safe coalescing or explicit retirement");
        if(i==1)fill(b->surfaces[i],0xffff00ff); // Released intermediate storage is immediately reusable.
    }
    capture(view,mode!=0,0xff2456d4);
    if(mode) {
        dispatch_sync(dispatch_get_main_queue(),^{
            fill(b->surfaces[0],0xff339977);
            check(!VIMSurfaceFrameSend(b->endpoint,b->replies[0],b->surfaces[0],1,4,0,640,480,0),"send new post-retirement version");
            [connection receiveSurface:b->port];
        });
        uint32_t status=VIMSurfaceFailed;
        check(!VIMSurfaceReleaseReceive(b->replies[0],1,4,0,&status,5000)&&status==VIMSurfaceGPUCompleted,"fresh post-retirement GPU completion");
        capture(view,NO,0xff339977);
    }
    dispatch_sync(dispatch_get_main_queue(),^{[connection stop];[view discardFrameStorage];});
    for(unsigned i=0;i<3;++i){CFRelease(b->surfaces[i]);mach_port_destroy(mach_task_self(),b->replies[i]);}
    mach_port_deallocate(mach_task_self(),b->endpoint);free(b);
}
int main(void){@autoreleasepool {
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,640,480)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    VIMMetalView *view=[[VIMMetalView alloc] initWithFrame:NSMakeRect(0,0,640,480)];
    window.contentView=view;window.title=@"Owned surface-burst correctness check";[window center];[window orderFront:nil];[view layout];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{
        for(unsigned mode=0;mode<3;++mode)phase(view,mode);
        dispatch_sync(dispatch_get_main_queue(),^{[window orderOut:nil];});
        fprintf(stderr,"surface-burst failures=%u\n",failures);exit(failures?1:0);
    });
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,20*NSEC_PER_SEC),dispatch_get_main_queue(),^{fputs("FAIL bounded surface probe timed out\n",stderr);exit(1);});
    [NSApp run];return 2;
}}
