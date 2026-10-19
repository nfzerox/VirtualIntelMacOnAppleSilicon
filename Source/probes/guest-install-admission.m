#import <Foundation/Foundation.h>
#import "VIMGuestAgent.h"
int main(int argc,const char **argv){@autoreleasepool {
    if(argc!=4&&argc!=6)return 64;
    NSArray *arguments=argc==4?@[@"--check-environment"]:@[@(argv[4]),@(argv[5])];
    if(argc==6&&![@[@"--apply",@"--rollback",@"--verify-after-reboot"] containsObject:arguments[0]])return 64;
    VIMGuestAgent *agent=[[VIMGuestAgent alloc] initWithSocketPath:@(argv[1])];
    __block BOOL done=NO;__block int code=1;
    [agent connectWithCompletion:^(NSError *connectionError){
    if(connectionError){NSLog(@"%@",connectionError);done=YES;return;}
    [agent stageFiles:@{@"guest-macho":[NSURL fileURLWithPath:@(argv[2])],@"guest-install":[NSURL fileURLWithPath:@(argv[3])]} progress:^(uint64_t a,uint64_t b){}
        completion:^(NSDictionary *receipt,NSError *error){
        if(error){NSLog(@"%@",error);done=YES;return;}
        NSString *binary=[receipt[@"Directory"] stringByAppendingPathComponent:@"guest-install"];
        NSLog(@"Verified staging retained at %@",receipt[@"Directory"]);
        [agent runExecutable:@"/bin/chmod" arguments:@[@"0700",binary] timeout:15 completion:^(NSDictionary *mode,NSError *modeError){
            if(modeError||![mode[@"exitcode"] isEqual:@0]){NSLog(@"chmod failed %@",modeError);done=YES;return;}
            [agent runExecutable:binary arguments:arguments timeout:argc==4?30:1200 completion:^(NSDictionary *result,NSError *failure){
                if(failure)NSLog(@"%@",failure);
                else {fwrite([result[@"stdout"] bytes],1,[result[@"stdout"] length],stdout);fwrite([result[@"stderr"] bytes],1,[result[@"stderr"] length],stderr);if([result[@"exitcode"] isEqual:@0])code=0;}
                done=YES;
            }];
        }];
    }];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:argc==4?180:1320];
    while(!done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    [agent close];return code;
}}
