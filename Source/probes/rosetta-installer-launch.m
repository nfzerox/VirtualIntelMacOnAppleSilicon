// Finite launch check on a host where Intel execution already works.
// Never remove Rosetta or invoke its installer to run this fixture.
#import <AppKit/AppKit.h>
#include <mach/machine.h>

int main(int argc,const char **argv) {
    if(argc!=2)return 64;
    @autoreleasepool {
        NSURL *url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]];
        __block BOOL finished=NO,passed=NO;
        __block NSRunningApplication *launched=nil;
        NSWorkspaceOpenConfiguration *configuration=NSWorkspaceOpenConfiguration.configuration;
        configuration.activates=NO;
        [NSWorkspace.sharedWorkspace openApplicationAtURL:url configuration:configuration
            completionHandler:^(NSRunningApplication *application,NSError *error){
                dispatch_async(dispatch_get_main_queue(),^{
                    launched=application;
                    passed=application!=nil&&error==nil&&application.executableArchitecture==CPU_TYPE_X86_64;
                    if(error)fprintf(stderr,"Launch failed: %s\n",error.description.UTF8String);
                    finished=YES;
                });
            }];
        NSDate *limit=[NSDate dateWithTimeIntervalSinceNow:15];
        while((!finished||(passed&&!launched.terminated))&&limit.timeIntervalSinceNow>0)
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
        if(!finished||!passed||!launched.terminated)return 1;
        puts("PASS: NSWorkspace launches the Intel trigger and it terminates without starting a VM");
        return 0;
    }
}
