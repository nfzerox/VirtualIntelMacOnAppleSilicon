/* Finite real-window input for Tiger and later. Prepare/confirm the actual
 * Finder window outside this timed command; no AppleEvents, AX queries, GL
 * injection or stdout traffic during motion. Uses the genuine 10.4 HID API.
 * PROGRAM title_x title_y amplitude_x amplitude_y seconds [marker_x marker_y [--vbl]]
 * Optional Intel scanout measurement reads only the neighborhood of the
 * real red close button. It records the marker position, not screen contents.
 * --vbl also samples the native IOFramebuffer clock during that same drag.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/graphics/IOGraphicsLib.h>
#include <IOKit/graphics/IOFramebufferShared.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct marker_sample {
    double at;
    int x,y;
};
static struct marker_sample samples[2048];
static struct clock_sample {
    double at, vbl;
    uint64_t count;
} clocks[2048];
static struct input_sample {
    double scheduled, ready, posted;
} input_delays[2048];
static mach_timebase_info_data_t scale;

static double seconds_for_ticks(uint64_t ticks) {
    if(!scale.denom)mach_timebase_info(&scale);
    return (double)ticks*scale.numer/scale.denom/1e9;
}
static double now(void) {
    return seconds_for_ticks(mach_absolute_time());
}
static uint64_t clock_word(const volatile void *p) {
    uint64_t result;
    const volatile unsigned char *src=p;
    unsigned char *dest=(unsigned char *)&result;
    for(unsigned i=0;i<sizeof(result);++i)dest[i]=src[i];
    return result;
}
static void until(double at) {
    double delay;
    while((delay=at-now())>0)usleep((useconds_t)fmin(delay*1e6,1000));
}

static int locate_marker(const volatile unsigned char *base,size_t stride,
                         int *x,int *y,int width,int height) {
    int sx=0,sy=0,n=0;
    const int reach=18;
    for(int row=*y-reach;row<=*y+reach;row++) {
        if(row<0||row>=height)continue;
        const volatile uint32_t *pixels=(const volatile uint32_t *)(base+row*stride);
        for(int col=*x-reach;col<=*x+reach;col++) {
            if(col<0||col>=width)continue;
            uint32_t p=pixels[col]; // period Intel's 32-bit BGRA framebuffer
            int b=p&255,g=(p>>8)&255,r=(p>>16)&255;
            if(r<170||r-g<85||r-b<70)continue;
            sx+=col;sy+=row;n++;
        }
    }
    if(n<12||n>165)return 0; // don't mistake a white or fully colored region for a button
    *x=(sx+n/2)/n;*y=(sy+n/2)/n;return 1;
}

int main(int argc,char **argv) {
    int clocked=argc==9&&!strcmp(argv[8],"--vbl");
    if(clocked)--argc;
    int observe=argc==8;
    if(argc!=6&&!observe)return 64;
    double v[7];
    for(int i=0;i<argc-1;i++) {
        char *end;v[i]=strtod(argv[i+1],&end);
        if(!*argv[i+1]||*end||!isfinite(v[i]))return 64;
    }
    double x=v[0],y=v[1],ax=v[2],ay=v[3],seconds=v[4];
    CGDirectDisplayID id=CGMainDisplayID();
    CGRect display=CGDisplayBounds(id);
    if(seconds<1||seconds>12||ax<1||ay<1||
       !CGRectContainsRect(display,CGRectMake(x-ax,y,2*ax,ay)))return 64;
    CGEventRef event=CGEventCreate(NULL);
    if(!event)return 1;
    CGPoint original=CGEventGetLocation(event);CFRelease(event);
    const volatile unsigned char *base=NULL;
    int mx=0,my=0,width=0,height=0,start_x=0,start_y=0;
    size_t stride=0;
    if(observe) {
        width=(int)CGDisplayPixelsWide(id);height=(int)CGDisplayPixelsHigh(id);
        stride=CGDisplayBytesPerRow(id);base=CGDisplayBaseAddress(id);
        if(!base||CGDisplayBitsPerPixel(id)!=32||stride<(size_t)width*4||
           v[5]<0||v[5]>=width||v[6]<0||v[6]>=height) {
            fprintf(stderr,"No readable 32-bit display at the marker location.\n");return 1;
        }
        mx=(int)v[5];my=(int)v[6];
        if(!locate_marker(base,stride,&mx,&my,width,height)) {
            fprintf(stderr,"Could not calibrate the scanout close-button marker.\n");return 1;
        }
        start_x=mx;start_y=my;
        fprintf(stderr,"Calibrated scanout marker at %d,%d.\n",mx,my);
    }
    CGPoint origin=CGPointMake(x,y);
    if(CGPostMouseEvent(origin,1,1,0))return 1;
    until(now()+.15);
    if(CGPostMouseEvent(origin,1,1,1))return 1;
    until(now()+.15);
    unsigned sent=0,recorded=0,missing=0;int good=1;
    io_connect_t connection=0;
    vm_address_t clock_mapping=0;
    vm_size_t clock_size=0;
    unsigned clock_records=0, unstable=0, input_records=0;
    const volatile StdFBShmem_t *panel=NULL;
    if(clocked) {
        io_service_t service=CGDisplayIOServicePort(id);
        IOReturn rc=service?IOServiceOpen(service,mach_task_self(),kIOFBSharedConnectType,&connection):kIOReturnNotFound;
        if(!rc)rc=IOConnectMapMemory(connection,kIOFBCursorMemory,mach_task_self(),
            &clock_mapping,&clock_size,kIOMapAnywhere|kIOMapReadOnly);
        if(rc || clock_size<sizeof(*panel)) {
            fprintf(stderr,"Framebuffer clock unavailable (%x, %lu bytes).\n",rc,(unsigned long)clock_size);
            good=0;
        } else panel=(const volatile StdFBShmem_t *)(uintptr_t)clock_mapping;
    }
    double began=now();
    for(unsigned i=0;good;i++) {
        const double scheduled=i/120.;
        until(began+scheduled);
        const double ready=now()-began;
        double t=fmin(1,ready/seconds),angle=4*M_PI*t;
        CGPoint p=CGPointMake(x+ax*sin(angle),y+ay*(1-cos(angle))*.5);
        if(CGPostMouseEvent(p,1,1,1)){good=0;break;}
        if(panel) {
            uint64_t count=clock_word(&panel->vblCount),tick=clock_word(&panel->vblTime);
            double at=now(),when=seconds_for_ticks(tick);
            if(input_records<sizeof(input_delays)/sizeof(input_delays[0])&&
               (ready-scheduled>.004 || at-began-ready>.003))
                input_delays[input_records++]=(struct input_sample){scheduled,ready,at-began};
            if(count!=clock_word(&panel->vblCount)||tick!=clock_word(&panel->vblTime)||
               !tick||when>at||at-when>.25)++unstable;
            else if(clock_records<sizeof(clocks)/sizeof(clocks[0])&&
                    (!clock_records||clocks[clock_records-1].vbl!=when-began))
                clocks[clock_records++]=(struct clock_sample){at-began,when-began,count};
        }
        if(observe) {
            if(recorded<sizeof(samples)/sizeof(samples[0])&&
               locate_marker(base,stride,&mx,&my,width,height))
                samples[recorded++]=(struct marker_sample){now()-began,mx,my};
            else missing++;
        }
        sent++;if(t>=1)break;
    }
    double ended=now();
    if(CGPostMouseEvent(origin,1,1,0))good=0;
    until(now()+.4);CGPostMouseEvent(original,1,1,0);
    if(clock_mapping)IOConnectUnmapMemory(connection,kIOFBCursorMemory,mach_task_self(),clock_mapping);
    if(connection)IOServiceClose(connection);
    printf("%s native drag events=%u motion_seconds=%.3f origin=%.0f,%.0f amplitude=%.0f,%.0f (input, not FPS)\n",
        good?"DONE":"FAILED",sent,ended-began,x,y,ax,ay);
    if(observe) {
        unsigned changed=0;
        int min_x=start_x,max_x=start_x,min_y=start_y,max_y=start_y;
        for(unsigned i=0;i<recorded;i++) {
            const struct marker_sample *sample=&samples[i];
            if(sample->x<min_x)min_x=sample->x;
            if(sample->x>max_x)max_x=sample->x;
            if(sample->y<min_y)min_y=sample->y;
            if(sample->y>max_y)max_y=sample->y;
            if(i&&sample->x==samples[i-1].x&&sample->y==samples[i-1].y)continue;
            printf("marker %.6f %d %d\n",sample->at,sample->x,sample->y);changed++;
        }
        // Verify actual window travel, not just a cursor or a static red
        // object. Allow one search radius of compositor lag at either end.
        int traveled = max_x-min_x>=2*ax-18 && max_y-min_y>=ay-18 &&
                       ax>=40 && ay>=40 &&
                       abs(mx-start_x)<=18 && abs(my-start_y)<=18;
        printf("Scanout marker changes=%u samples=%u missing=%u travel=%d,%d valid=%d (not panel FPS)\n",
               changed,recorded,missing,max_x-min_x,max_y-min_y,
               traveled&&changed&&missing<=sent/10);
        if(!traveled||!changed||missing>sent/10)good=0;
    }
    if(panel) {
        for(unsigned i=0;i<clock_records;++i)
            printf("vbl %.6f %.6f %llu\n",clocks[i].at,clocks[i].vbl,(unsigned long long)clocks[i].count);
        printf("VBL sampled=%u unstable=%u\n",clock_records,unstable);
        for(unsigned i=0;i<input_records;++i)
            printf("delay %.6f %.6f %.6f\n",input_delays[i].scheduled,input_delays[i].ready,input_delays[i].posted);
    }
    return good&&(!clocked||clock_records>=2)?0:1;
}
