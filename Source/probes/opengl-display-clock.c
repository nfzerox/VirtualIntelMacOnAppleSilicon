/* Read-only Mavericks display clock survey. This measures callbacks, NOT
 * rendered/presented frames. --require-intel binds the physical comparison to
 * the main display's actual Intel framebuffer ancestry, not a GPU guess. */
#include <ApplicationServices/ApplicationServices.h>
#include <CoreVideo/CoreVideo.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SAMPLES 120
struct Clock { pthread_mutex_t lock;pthread_cond_t ready;unsigned count;
    uint64_t times[SAMPLES],flags;int64_t period;int32_t scale; };
static CVReturn output(CVDisplayLinkRef link,const CVTimeStamp *now,const CVTimeStamp *next,
    CVOptionFlags input,CVOptionFlags *out,void *opaque)
{
    (void)link;(void)now;(void)input;(void)out;struct Clock *c=opaque;
    pthread_mutex_lock(&c->lock);
    if(c->count<SAMPLES){c->times[c->count++]=mach_absolute_time();c->flags=next->flags;
        c->period=next->videoRefreshPeriod;c->scale=next->videoTimeScale;}
    if(c->count==SAMPLES)pthread_cond_signal(&c->ready);
    pthread_mutex_unlock(&c->lock);return kCVReturnSuccess;
}
static int compare(const void *x,const void *y)
{double a=*(const double*)x,b=*(const double*)y;return a<b?-1:a>b;}
int main(int argc,char **argv)
{
    int requireIntel=argc==2 && !strcmp(argv[1],"--require-intel");
    if(argc>1 && !requireIntel)return 2;
    CGDirectDisplayID display=CGMainDisplayID();
    io_registry_entry_t entry=CGDisplayIOServicePort(display);int intel=0;
    puts("{\"DisplayAncestry\":[");
    for(unsigned i=0;entry && i<16;++i){
        io_name_t name;IOObjectGetClass(entry,name);if(strstr(name,"Intel"))intel=1;
        printf("%s\"%s\"",i?",":"",name);
        io_registry_entry_t parent=0;IOReturn error=IORegistryEntryGetParentEntry(entry,kIOServicePlane,&parent);
        if(i)IOObjectRelease(entry);entry=error?0:parent;
    }
    puts("],");
    if(requireIntel && !intel){fputs("Reference display is not Intel-owned. Refusing comparison.\n",stderr);return 1;}
    CVDisplayLinkRef link=0;CVReturn error=CVDisplayLinkCreateWithCGDisplay(display,&link);
    if(error || !link){printf("\"CreateError\":%d}\n",error);return 1;}
    CVTime nominal=CVDisplayLinkGetNominalOutputVideoRefreshPeriod(link);
    struct Clock c={.lock=PTHREAD_MUTEX_INITIALIZER,.ready=PTHREAD_COND_INITIALIZER};
    error=CVDisplayLinkSetOutputCallback(link,output,&c);if(!error)error=CVDisplayLinkStart(link);
    struct timeval now;gettimeofday(&now,NULL);struct timespec deadline={now.tv_sec+10,now.tv_usec*1000};
    pthread_mutex_lock(&c.lock);
    while(!error && c.count<SAMPLES){int waited=pthread_cond_timedwait(&c.ready,&c.lock,&deadline);if(waited)break;}
    pthread_mutex_unlock(&c.lock);
    double actual=CVDisplayLinkGetActualOutputVideoRefreshPeriod(link);CVDisplayLinkStop(link);CVDisplayLinkRelease(link);
    printf("\"StartError\":%d,\"Samples\":%u,\"NominalValue\":%lld,\"NominalScale\":%d,\"NominalFlags\":%d,"
        "\"MeasuredPeriodSeconds\":%.9f,\"TimestampFlags\":%llu,\"VideoPeriod\":%lld,\"VideoScale\":%d,",
        error,c.count,(long long)nominal.timeValue,nominal.timeScale,nominal.flags,actual,
        (unsigned long long)c.flags,(long long)c.period,c.scale);
    if(c.count<2){puts("\"ClockUsable\":false}");return 1;}
    mach_timebase_info_data_t tb;mach_timebase_info(&tb);double gaps[SAMPLES-1],sum=0;
    for(unsigned i=1;i<c.count;++i){gaps[i-1]=(c.times[i]-c.times[i-1])*(double)tb.numer/tb.denom/1e6;sum+=gaps[i-1];}
    qsort(gaps,c.count-1,sizeof(*gaps),compare);
    printf("\"CallbackGapMilliseconds\":{\"Mean\":%.3f,\"P50\":%.3f,\"P95\":%.3f,\"Max\":%.3f},"
        "\"Caution\":\"Display-clock callbacks only, not graphics FPS or presentation evidence.\"}\n",
        sum/(c.count-1),gaps[(c.count-1)/2],gaps[(c.count-2)*95/100],gaps[c.count-2]);
    return error || c.count!=SAMPLES;
}
