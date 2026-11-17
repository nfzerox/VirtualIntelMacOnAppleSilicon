// Only for a user-confirmed forced power action, never a guest-work timeout.
#ifndef VIM_FORCE_TERMINATION_H
#define VIM_FORCE_TERMINATION_H
#import <Foundation/Foundation.h>
#include <signal.h>
static inline void VIMForceTerminateTask(NSTask *task)
{
    if(!task.running)return;
    [task terminate];
    // Retain the exact child task, not a session's possibly replaced task/PID.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC),dispatch_get_main_queue(),^{
        if(task.running)kill(task.processIdentifier,SIGKILL);
    });
}
#endif
