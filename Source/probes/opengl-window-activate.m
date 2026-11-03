/* Finite real Finder activation on the guest OR physical Intel oracle.
 * Read window geometry only before/after; no AX, capture or queries per click.
 */
#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include <mach/mach_time.h>
#include <sys/time.h>
#include <stdio.h>
static double now(void) {
    static mach_timebase_info_data_t tb;if(!tb.denom)mach_timebase_info(&tb);
    return mach_absolute_time()*(double)tb.numer/tb.denom/1e9;
}
static uint64_t wall(void) {struct timeval t;gettimeofday(&t,NULL);return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_usec*1000;}
static void wait_until(double target) {double left;while((left=target-now())>0)usleep((useconds_t)MIN(left*1e6,5000));}
static int window(CGWindowID wid,CGRect *out) {
    NSArray *rows=(id)CGWindowListCopyWindowInfo(kCGWindowListOptionIncludingWindow,wid);int found=0;
    for(NSDictionary *r in rows)if([r[(id)kCGWindowNumber] unsignedIntValue]==wid &&
        [r[(id)kCGWindowOwnerName] isEqual:@"Finder"] &&
        CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)r[(id)kCGWindowBounds],out))found=1;
    [rows release];return found;
}
static void post(CGEventSourceRef source,CGEventType type,CGPoint point,unsigned number) {
    CGEventRef e=CGEventCreateMouseEvent(source,type,point,kCGMouseButtonLeft);
    CGEventSetIntegerValueField(e,kCGMouseEventNumber,number);
    CGEventSetIntegerValueField(e,kCGMouseEventClickState,1);CGEventPost(kCGHIDEventTap,e);CFRelease(e);
}
int main(int argc,char **argv) {@autoreleasepool {
    if(argc!=3){fprintf(stderr,"usage: %s FIRST_FINDER_WINDOW_ID SECOND_FINDER_WINDOW_ID\n",argv[0]);return 2;}
    CGWindowID ids[2]={(CGWindowID)strtoul(argv[1],NULL,10),(CGWindowID)strtoul(argv[2],NULL,10)};
    CGRect before[2];if(!ids[0]||!ids[1]||!window(ids[0],before)||!window(ids[1],before+1))return 2;
    // The first window exposes its left title segment; the second its right.
    CGPoint points[2]={CGPointMake(before[0].origin.x+120,before[0].origin.y+11),
        CGPointMake(CGRectGetMaxX(before[1])-100,before[1].origin.y+11)};
    for(unsigned i=0;i<2;++i)if(CGRectContainsPoint(before[!i],points[i])){
        fprintf(stderr,"Title points overlap another test window; refusing input\n");return 2;}
    CGEventSourceRef source=CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);if(!source)return 2;
    double start=now()+.5;printf("READY real Finder activation %u %u\n",ids[0],ids[1]);fflush(stdout);
    for(unsigned i=0;i<12;++i){
        wait_until(start+i*.7);CGPoint point=points[i%2];
        post(source,kCGEventMouseMoved,point,i+1);wait_until(now()+.04);
        uint64_t at=wall();post(source,kCGEventLeftMouseDown,point,i+1);
        printf("CLICK %u %llu %.9f %g %g\n",i,(unsigned long long)at,now(),point.x,point.y);fflush(stdout);
        wait_until(now()+.04);post(source,kCGEventLeftMouseUp,point,i+1);
    }
    wait_until(now()+.3);CFRelease(source);
    for(unsigned i=0;i<2;++i){CGRect after;if(!window(ids[i],&after)||!CGRectEqualToRect(before[i],after)){
        fprintf(stderr,"Window geometry changed; reject this trial\n");return 1;}}
    puts("PASS fixed real-window geometry. Input timing only, not presented latency.");return 0;
}}
