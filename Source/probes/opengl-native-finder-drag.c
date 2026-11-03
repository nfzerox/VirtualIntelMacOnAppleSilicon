/* Finite Snow-or-later native Finder mouse workload. No AppKit registration,
 * AppleEvents during motion, AX mutation, GL injection or persistent policy.
 * Same public HID input on a real Intel reference or guest. This emits input;
 * the separate compositor trace measures work, not this command's duration.
 * Usage: PROGRAM FINDER_WINDOW_ID SECONDS (1..12)
 *        PROGRAM --reversals FINDER_WINDOW_ID SECONDS AMPLITUDE_X AMPLITUDE_Y
 *        PROGRAM --place FINDER_WINDOW_ID X Y WIDTH HEIGHT
 * --place explicitly changes only that real Finder window's geometry through
 * its native title bar/resize grip, verifying observed bounds after each step.
 * It is preparation, NOT timing or an implicit part of the measured drag.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static double clock_seconds(void)
{
    static mach_timebase_info_data_t scale;
    if(!scale.denom)mach_timebase_info(&scale);
    return (double)mach_absolute_time()*scale.numer/scale.denom/1e9;
}
static void wait_until(double deadline)
{
    double remaining;
    while((remaining=deadline-clock_seconds())>0)usleep((useconds_t)fmin(remaining*1e6,1000));
}
static void post(CGEventSourceRef source,CGEventType type,CGPoint p)
{
    CGEventRef event=CGEventCreateMouseEvent(source,type,p,kCGMouseButtonLeft);
    CGEventSetIntegerValueField(event,kCGMouseEventClickState,1);
    CGEventPost(kCGHIDEventTap,event);CFRelease(event);
}
static int bounds(CGWindowID id,CGRect *r)
{
    CFArrayRef list=CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,id);
    int found=0;
    for(CFIndex i=0;list&&i<CFArrayGetCount(list);i++) {
        CFDictionaryRef d=CFArrayGetValueAtIndex(list,i);int n=0,layer=-1;
        CFNumberGetValue(CFDictionaryGetValue(d,kCGWindowNumber),kCFNumberIntType,&n);
        CFNumberGetValue(CFDictionaryGetValue(d,kCGWindowLayer),kCFNumberIntType,&layer);
        CFStringRef owner=CFDictionaryGetValue(d,kCGWindowOwnerName);
        if((unsigned)n==id && !layer && owner && CFEqual(owner,CFSTR("Finder")))
            found=CGRectMakeWithDictionaryRepresentation(CFDictionaryGetValue(d,kCGWindowBounds),r);
    }
    if(list)CFRelease(list);return found;
}
static int same_rect(CGRect a,CGRect b)
{
    return fabs(a.origin.x-b.origin.x)<3 && fabs(a.origin.y-b.origin.y)<3 &&
        fabs(a.size.width-b.size.width)<3 && fabs(a.size.height-b.size.height)<3;
}
static int observed(CGWindowID id,CGRect expected,CGRect *r)
{
    double deadline=clock_seconds()+3;
    do {
        if(bounds(id,r) && same_rect(*r,expected))return 1;
        usleep(10000);
    }while(clock_seconds()<deadline);
    return 0;
}
static void prepare_drag(CGEventSourceRef source,CGPoint from,CGPoint to)
{
    post(source,kCGEventMouseMoved,from);wait_until(clock_seconds()+.05);
    post(source,kCGEventLeftMouseDown,from);
    double start=clock_seconds();
    for(unsigned i=1;i<=30;i++){
        wait_until(start+i/100.);
        post(source,kCGEventLeftMouseDragged,CGPointMake(from.x+(to.x-from.x)*i/30.,from.y+(to.y-from.y)*i/30.));
    }
    post(source,kCGEventLeftMouseUp,to);
}
static int place(int argc,char **argv)
{
    if(argc!=7)return 64;
    double v[5];
    for(unsigned i=0;i<5;i++){
        char *end=NULL;v[i]=strtod(argv[i+2],&end);
        if(!*argv[i+2] || *end || !isfinite(v[i]) || floor(v[i])!=v[i])return 64;
    }
    if(v[0]<1 || v[0]>UINT32_MAX || v[3]<400 || v[4]<200)return 64;
    CGWindowID id=(CGWindowID)v[0];CGRect r=CGRectZero,target=CGRectMake(v[1],v[2],v[3],v[4]);
    CGRect display=CGDisplayBounds(CGMainDisplayID());
    if(!CGRectContainsRect(CGRectInset(display,8,30),target) || !bounds(id,&r))return 65;
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    if(!source)return 66;
    CGEventRef event=CGEventCreate(NULL);CGPoint cursor=CGEventGetLocation(event);CFRelease(event);
    int good=1;
    if(fabs(r.size.width-target.size.width)>2 || fabs(r.size.height-target.size.height)>2){
        CGRect sized=r;sized.size=target.size;
        prepare_drag(source,CGPointMake(CGRectGetMaxX(r)-6,CGRectGetMaxY(r)-6),
            CGPointMake(CGRectGetMaxX(sized)-6,CGRectGetMaxY(sized)-6));
        good=observed(id,sized,&r);
    }
    if(good && !same_rect(r,target)){
        prepare_drag(source,CGPointMake(r.origin.x+200,r.origin.y+10),
            CGPointMake(target.origin.x+200,target.origin.y+10));
        good=observed(id,target,&r);
    }
    post(source,kCGEventMouseMoved,cursor);CFRelease(source);
    printf("%s Finder window=%u bounds=%.0f,%.0f %.0fx%.0f (preparation, not FPS)\n",
        good?"PREPARED":"FAILED",id,r.origin.x,r.origin.y,r.size.width,r.size.height);
    return good?0:1;
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--place"))return place(argc,argv);
    int reversals=argc>1&&!strcmp(argv[1],"--reversals");
    if(reversals){argc--;argv++;}
    char *end=NULL;unsigned long id=argc==(reversals?5:3)?strtoul(argv[1],&end,10):0;
    if(!id || id>UINT32_MAX || !end || *end)return 64;
    double seconds=strtod(argv[2],&end);
    if(*end || !isfinite(seconds) || seconds<1 || seconds>12)return 64;
    CGRect r=CGRectZero,display=CGDisplayBounds(CGMainDisplayID());
    if(!bounds((CGWindowID)id,&r) || r.size.width<400 || r.size.height<200)return 65;
    double ax=fmin(140,fmin(r.origin.x-12,CGRectGetMaxX(display)-CGRectGetMaxX(r)-12));
    double ay=fmin(60,CGRectGetMaxY(display)-CGRectGetMaxY(r)-85);
    if(reversals){
        ax=strtod(argv[3],&end);if(!*argv[3]||*end||!isfinite(ax))return 64;
        ay=strtod(argv[4],&end);if(!*argv[4]||*end||!isfinite(ay))return 64;
        if(!CGRectContainsRect(CGRectInset(display,8,30),
            CGRectMake(r.origin.x-ax,r.origin.y-ay,r.size.width+2*ax,r.size.height+2*ay)))return 65;
    }
    if(ax<20 || ay<15)return 65;
    CGPoint origin=CGPointMake(r.origin.x+200,r.origin.y+10);
    CGEventRef cursor=CGEventCreate(NULL);CGPoint original=CGEventGetLocation(cursor);CFRelease(cursor);
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
    if(!source)return 66;
    setvbuf(stdout,NULL,_IOLBF,0);
    post(source,kCGEventMouseMoved,origin);wait_until(clock_seconds()+.15);
    post(source,kCGEventLeftMouseDown,origin);wait_until(clock_seconds()+.15);
    double start=clock_seconds();unsigned events=0;
    printf("READY Finder window=%lu bounds=%.0f,%.0f %.0fx%.0f amplitude=%.0f,%.0f seconds=%.2f pattern=%s\n",
        id,r.origin.x,r.origin.y,r.size.width,r.size.height,ax,ay,seconds,reversals?"reversals":"circle");
    for(unsigned i=0;;i++) {
        wait_until(start+i/120.0);double t=fmin(1,(clock_seconds()-start)/seconds),a=t*4*M_PI;
        double dx=sin(a),dy=(1-cos(a))*.5;
        if(reversals){
            static const double points[][2]={{0,0},{1,1},{-1,-1},{1,-1},{-1,1},{.6,-.5},{-.6,.5},{0,0},
                {-.7,-.8},{.8,.7},{-.5,.8},{.7,-.7},{-1,0},{1,0},{0,1},{0,0}};
            double segment=t*15;unsigned step=(unsigned)fmin(14,floor(segment));double f=segment-step;
            dx=points[step][0]*(1-f)+points[step+1][0]*f;
            dy=points[step][1]*(1-f)+points[step+1][1]*f;
        }
        post(source,kCGEventLeftMouseDragged,CGPointMake(origin.x+ax*dx,origin.y+ay*dy));
        events++;if(t>=1)break;
    }
    post(source,kCGEventLeftMouseUp,origin);wait_until(clock_seconds()+.4);
    CGRect after=CGRectZero;int verified=bounds((CGWindowID)id,&after) &&
        fabs(after.origin.x-r.origin.x)<3 && fabs(after.origin.y-r.origin.y)<3 &&
        fabs(after.size.width-r.size.width)<3 && fabs(after.size.height-r.size.height)<3;
    post(source,kCGEventMouseMoved,original);CFRelease(source);
    printf("DONE events=%u final_geometry_restored=%d\n",events,verified);
    return verified?0:1;
}
