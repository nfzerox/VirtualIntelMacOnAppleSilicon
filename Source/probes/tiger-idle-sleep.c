/* Finite guest-only qualification of the same native observer as the agent.
 * Optional idle-minutes changes live kernel aggressiveness only, restoring
 * its old value at completion. Never edits persistent power preferences. */
#include "../tools/opengl-tiger-sleep.h"
#include <IOKit/pwr_mgt/IOPM.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    if(argc!=2&&argc!=3)return 64;
    if(argc==3&&!strcmp(argv[1],"--thread")){
        int duration=atoi(argv[2]);if(duration<1||duration>10)return 64;
        if(!VIMTigerStartIdleSleepGuard())return 77;
        printf("thread-guard-registered=true\n");fflush(stdout);sleep((unsigned)duration);return 0;
    }
    int seconds=atoi(argv[1]),minutes=argc==3?atoi(argv[2]):-1;
    if(seconds<1||seconds>180||minutes>2||minutes< -1)return 64;
    VIMTigerSleepObserver observer;
    if(!VIMTigerRegisterSleepObserver(&observer))return 77;
    unsigned long old=0;
    IOReturn read=IOPMGetAggressiveness(observer.connection,kPMMinutesToSleep,&old);
    if(read!=kIOReturnSuccess){VIMTigerDisposeSleepObserver(&observer);return 1;}
    if(minutes>=0&&IOPMSetAggressiveness(observer.connection,kPMMinutesToSleep,(unsigned long)minutes)!=kIOReturnSuccess){VIMTigerDisposeSleepObserver(&observer);return 1;}
    printf("registered=true prior-idle-minutes=%lu temporary-idle-minutes=%d\n",old,minutes);fflush(stdout);
    CFAbsoluteTime end=CFAbsoluteTimeGetCurrent()+seconds;
    while(CFAbsoluteTimeGetCurrent()<end)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode,end-CFAbsoluteTimeGetCurrent(),false);
    IOReturn restored=minutes>=0?IOPMSetAggressiveness(observer.connection,kPMMinutesToSleep,old):kIOReturnSuccess;
    printf("idle-requests=%u cancel-errors=%u restored=%s\n",observer.idleRequests,observer.cancelErrors,restored==kIOReturnSuccess?"true":"false");
    BOOL passed=!observer.cancelErrors&&restored==kIOReturnSuccess;
    VIMTigerDisposeSleepObserver(&observer);return passed?0:1;
}
