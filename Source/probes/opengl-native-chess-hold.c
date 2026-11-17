/* Finite native counterpart of measure-chess-fixed-deflection. Choose a
 * visible wooden board edge in stock Chess first. The one deflection lets
 * Chess's own periodic tracking events rotate the camera, not our event rate.
 * No AX, injection, display-mode change or persistent policy. */
#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t interrupted;
static void stop(int signal) { (void)signal;interrupted=1; }
static int post(CGEventSourceRef source,CGEventType type,CGPoint point)
{
    CGEventRef event=CGEventCreateMouseEvent(source,type,point,kCGMouseButtonLeft);
    if(!event)return 0;
    CGEventSetIntegerValueField(event,kCGMouseEventClickState,1);
    CGEventPost(kCGHIDEventTap,event);CFRelease(event);return 1;
}
int main(int argc,char **argv)
{
    if(argc!=5){fprintf(stderr,"usage: %s X Y DX SECONDS (1..12)\n",argv[0]);return 64;}
    double values[4];
    for(unsigned i=0;i<4;i++){
        char *end=NULL;values[i]=strtod(argv[i+1],&end);
        if(!*argv[i+1]||*end||!isfinite(values[i]))return 64;
    }
    CGPoint from=CGPointMake(values[0],values[1]),to=CGPointMake(values[0]+values[2],values[1]);
    CGRect screen=CGDisplayBounds(CGMainDisplayID());
    if(!values[2]||values[3]<1||values[3]>12||
       !CGRectContainsPoint(screen,from)||!CGRectContainsPoint(screen,to))return 64;
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    CGEventRef event=CGEventCreate(NULL);
    if(!source||!event){if(source)CFRelease(source);if(event)CFRelease(event);return 1;}
    CGPoint saved=CGEventGetLocation(event);CFRelease(event);
    signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGHUP,stop);
    int good=post(source,kCGEventMouseMoved,from);
    usleep(100000);
    good=post(source,kCGEventLeftMouseDown,from)&&good;
    usleep(100000);
    good=post(source,kCGEventLeftMouseDragged,to)&&good;
    mach_timebase_info_data_t scale;mach_timebase_info(&scale);
    uint64_t start=mach_absolute_time();
    while(good&&!interrupted&&
          (mach_absolute_time()-start)*(double)scale.numer/scale.denom/1e9<values[3])usleep(10000);
    good=post(source,kCGEventLeftMouseUp,to)&&good;
    good=post(source,kCGEventMouseMoved,saved)&&good;
    CFRelease(source);
    printf("%s native Chess deflection %.0f,%.0f -> %.0f,%.0f; %.2fs requested; mouse released.\n",
           good?"POSTED":"FAILED",from.x,from.y,to.x,to.y,values[3]);
    return interrupted?130:good?0:1;
}
