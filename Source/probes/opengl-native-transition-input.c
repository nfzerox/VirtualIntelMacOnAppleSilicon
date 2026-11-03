/* Bounded physical-Mavericks comparison input through ordinary CGEventPost.
 * No AX injection, preference/TCC changes, persistent process or test window.
 * This API has no delivery acknowledgement: verify actual transitions in the
 * paired native recording. Successful posting alone is NOT a passing test.
 * Explicit workload is mandatory; invoking with no arguments never posts.
 */
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <mach/mach_time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static CGEventSourceRef source;
static unsigned char held[128];
static volatile sig_atomic_t interrupted;
static uint64_t began;
static mach_timebase_info_data_t timebase;

static double absolute_seconds(uint64_t ticks)
{
    return (double)ticks*timebase.numer/timebase.denom/1e9;
}

static double seconds(void)
{
    return absolute_seconds(mach_absolute_time()-began);
}
static int key(CGKeyCode code,int down)
{
    if(code>=sizeof(held))return 0;
    CGEventRef event=CGEventCreateKeyboardEvent(source,code,down!=0);
    if(!event)return 0;
    unsigned char old=held[code];held[code]=down!=0;
    CGEventFlags flags=(held[kVK_Control]?kCGEventFlagMaskControl:0)|
        (held[kVK_Command]?kCGEventFlagMaskCommand:0);
    CGEventSetFlags(event,flags);
    uint64_t before=mach_absolute_time();
    CGEventPost(kCGHIDEventTap,event);uint64_t after=mach_absolute_time();CFRelease(event);
    fprintf(stderr,"%.6f key=%u down=%d flags=%llx previous=%u absolute_begin=%.9f absolute_end=%.9f\n",
        seconds(),(unsigned)code,down,(unsigned long long)flags,old,
        absolute_seconds(before),absolute_seconds(after));
    return 1;
}
static void release(void)
{
    if(!source)return;
    for(unsigned code=0;code<sizeof(held);++code)
        if(held[code]&&code!=kVK_Control&&code!=kVK_Command)key((CGKeyCode)code,0);
    if(held[kVK_Command])key(kVK_Command,0);
    if(held[kVK_Control])key(kVK_Control,0);
    CFRelease(source);source=NULL;
    fprintf(stderr,"CLOCK native_end=%.9f\n",absolute_seconds(mach_absolute_time()));
}
static void cancel_input(int number){(void)number;interrupted=1;}
static int pause_for(double duration)
{
    double deadline=seconds()+duration;
    while(!interrupted&&seconds()<deadline)usleep(5000);
    return !interrupted;
}
static int chord(CGKeyCode code,int command)
{
    if(!key(kVK_Control,1)||(command&&!key(kVK_Command,1))||!pause_for(.10))return 0;
    if(!key(code,1)||!pause_for(.03)||!key(code,0))return 0;
    if(command&&!key(kVK_Command,0))return 0;
    return key(kVK_Control,0);
}
static int escape(void)
{
    return key(kVK_Escape,1)&&pause_for(.03)&&key(kVK_Escape,0);
}
int main(int argc,char **argv)
{
    const char *usage="usage: opengl-native-transition-input mission-control|app-expose|spaces|fullscreen|launchpad|escape|spotlight-toggle|spotlight-typing\n";
    if(argc!=2){fputs(usage,stderr);return 2;}
    if(!strcmp(argv[1],"--help")){fputs(usage,stdout);return 0;}
    int spaces=!strcmp(argv[1],"spaces"),fullscreen=!strcmp(argv[1],"fullscreen");
    int expose=!strcmp(argv[1],"app-expose"),mission=!strcmp(argv[1],"mission-control");
    int launchpad=!strcmp(argv[1],"launchpad");
    int dismiss=!strcmp(argv[1],"escape");
    int typing=!strcmp(argv[1],"spotlight-typing");
    int spotlight=!strcmp(argv[1],"spotlight-toggle");
    if(!spaces&&!fullscreen&&!expose&&!mission&&!launchpad&&!dismiss&&!typing&&!spotlight){fputs(usage,stderr);return 2;}
    mach_timebase_info(&timebase);began=mach_absolute_time();
    source=CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if(!source){fputs("CGEvent source unavailable; no input sent\n",stderr);return 1;}
    atexit(release);signal(SIGINT,cancel_input);signal(SIGTERM,cancel_input);
    /* The enclosing host command journals send/return monotonic times.
     * These two real Mach anchors bound the guest/host clock offset without
     * assuming symmetric SSH latency or equating their absolute epochs.
     * CGEventPost/LSOpen are requests, NOT delivered/visible acknowledgments.
     * No timer, trace, per-frame work or persistent guest agent is added. */
    fprintf(stderr,"CLOCK native_begin=%.9f\n",absolute_seconds(began));
    fprintf(stderr,"READY native %s input; require visible transition evidence\n",argv[1]);fflush(stderr);
    if(dismiss)return escape()?0:1;
    if(spotlight)return key(kVK_Command,1)&&key(kVK_Space,1)&&pause_for(.05)&&
        key(kVK_Space,0)&&key(kVK_Command,0)?0:1;
    if(typing){
        /* Caller must visibly focus Spotlight's disposable query first.
         * Exercise real result-bearing prefixes, not a long unmatched x query.
         * No Return, clipboard, text-model setters or persistent setting. */
        if(!key(kVK_Command,1)||!key(kVK_ANSI_A,1)||!pause_for(.05)||
           !key(kVK_ANSI_A,0)||!key(kVK_Command,0))return 1;
        const CGKeyCode letters[]={kVK_ANSI_T,kVK_ANSI_E,kVK_ANSI_R,kVK_ANSI_M,
            kVK_ANSI_I,kVK_ANSI_N,kVK_ANSI_A,kVK_ANSI_L};
        for(unsigned cycle=0;cycle<2&&!interrupted;++cycle){
            for(unsigned i=0;i<sizeof(letters)/sizeof(letters[0]);++i)
                if(!key(letters[i],1)||!pause_for(.05)||!key(letters[i],0)||!pause_for(.15))return 1;
            if(!pause_for(.8))return 1;
            for(unsigned i=0;i<sizeof(letters)/sizeof(letters[0]);++i)
                if(!key(kVK_Delete,1)||!pause_for(.05)||!key(kVK_Delete,0)||!pause_for(.15))return 1;
        }
        return interrupted?130:0;
    }
    for(unsigned cycle=0;cycle<2&&!interrupted;++cycle){
        if(launchpad){
            /* Use the real system launcher, not an assumed F4 mapping or
             * synthetic animation. Success is a request, NOT delivery/FPS. */
            CFURLRef app=CFURLCreateWithFileSystemPath(NULL,CFSTR("/Applications/Launchpad.app"),kCFURLPOSIXPathStyle,true);
            if(!app)return 1;
            uint64_t before=mach_absolute_time();
            OSStatus status=LSOpenCFURLRef(app,NULL);uint64_t after=mach_absolute_time();CFRelease(app);
            fprintf(stderr,"%.6f launchpad request status=%d absolute_begin=%.9f absolute_end=%.9f\n",
                seconds(),(int)status,absolute_seconds(before),absolute_seconds(after));
            if(status || !pause_for(.9) || !escape() || !pause_for(.9))return 1;
        }else if(spaces){
            if(!chord(kVK_RightArrow,0)||!pause_for(.9)||
               !chord(kVK_LeftArrow,0)||!pause_for(.9))return 1;
        }else if(fullscreen){
            if(!chord(kVK_ANSI_F,1)||!pause_for(1.2)||
               !chord(kVK_ANSI_F,1)||!pause_for(1.2))return 1;
        }else{
            if(!chord(expose?kVK_DownArrow:kVK_UpArrow,0)||!pause_for(.9)||
               !escape()||!pause_for(.9))return 1;
        }
    }
    return interrupted?130:0;
}
