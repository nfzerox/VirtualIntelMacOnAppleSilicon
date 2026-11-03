/* Bounded read-only guest scroll delivery evidence, no event suppression. */
#include <ApplicationServices/ApplicationServices.h>
#include <stdio.h>
static unsigned count;
static CGEventRef observed(CGEventTapProxy proxy,CGEventType type,CGEventRef event,void *unused)
{
    (void)proxy;(void)unused;
    if(type==kCGEventScrollWheel){
        printf("SCROLL vertical=%lld horizontal=%lld point_y=%lld point_x=%lld\n",
            CGEventGetIntegerValueField(event,kCGScrollWheelEventDeltaAxis1),
            CGEventGetIntegerValueField(event,kCGScrollWheelEventDeltaAxis2),
            CGEventGetIntegerValueField(event,kCGScrollWheelEventPointDeltaAxis1),
            CGEventGetIntegerValueField(event,kCGScrollWheelEventPointDeltaAxis2));
        fflush(stdout);count++;
    }
    return event;
}
static void finished(CFRunLoopTimerRef timer,void *unused)
{(void)timer;(void)unused;CFRunLoopStop(CFRunLoopGetCurrent());}
int main(void)
{
    CFMachPortRef tap=CGEventTapCreate(kCGSessionEventTap,kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,CGEventMaskBit(kCGEventScrollWheel),observed,NULL);
    if(!tap){fputs("Unable to observe guest scroll delivery; no permission changed.\n",stderr);return 2;}
    CFRunLoopSourceRef source=CFMachPortCreateRunLoopSource(NULL,tap,0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(),source,kCFRunLoopCommonModes);
    CFRunLoopTimerRef timer=CFRunLoopTimerCreate(NULL,CFAbsoluteTimeGetCurrent()+16,0,0,0,finished,NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(),timer,kCFRunLoopCommonModes);
    puts("READY scroll observer");fflush(stdout);CFRunLoopRun();
    printf("END scroll observer count=%u\n",count);
    CFRunLoopTimerInvalidate(timer);CFRelease(timer);CFRelease(source);CFRelease(tap);return 0;
}
