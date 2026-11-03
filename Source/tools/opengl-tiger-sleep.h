#pragma once
#if __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 1050
#include <objc/objc.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/IOMessage.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include "opengl-guest-machine.h"

typedef struct {
    io_connect_t connection;
    IONotificationPortRef notifications;
    io_object_t notifier;
    unsigned idleRequests,cancelErrors;
} VIMTigerSleepObserver;
static void VIMTigerSleepNotification(void *context,io_service_t service,natural_t message,void *argument) {
    (void)service;VIMTigerSleepObserver *observer=context;
    // Public10.4 IOPMLib.h: only CanSystemSleep (idle sleep) is abortable.
    // Acknowledge WillSleep rather than pretending an explicit sleep can be
    // cancelled. QEMU separately does not advertise unsupported S3/S4 states.
    if(message==kIOMessageCanSystemSleep) {
        observer->idleRequests++;
        if(IOCancelPowerChange(observer->connection,(long)argument)!=kIOReturnSuccess)observer->cancelErrors++;
    }else if(message==kIOMessageSystemWillSleep) {
        IOAllowPowerChange(observer->connection,(long)argument);
    }
}
static inline void VIMTigerDisposeSleepObserver(VIMTigerSleepObserver *observer) {
    if(observer->notifier)IODeregisterForSystemPower(&observer->notifier);
    if(observer->connection)IOServiceClose(observer->connection);
    if(observer->notifications)IONotificationPortDestroy(observer->notifications);
    memset(observer,0,sizeof(*observer));
}
static inline BOOL VIMTigerRegisterSleepObserver(VIMTigerSleepObserver *observer) {
    struct utsname kernel;memset(observer,0,sizeof(*observer));
    if(geteuid()!=0||uname(&kernel)||strncmp(kernel.release,"8.",2)||!VIMOpenGLGuestMachine())return NO;
    observer->connection=IORegisterForSystemPower(observer,&observer->notifications,VIMTigerSleepNotification,&observer->notifier);
    CFRunLoopSourceRef source=observer->notifications?IONotificationPortGetRunLoopSource(observer->notifications):NULL;
    if(!observer->connection||!observer->notifier||!source){VIMTigerDisposeSleepObserver(observer);return NO;}
    CFRunLoopAddSource(CFRunLoopGetCurrent(),source,kCFRunLoopDefaultMode);return YES;
}
typedef struct {pthread_mutex_t lock;pthread_cond_t changed;BOOL started,finished,registered;} VIMTigerSleepStart;
static void *VIMTigerSleepGuardThread(void *context) {
    VIMTigerSleepObserver observer;BOOL registered=VIMTigerRegisterSleepObserver(&observer);
    VIMTigerSleepStart *start=context;
    pthread_mutex_lock(&start->lock);start->registered=registered;start->finished=YES;
    pthread_cond_signal(&start->changed);pthread_mutex_unlock(&start->lock);
    // The process-lifetime handoff also remains valid on a wait error.
    if(!registered)return NULL;
    // Native Mach notifications block in CFRunLoop. No timers, JSON, process
    // enumeration, preference writes or per-frame/input-path work.
    CFRunLoopRun();VIMTigerDisposeSleepObserver(&observer);return NULL;
}
static inline BOOL VIMTigerStartIdleSleepGuard(void) {
    static VIMTigerSleepStart start={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,NO,NO,NO};
    pthread_attr_t attributes;pthread_t thread;
    if(pthread_attr_init(&attributes))return NO;
    pthread_mutex_lock(&start.lock);
    int status=0;
    if(!start.started){
        status=pthread_attr_setdetachstate(&attributes,PTHREAD_CREATE_DETACHED);
        if(!status)status=pthread_create(&thread,&attributes,VIMTigerSleepGuardThread,&start);
        if(!status)start.started=YES;
    }
    while(!status&&!start.finished)status=pthread_cond_wait(&start.changed,&start.lock);
    BOOL registered=!status&&start.registered;
    pthread_mutex_unlock(&start.lock);
    pthread_attr_destroy(&attributes);return registered;
}
#endif
