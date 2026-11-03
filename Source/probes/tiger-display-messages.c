/* Finite, read-only native Darwin8 framebuffer notification/PID probe.
 * No process arguments, private ivar offsets, injected code or policy changes. */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void Interest(void *context,io_service_t service,natural_t message,void *argument)
{
    (void)context;(void)service;(void)argument;
    printf("framebuffer-message=0x%x suspended=%u\n",message,message==kIOMessageServiceIsSuspended);
    fflush(stdout);
}
int main(void)
{
    struct utsname kernel;
    if(geteuid()||uname(&kernel)||strncmp(kernel.release,"8.",2))return 77;
    io_service_t service=IOServiceGetMatchingService(kIOMasterPortDefault,IOServiceMatching("VMsvga2"));
    if(!service)return 77;
    int mib[]={CTL_KERN,KERN_PROC,KERN_PROC_ALL};size_t size=0;
    if(!sysctl(mib,3,NULL,&size,NULL,0)&&size<=4*1024*1024){
        size+=16*sizeof(struct kinfo_proc);struct kinfo_proc *processes=malloc(size);
        if(processes&&!sysctl(mib,3,processes,&size,NULL,0)&&size%sizeof(*processes)==0){
            size_t i;for(i=0;i<size/sizeof(*processes);i++)
                if(!strcmp(processes[i].kp_proc.p_comm,"WindowServer"))
                    printf("WindowServerPID=%d\n",processes[i].kp_proc.p_pid);
        }
        free(processes);
    }
    IONotificationPortRef port=IONotificationPortCreate(kIOMasterPortDefault);
    io_object_t notifier=IO_OBJECT_NULL;
    kern_return_t status=port?IOServiceAddInterestNotification(port,service,kIOGeneralInterest,Interest,NULL,&notifier):KERN_FAILURE;
    printf("interest-status=0x%x\n",status);fflush(stdout);
    if(status==KERN_SUCCESS){
        CFRunLoopAddSource(CFRunLoopGetCurrent(),IONotificationPortGetRunLoopSource(port),kCFRunLoopDefaultMode);
        CFRunLoopRunInMode(kCFRunLoopDefaultMode,30,false);
    }
    if(notifier)IOObjectRelease(notifier);
    if(port)IONotificationPortDestroy(port);
    IOObjectRelease(service);
    return status==KERN_SUCCESS?0:1;
}
