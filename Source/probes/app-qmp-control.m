#import <Foundation/Foundation.h>
#import "VIMQMPConnection.h"
#include <unistd.h>
// Runs QEMU without a machine, guest memory image or disk. Exercises the actual
// control implementation against QEMU's greeting, commands and error response.
int main(int argc,char **argv) { @autoreleasepool {
    if(argc!=2)return 64;
    char directory[]="/tmp/vim-qmp-test.XXXXXX";
    if(!mkdtemp(directory))return 1;
    NSString *path=[@(directory) stringByAppendingPathComponent:@"qmp"];
    NSTask *task=[NSTask new];task.executableURL=[NSURL fileURLWithPath:@(argv[1])];
    task.arguments=@[@"-machine",@"none",@"-nodefaults",@"-display",@"none",@"-S",
        @"-qmp",[NSString stringWithFormat:@"unix:%@,server=on,wait=off",path]];
    NSError *error=nil;
    if(![task launchAndReturnError:&error]) { NSLog(@"%@",error);return 1; }
    VIMQMPConnection *control=[[VIMQMPConnection alloc] initWithSocketPath:path];
    __block BOOL finished=NO;__block int result=1;
    __block BOOL sawReset=NO;
    control.lifecycleEventHandler=^(NSDictionary *event){if([event[@"event"] isEqual:@"RESET"])sawReset=YES;};
    void (^finish)(BOOL)=^(BOOL success) {
        result=success?0:1;[control close];if(task.running)[task terminate];finished=YES;
    };
    [control connectWithCompletion:^(NSError *connectionError) {
        if(connectionError) { NSLog(@"%@",connectionError);finish(NO);return; }
        [control execute:@"query-status" arguments:@{} completion:^(id status,NSError *statusError) {
            if(statusError || ![status isKindOfClass:NSDictionary.class] || !status[@"status"]) { finish(NO);return; }
            [control execute:@"vim-nonexistent-test-command" arguments:@{} completion:^(id unused,NSError *expectedError) {
                if(!expectedError) { finish(NO);return; }
                [control execute:@"query-name" arguments:@{} completion:^(id name,NSError *nameError) {
                    if(nameError||![name isKindOfClass:NSDictionary.class]){finish(NO);return;}
                    [control execute:@"system_reset" arguments:@{} completion:^(id unused,NSError *resetError){
                        if(resetError){finish(NO);return;}
                        [control execute:@"query-status" arguments:@{} completion:^(id state,NSError *stateError){
                            finish(!stateError&&sawReset);
                        }];
                    }];
                }];
            }];
        }];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:20];
    while(!finished && deadline.timeIntervalSinceNow>0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    if(!finished)finish(NO);
    [task waitUntilExit];unlink(path.fileSystemRepresentation);rmdir(directory);
    puts(result==0?"PASS: QMP handshake, status, structured error, subsequent command, lifecycle event, shutdown":"FAIL: QMP control");
    return result;
} }
