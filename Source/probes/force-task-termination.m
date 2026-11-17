// Finite owned-child test. Never signals a VM or another existing process.
#import "../compat/VIMForceTermination.h"
#include <assert.h>
static void stop(BOOL ignoresTerm)
{
    NSTask *task=[NSTask new];NSPipe *output=[NSPipe pipe];
    task.executableURL=[NSURL fileURLWithPath:@"/bin/sh"];
    task.arguments=@[@"-c",ignoresTerm?@"trap '' TERM; printf ready; exec /bin/sleep 30":@"printf ready; exec /bin/sleep 30"];
    task.standardOutput=output;NSError *error=nil;assert([task launchAndReturnError:&error]);
    NSData *ready=[output.fileHandleForReading availableData];assert(ready.length==5);
    NSDate *start=[NSDate date];VIMForceTerminateTask(task);
    while(task.running&&-[start timeIntervalSinceNow]<6)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    assert(!task.running&&task.terminationReason==NSTaskTerminationReasonUncaughtSignal);
    assert(task.terminationStatus==(ignoresTerm?SIGKILL:SIGTERM));
    printf("PASS owned child ignores-TERM=%d signal=%d elapsed=%.3f\n",ignoresTerm,task.terminationStatus,-[start timeIntervalSinceNow]);
}
int main(void){@autoreleasepool{stop(NO);stop(YES);}return 0;}
