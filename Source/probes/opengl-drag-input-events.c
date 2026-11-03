/* Finite, passive mouse-only observation. Run as guest/reference root for
 * kCGHIDEventTap, exactly as the 10.9 CGEvent.h requires. No key collection,
 * input injection/filtering, AX/TCC writes, GL calls or polling service.
 * The owned noinline observer has a simple integer ABI for a paired DTrace
 * timeline; its callback timestamp is NOT the original HID receipt time.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct VIMMouseObservation { uint32_t type;int32_t x,y;uint64_t event_ns,callback_mach; };
static struct VIMMouseObservation observations[128];
static unsigned events, disabled;
static void stop(CFRunLoopTimerRef timer,void *context)
{ (void)timer;(void)context;CFRunLoopStop(CFRunLoopGetCurrent()); }

__attribute__((noinline))
void VIMDragEventObserved(uint32_t type,int32_t x,int32_t y,uint64_t event_ns,uint64_t callback_mach)
{
    /* Do not create SSH/network traffic during the input trial. The first
     * ordering capture used per-event output; retain that distinction when
     * comparing it with ordinary no-SSH-motion measurements. */
    observations[events++]=(struct VIMMouseObservation){type,x,y,event_ns,callback_mach};
}

static CGEventRef observe(CGEventTapProxy proxy,CGEventType type,CGEventRef event,void *context)
{
    (void)proxy;(void)context;
    if(type==kCGEventTapDisabledByTimeout || type==kCGEventTapDisabledByUserInput) {
        ++disabled;printf("FAILED event tap disabled type=%u\n",type);
        CFRunLoopStop(CFRunLoopGetCurrent());return event;
    }
    if(events>=128) {
        printf("FAILED mouse observation budget exhausted\n");++disabled;
        CFRunLoopStop(CFRunLoopGetCurrent());return event;
    }
    CGPoint point=CGEventGetLocation(event);
    if(isfinite(point.x) && isfinite(point.y) && fabs(point.x)<100000 && fabs(point.y)<100000) {
        VIMDragEventObserved(type,(int32_t)llround(point.x),(int32_t)llround(point.y),
            CGEventGetTimestamp(event),mach_absolute_time());
    } else { ++disabled;CFRunLoopStop(CFRunLoopGetCurrent()); }
    return event; // Listen-only; never change, suppress or post an event.
}

int main(int argc,char **argv)
{
    if(argc!=3 || strcmp(argv[1],"--seconds"))return 2;
    char *end=NULL;long seconds=strtol(argv[2],&end,10);
    if(!end || *end || seconds<1 || seconds>30 || geteuid()!=0) {
        fputs("Use guest/reference root and --seconds 1..30; no permissions are changed.\n",stderr);return 2;
    }
    setvbuf(stdout,NULL,_IOLBF,0);
    CGEventMask mask=CGEventMaskBit(kCGEventMouseMoved)|CGEventMaskBit(kCGEventLeftMouseDown)|
        CGEventMaskBit(kCGEventLeftMouseUp)|CGEventMaskBit(kCGEventLeftMouseDragged);
    CFMachPortRef tap=CGEventTapCreate(kCGHIDEventTap,kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,mask,observe,NULL);
    if(!tap){fputs("FAILED passive mouse tap unavailable; no input/permissions changed.\n",stderr);return 1;}
    CFRunLoopSourceRef source=CFMachPortCreateRunLoopSource(kCFAllocatorDefault,tap,0);
    CFRunLoopTimerRef timer=CFRunLoopTimerCreate(kCFAllocatorDefault,CFAbsoluteTimeGetCurrent()+seconds,
        0,0,0,stop,NULL);
    if(!source || !timer){if(source)CFRelease(source);if(timer)CFRelease(timer);CFRelease(tap);return 1;}
    CFRunLoopAddSource(CFRunLoopGetCurrent(),source,kCFRunLoopCommonModes);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(),timer,kCFRunLoopCommonModes);
    mach_timebase_info_data_t timebase;mach_timebase_info(&timebase);
    CGRect bounds=CGDisplayBounds(CGMainDisplayID());
    printf("READY passive mouse observation pid=%d seconds=%ld bounds=%.0f,%.0f,%.0f,%.0f mach_timebase=%u/%u\n",
        getpid(),seconds,bounds.origin.x,bounds.origin.y,bounds.size.width,bounds.size.height,
        timebase.numer,timebase.denom);
    CFRunLoopRun();
    CFRunLoopTimerInvalidate(timer);CFMachPortInvalidate(tap);
    CFRelease(timer);CFRelease(source);CFRelease(tap);
    for(unsigned i=0;i<events;++i) {
        const struct VIMMouseObservation *row=&observations[i];
        printf("MOUSE type=%u x=%d y=%d event_ns=%llu callback_mach=%llu\n",
            row->type,row->x,row->y,(unsigned long long)row->event_ns,
            (unsigned long long)row->callback_mach);
    }
    printf("DONE mouse events=%u disabled=%u\n",events,disabled);
    return disabled?1:0;
}
