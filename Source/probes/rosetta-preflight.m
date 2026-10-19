// Bounded host-only check; no VM, GPU work or Rosetta installation/removal.
// Run from a test .app bundle with Runtime/bin/apple-gfx-helper set to either
// this ARM-only binary (expected: missing), the real Intel helper (ready), or
// absent (broken). With no arguments this also serves as the thin ARM helper.
#import "../VirtualIntelMac/VIMHostSupport.h"
#include <stdio.h>
int main(int argc,const char **argv) {
    if(argc==1)return 64;
    @autoreleasepool {
        NSString *expected=[NSString stringWithUTF8String:argv[1]];
        __block BOOL finished=NO,passed=NO;
        VIMCheckRosettaForBoot(^(BOOL missing,NSError *error){
            NSCAssert(NSThread.isMainThread,@"Completion must return to the UI thread");
            NSString *actual=error?@"broken":(missing?@"missing":@"ready");
            passed=[actual isEqual:expected];finished=YES;
            printf("%s: expected %s, observed %s\n",passed?"PASS":"FAIL",expected.UTF8String,actual.UTF8String);
        });
        NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:12];
        while(!finished&&deadline.timeIntervalSinceNow>0)
            [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
        return finished&&passed?0:1;
    }
}
