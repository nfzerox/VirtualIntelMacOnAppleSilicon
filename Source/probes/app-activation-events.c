/* Finite, passive Leopard/Snow Leopard activation observation. No window,
 * synthetic input, screen capture, polling or modification of another app.
 * Compare HID-observed clicks and Carbon's real front-process notification;
 * the latter is not proof that the target app has redrawn/presented pixels.
 */
#include <Carbon/Carbon.h>
#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static FILE *output;
static unsigned clicks, switches;
static double now(void) {
    static mach_timebase_info_data_t tb;
    if(!tb.denom)mach_timebase_info(&tb);
    return mach_absolute_time()*(double)tb.numer/tb.denom/1e9;
}
static CGEventRef input(CGEventTapProxy proxy,CGEventType type,CGEventRef event,void *context) {
    (void)proxy;(void)context;
    if(type==kCGEventTapDisabledByTimeout||type==kCGEventTapDisabledByUserInput) {
        fprintf(output,"INVALID event tap disabled\n");return event;
    }
    if(type==kCGEventLeftMouseDown) {
        CGPoint p=CGEventGetLocation(event);double at=now();clicks++;
        fprintf(output,"DOWN %u %.9f event-ns=%llu x=%.1f y=%.1f\n",clicks,at,
            (unsigned long long)CGEventGetTimestamp(event),p.x,p.y);
    }
    return event; // listen-only: never replace, suppress or delay the event
}
static OSStatus front(EventHandlerCallRef next,EventRef event,void *context) {
    (void)next;(void)context;
    double at=now();ProcessSerialNumber psn;pid_t pid=0;
    OSStatus status=GetEventParameter(event,kEventParamProcessID,typeProcessSerialNumber,
                                    NULL,sizeof psn,NULL,&psn);
    if(status==noErr)status=GetProcessPID(&psn,&pid);
    switches++;
    // The notification and listen-only event tap have independent delivery
    // queues: FRONT can arrive before its DOWN callback. Pair offline using
    // the underlying input timestamp, never whichever callback ran last.
    fprintf(output,"FRONT %u %.9f pid=%d status=%d event-time=%.9f\n",
        switches,at,(int)pid,(int)status,GetEventTime(event));
    return noErr;
}
static void finish(EventLoopTimerRef timer,void *context) {
    (void)timer;(void)context;QuitApplicationEventLoop();
}
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"app-activation-events NEW_LOG SECONDS\n");return 64;}
    double seconds=strtod(argv[2],NULL);
    if(!isfinite(seconds)||seconds<1||seconds>30)return 64;
    int fd=open(argv[1],O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0)return 73;
    output=fdopen(fd,"w");if(!output){close(fd);return 73;}
    setvbuf(output,NULL,_IOLBF,0);
    EventTypeSpec spec={kEventClassApplication,kEventAppFrontSwitched};
    EventHandlerUPP handler=NewEventHandlerUPP(front);
    EventHandlerRef registration=NULL;
    OSStatus status=InstallApplicationEventHandler(handler,1,&spec,NULL,&registration);
    CFMachPortRef tap=CGEventTapCreate(kCGHIDEventTap,kCGHeadInsertEventTap,
        kCGEventTapOptionListenOnly,CGEventMaskBit(kCGEventLeftMouseDown),input,NULL);
    if(status!=noErr||!tap) {
        fprintf(output,"INVALID observer unavailable status=%d tap=%d\n",(int)status,tap!=NULL);
        fclose(output);return 77;
    }
    CFRunLoopSourceRef source=CFMachPortCreateRunLoopSource(NULL,tap,0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(),source,kCFRunLoopCommonModes);
    EventLoopTimerUPP ending=NewEventLoopTimerUPP(finish);EventLoopTimerRef timer;
    status=InstallEventLoopTimer(GetMainEventLoop(),seconds,0,ending,NULL,&timer);
    if(status!=noErr){fclose(output);return 77;}
    fprintf(output,"READY %.9f seconds=%.1f\n",now(),seconds);
    RunApplicationEventLoop();
    fprintf(output,"END clicks=%u switches=%u\n",clicks,switches);
    RemoveEventLoopTimer(timer);DisposeEventLoopTimerUPP(ending);
    RemoveEventHandler(registration);DisposeEventHandlerUPP(handler);
    CFMachPortInvalidate(tap);CFRelease(source);CFRelease(tap);fclose(output);
    return 0;
}
