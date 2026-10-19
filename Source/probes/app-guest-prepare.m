#import <Foundation/Foundation.h>
#import "VIMGuestAgent.h"
#import "VIMGuestPreparation.h"
#import "VIMQMPConnection.h"
// Executes the same coordinator as --prepare-vm. No system installation occurs.
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc!=5)return 64;
    VIMGuestAgent *agent=[[VIMGuestAgent alloc] initWithSocketPath:@(argv[1])];
    VIMQMPConnection *control=[[VIMQMPConnection alloc] initWithSocketPath:@(argv[2])];
    NSURL *runtime=[NSURL fileURLWithPath:@(argv[3])],*journal=[NSURL fileURLWithPath:@(argv[4])];
    NSData *tool=[NSData dataWithContentsOfURL:[runtime URLByAppendingPathComponent:@"GuestTools/guest-status"]];
    __block BOOL done=NO;__block int code=1;__block NSString *last=nil;
    [control connectWithCompletion:^(NSError *controlError) {
    if(controlError){NSLog(@"%@",controlError);done=YES;return;}
    [agent connectWithCompletion:^(NSError *error) {
        if(error){NSLog(@"%@",error);done=YES;return;}
        [agent collectStatusWithTool:tool completion:^(NSDictionary *report,NSError *failure) {
            if(failure){NSLog(@"%@",failure);done=YES;return;}
            [VIMGuestPreparation prepareWithAgent:agent report:report runtime:runtime journal:journal control:control
                progress:^(NSString *message){if(![last isEqual:message]){NSLog(@"%@",message);last=message;}}
                completion:^(NSDictionary *receipt,NSError *problem) {
                    if(problem)NSLog(@"%@",problem);
                    else {NSData *data=[NSJSONSerialization dataWithJSONObject:receipt options:NSJSONWritingPrettyPrinted error:NULL];fwrite(data.bytes,1,data.length,stdout);puts("");code=0;}
                    done=YES;
                }];
        }];
    }];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:1800];
    while(!done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    [agent close];[control close];return code;
}}
