/* Finite timing isolation for Chess's actual AppKit tracking-loop pattern.
 * No injection, input posting, GPU work, or changes to system timer policy.
 * Event timestamps/callbacks are not presented frames or an FPS benchmark. */
#import <Cocoa/Cocoa.h>
#include <mach/mach_time.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

enum {Count=500};
static int compare(const void *left,const void *right)
{ double a=*(const double *)left,b=*(const double *)right;return (a>b)-(a<b); }
int main(void)
{
    alarm(20); /* Diagnostic watchdog only, never a guest setup timeout. */
    @autoreleasepool {
        [NSApplication sharedApplication];[NSApp finishLaunching];
        mach_timebase_info_data_t clock;mach_timebase_info(&clock);
        double ms=(double)clock.numer/clock.denom/1e6,gaps[Count],sum=0;
        struct timeval wall_before,wall_after;gettimeofday(&wall_before,NULL);
        uint64_t began=mach_absolute_time(),last=began;
        /* Mojave Chess 333.0.1 requests this same 8-ms period and dequeues
         * periodic events in NSEventTrackingRunLoopMode while rotating. */
        [NSEvent startPeriodicEventsAfterDelay:.008 withPeriod:.008];
        unsigned count=0;
        for(;count<Count;++count){
            NSEvent *event=[NSApp nextEventMatchingMask:NSPeriodicMask
                untilDate:[NSDate dateWithTimeIntervalSinceNow:2]
                inMode:NSEventTrackingRunLoopMode dequeue:YES];
            if(!event)break;
            uint64_t now=mach_absolute_time();gaps[count]=(now-last)*ms;
            last=now;sum+=gaps[count];
        }
        [NSEvent stopPeriodicEvents];gettimeofday(&wall_after,NULL);
        double wall_ms=(wall_after.tv_sec-wall_before.tv_sec)*1000.+
            (wall_after.tv_usec-wall_before.tv_usec)/1000.;
        if(!count){fputs("FAIL no AppKit periodic events\n",stderr);return 1;}
        qsort(gaps,count,sizeof(*gaps),compare);
        printf("AppKit tracking events=%u period_requested_ms=8 mean_ms=%.3f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f mach_elapsed_ms=%.3f wall_elapsed_ms=%.3f (callbacks only, not FPS)\n",
            count,sum/count,gaps[count/2],gaps[(count-1)*95/100],gaps[count-1],
            (last-began)*ms,wall_ms);
        return count==Count?0:1;
    }
}
