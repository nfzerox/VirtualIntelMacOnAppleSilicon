/* Finite real-window input for Tiger and later. Prepare/confirm the actual
 * Finder window outside this timed command; no AppleEvents, AX queries, GL
 * injection or stdout traffic during motion. Uses the genuine 10.4 HID API.
 * PROGRAM title_x title_y amplitude_x amplitude_y seconds
 */
#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static double now(void) {
    static mach_timebase_info_data_t scale;
    if(!scale.denom)mach_timebase_info(&scale);
    return (double)mach_absolute_time()*scale.numer/scale.denom/1e9;
}
static void until(double at) {
    double delay;
    while((delay=at-now())>0)usleep((useconds_t)fmin(delay*1e6,1000));
}
int main(int argc,char **argv) {
    if(argc!=6)return 64;
    double v[5];
    for(unsigned i=0;i<5;i++) {
        char *end;v[i]=strtod(argv[i+1],&end);
        if(!*argv[i+1]||*end||!isfinite(v[i]))return 64;
    }
    double x=v[0],y=v[1],ax=v[2],ay=v[3],seconds=v[4];
    CGRect display=CGDisplayBounds(CGMainDisplayID());
    if(seconds<1||seconds>12||ax<1||ay<1||
       !CGRectContainsRect(display,CGRectMake(x-ax,y,2*ax,ay)))return 64;
    CGEventRef event=CGEventCreate(NULL);
    if(!event)return 1;
    CGPoint original=CGEventGetLocation(event);CFRelease(event);
    CGPoint origin=CGPointMake(x,y);
    if(CGPostMouseEvent(origin,1,1,0))return 1;
    until(now()+.15);
    if(CGPostMouseEvent(origin,1,1,1))return 1;
    until(now()+.15);
    double began=now();unsigned sent=0;int good=1;
    for(unsigned i=0;;i++) {
        until(began+i/120.);double t=fmin(1,(now()-began)/seconds),angle=4*M_PI*t;
        CGPoint p=CGPointMake(x+ax*sin(angle),y+ay*(1-cos(angle))*.5);
        if(CGPostMouseEvent(p,1,1,1)){good=0;break;}
        sent++;if(t>=1)break;
    }
    if(CGPostMouseEvent(origin,1,1,0))good=0;
    until(now()+.4);CGPostMouseEvent(original,1,1,0);
    printf("%s native drag events=%u motion_seconds=%.3f origin=%.0f,%.0f amplitude=%.0f,%.0f (input, not FPS)\n",
        good?"DONE":"FAILED",sent,now()-began-.4,x,y,ax,ay);
    return good?0:1;
}
