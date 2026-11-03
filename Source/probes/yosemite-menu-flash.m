/* Bounded Yosemite menu transition diagnostic. Reports black-pixel counts
 * for only the left menu-bar strip; no screenshots or full frames saved. */
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void post(CGEventType type, double x, double y)
{
    CGEventRef event=CGEventCreateMouseEvent(NULL,type,CGPointMake(x,y),kCGMouseButtonLeft);
    if(event){CGEventPost(kCGHIDEventTap,event);CFRelease(event);}
}

static void sweep(void)
{
    static const double titles[]={19,76,128,171,218,262,324,373};
    usleep(350000);
    for(unsigned round=0;round<3;++round){
        bool drag=round!=0;
        CGEventType move=drag?kCGEventLeftMouseDragged:kCGEventMouseMoved;
        post(kCGEventLeftMouseDown,titles[0],11);
        usleep(35000);if(!drag)post(kCGEventLeftMouseUp,titles[0],11);
        usleep(140000);
        for(unsigned i=1;i<sizeof(titles)/sizeof(*titles);++i){
            post(move,titles[i],11);usleep(round==2?32000:85000);
        }
        for(unsigned i=sizeof(titles)/sizeof(*titles)-1;i>0;--i){
            post(move,titles[i-1],11);usleep(round==2?32000:85000);
        }
        if(drag)post(kCGEventLeftMouseUp,titles[0],11);
        CGEventRef escDown=CGEventCreateKeyboardEvent(NULL,53,true);
        CGEventRef escUp=CGEventCreateKeyboardEvent(NULL,53,false);
        if(escDown){CGEventPost(kCGHIDEventTap,escDown);CFRelease(escDown);}
        if(escUp){CGEventPost(kCGHIDEventTap,escUp);CFRelease(escUp);}
        usleep(250000);
    }
}

int main(int argc,char **argv)
{
    @autoreleasepool {
        if(argc==2&&!strcmp(argv[1],"--input-only")){sweep();return 0;}
        bool spaces=argc==2&&!strcmp(argv[1],"--observe-spaces");
        if(argc!=1&&!spaces)return 2;
        CGDirectDisplayID display=CGMainDisplayID();
        CGRect screen=CGDisplayBounds(display);
        size_t width=CGDisplayPixelsWide(display),height=CGDisplayPixelsHigh(display);
        if(width<640||height<480||width>8192||height>8192||screen.size.width<400)return 2;
        double scale=width/screen.size.width;
        size_t x0=8*scale,x1=390*scale,y0=2*scale,y1=20*scale;
        size_t belowRows=22*scale;
        if(spaces&&y1+belowRows>height)return 2;
        dispatch_queue_t queue=dispatch_queue_create("org.vim.research.menu-flash",DISPATCH_QUEUE_SERIAL);
        __block unsigned frames=0,mostlyBlack=0,peak=0,transitions=0;
        __block uint64_t first=0;
        CGDisplayStreamRef stream=CGDisplayStreamCreateWithDispatchQueue(display,width,height,'BGRA',
            (__bridge CFDictionaryRef)@{(__bridge NSString *)kCGDisplayStreamShowCursor:@NO},queue,
            ^(CGDisplayStreamFrameStatus status,uint64_t time,IOSurfaceRef surface,CGDisplayStreamUpdateRef update){
                if(status!=kCGDisplayStreamFrameStatusFrameComplete||!surface)return;
                if(IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL))return;
                const unsigned char *base=IOSurfaceGetBaseAddress(surface);
                size_t pitch=IOSurfaceGetBytesPerRow(surface);
                unsigned black=0,total=0;
                bool transition=false;
                if(base&&pitch>=width*4){
                    for(size_t y=y0;y<y1;++y)
                      for(size_t x=x0;x<(spaces?52*scale:x1);++x){
                        const unsigned char *p=base+y*pitch+4*x;
                        const unsigned char *below=p+(spaces?belowRows*pitch:0);
                        black+=p[0]<12&&p[1]<12&&p[2]<12&&
                            (!spaces||below[0]>=18||below[1]>=18||below[2]>=18);++total;
                      }
                    if(spaces)for(size_t x=20;x<width-20;x+=8){
                        bool allDark=true;
                        for(unsigned row=1;row<=4;++row){
                            const unsigned char *p=base+(height*row/5)*pitch+4*x;
                            if(p[0]>=18||p[1]>=18||p[2]>=18){allDark=false;break;}
                        }
                        if(allDark){transition=true;break;}
                    }
                }
                IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
                if(!total)return;
                if(!first)first=time;
                unsigned permille=black*1000/total;
                if(permille>peak)peak=permille;
                ++frames;
                if(transition)++transitions;
                if(permille>=300){
                    ++mostlyBlack;
                    printf("black-frame=%u host_ticks_since_first=%llu black_permille=%u\n",
                        frames,(unsigned long long)(time-first),permille);
                }
            });
        if(!stream||CGDisplayStreamStart(stream)){if(stream)CFRelease(stream);return 2;}
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT,0),^{
            // Moving alone cannot activate a menu command.
            if(spaces)sleep(12);else sweep();
            CGDisplayStreamStop(stream);
            dispatch_async(queue,^{
                printf("menu-flash frames=%u transition_frames=%u black_frames=%u peak_black_permille=%u\n",frames,transitions,mostlyBlack,peak);
                CFRelease(stream);exit(frames?0:1);
            });
        });
        dispatch_main();
    }
}
