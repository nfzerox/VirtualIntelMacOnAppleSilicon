#pragma once
#include <DiskArbitration/DiskArbitration.h>

/* The genuine 10.4 and 10.5 SDKs export this idle callback, including
 * delivery when already idle. Boot services can precede asynchronous volume
 * publication. Wait for that native state without polling or weakening the
 * caller's volume checks. This short-lived caller remains cancellable. */
extern void DARegisterIdleCallback(DASessionRef,void (*)(void *),void *);
static void VIMOpenGLDisksIdle(void *context) {
    *(Boolean *)context=true;CFRunLoopStop(CFRunLoopGetCurrent());
}
static inline void VIMOpenGLWaitForDisks(DASessionRef session) {
    Boolean ready=false;CFRunLoopRef loop=CFRunLoopGetCurrent();
    DARegisterIdleCallback(session,VIMOpenGLDisksIdle,&ready);
    DASessionScheduleWithRunLoop(session,loop,kCFRunLoopDefaultMode);
    while(!ready)CFRunLoopRun();
    DASessionUnscheduleFromRunLoop(session,loop,kCFRunLoopDefaultMode);
    DAUnregisterCallback(session,(void *)VIMOpenGLDisksIdle,&ready);
}
