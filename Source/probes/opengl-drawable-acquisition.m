/* Deterministic production-view ownership check, NOT a guest/FPS benchmark.
 * Gate actual CAMetalLayer acquisition on its worker; the normal main-thread
 * view must still accept/coalesce immutable sources, reject stale epochs and
 * finish every ownership callback exactly once. No guest input/TCC changes. */
#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import "../VirtualIntelMac/VIMMetalView.h"

static void require(BOOL good,const char *message)
{ if(!good){fprintf(stderr,"FAIL %s\n",message);exit(1);} }
static void waitFor(dispatch_semaphore_t event,const char *message)
{ require(!dispatch_semaphore_wait(event,dispatch_time(DISPATCH_TIME_NOW,3*NSEC_PER_SEC)),message); }
@interface VIMAcquisitionTestLayer : CAMetalLayer
@property dispatch_semaphore_t entered,releaseGate;
@property BOOL gateNext;
@end
@implementation VIMAcquisitionTestLayer
- (id<CAMetalDrawable>)nextDrawable {
    require(!NSThread.isMainThread,"acquisition is off the input thread");
    if(self.gateNext){self.gateNext=NO;dispatch_semaphore_signal(self.entered);waitFor(self.releaseGate,"bounded acquisition gate");}
    return [super nextDrawable];
}
@end
static IOSurfaceRef surface(NSUInteger width,NSUInteger height,uint32_t pixel)
{
    IOSurfaceRef s=IOSurfaceCreate((__bridge CFDictionaryRef)@{
        (id)kIOSurfaceWidth:@(width),(id)kIOSurfaceHeight:@(height),
        (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@(width*4),
        (id)kIOSurfaceAllocSize:@(width*height*4),(id)kIOSurfacePixelFormat:@(0x42475241)});
    require(s && !IOSurfaceLock(s,0,NULL),"fixture source allocation/lock");
    uint32_t *p=IOSurfaceGetBaseAddress(s);require(p!=NULL,"source pixels");
    for(NSUInteger i=0;i<width*height;i++)p[i]=pixel;
    require(!IOSurfaceUnlock(s,0,NULL),"complete source write before handoff");return s;
}
int main(void){@autoreleasepool{
    [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    NSWindow *window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,640,400)
        styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    window.title=@"Drawable ownership check";window.ignoresMouseEvents=YES;
    VIMMetalView *view=[[VIMMetalView alloc] initWithFrame:NSMakeRect(0,0,640,400)];window.contentView=view;
    view.asynchronousDrawableAcquisition=YES;
    CAMetalLayer *old=[view valueForKey:@"presentationLayer"];
    VIMAcquisitionTestLayer *layer=[VIMAcquisitionTestLayer layer];
    layer.device=old.device;layer.pixelFormat=old.pixelFormat;layer.framebufferOnly=NO;
    layer.opaque=YES;layer.presentsWithTransaction=NO;layer.allowsNextDrawableTimeout=YES;
    layer.entered=dispatch_semaphore_create(0);layer.releaseGate=dispatch_semaphore_create(0);
    [old removeFromSuperlayer];[view setValue:layer forKey:@"presentationLayer"];[view.layer addSublayer:layer];
    [window center];[window makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,NSEC_PER_SEC/5),dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
        IOSurfaceRef a=surface(640,400,0xffff0000),b=surface(640,400,0xff00ff00),c=surface(640,400,0xff0000ff);
        dispatch_semaphore_t done=dispatch_semaphore_create(0);
        __block NSUInteger callbacks=0,skipped=0,completed=0;
        void (^finish)(BOOL)=^(BOOL good){
            require(NSThread.isMainThread,"ownership callback is main-thread serialized");
            callbacks++;if(good)completed++;else skipped++;dispatch_semaphore_signal(done);
        };
        dispatch_sync(dispatch_get_main_queue(),^{layer.gateNext=YES;[view presentSurface:a width:640 height:400 completion:finish];});
        waitFor(layer.entered,"worker entered before coalescing");
        /* This sync proves the UI queue progresses WHILE acquisition waits. */
        dispatch_sync(dispatch_get_main_queue(),^{
            [view presentSurface:b width:640 height:400 completion:finish];
            [view presentSurface:c width:640 height:400 completion:finish];
            require(callbacks==1 && skipped==1,"middle immutable frame retired once without GPU use");
        });
        dispatch_semaphore_signal(layer.releaseGate);
        for(unsigned i=0;i<3;i++)waitFor(done,"all coalesced ownership callbacks complete");
        dispatch_sync(dispatch_get_main_queue(),^{
            require(callbacks==3 && skipped==2 && completed==1,"only newest source consumed, each callback once");
            require([view.frameDiagnostics[@"CoalescedBeforeEncoding"] unsignedIntegerValue]==1,"late source selected before encoding");
        });
        __block BOOL pixels=NO;
        dispatch_sync(dispatch_get_main_queue(),^{[view capturePNG:^(NSData *png){
            NSColor *color=[[[NSBitmapImageRep imageRepWithData:png] colorAtX:20 y:20] colorUsingColorSpace:NSColorSpace.deviceRGBColorSpace];
            fprintf(stderr,"actual captured bytes=%lu color=%s\n",(unsigned long)png.length,color.description.UTF8String?:"nil");
            // PNG capture uses the display's device color space. Distinguish
            // the deliberately disjoint primaries, not a color-profile test.
            pixels=color.blueComponent>.9 && color.blueComponent>color.redComponent+.5 &&
                color.blueComponent>color.greenComponent+.5;dispatch_semaphore_signal(done);
        }];});
        waitFor(done,"actual newest GPU pixels captured");require(pixels,"newest blue image, not old red/green");
        dispatch_sync(dispatch_get_main_queue(),^{layer.gateNext=YES;[view presentSurface:a width:640 height:400 completion:finish];});
        waitFor(layer.entered,"second acquisition waits");
        dispatch_sync(dispatch_get_main_queue(),^{[view invalidatePresentation];});
        dispatch_semaphore_signal(layer.releaseGate);waitFor(done,"stale epoch ownership retired");
        dispatch_sync(dispatch_get_main_queue(),^{
            require(callbacks==4 && skipped==3 && [view.frameDiagnostics[@"PresentationHidden"] boolValue],"stale acquired frame cannot unhide display");
            [view presentSurface:c width:640 height:400 completion:finish];
        });
        waitFor(done,"new epoch resumes");
        dispatch_sync(dispatch_get_main_queue(),^{require(callbacks==5 && completed==2,"new epoch completes once");});
        IOSurfaceRef resized=surface(800,600,0xff00ff00);
        dispatch_sync(dispatch_get_main_queue(),^{layer.gateNext=YES;[view presentSurface:a width:640 height:400 completion:finish];});
        waitFor(layer.entered,"resize acquisition waits");
        dispatch_sync(dispatch_get_main_queue(),^{[view presentSurface:resized width:800 height:600 completion:finish];});
        dispatch_semaphore_signal(layer.releaseGate);waitFor(done,"old geometry retired");waitFor(done,"new geometry completed");
        dispatch_sync(dispatch_get_main_queue(),^{
            require(callbacks==7 && completed==3 && skipped==4 &&
                [view.frameDiagnostics[@"PixelWidth"] unsignedIntegerValue]==800 &&
                [view.frameDiagnostics[@"PixelHeight"] unsignedIntegerValue]==600,"resize uses matching drawable/source geometry");
        });
        CFRelease(resized);
        CFRelease(a);CFRelease(b);CFRelease(c);
        puts("PASS actual Metal view: nonblocking UI, newest immutable pixels, once-only callbacks, stale-epoch rejection/recovery and resize");
        dispatch_async(dispatch_get_main_queue(),^{[window close];exit(0);});
    });
    [NSApp run];return 1;
}}
