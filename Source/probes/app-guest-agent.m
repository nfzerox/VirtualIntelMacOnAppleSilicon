#import <Foundation/Foundation.h>
#import "VIMQMPConnection.h"
#import "VIMGuestAgent.h"

// Read-only live qualification of the production guest-agent transport.
int main(int argc, char **argv) { @autoreleasepool {
    if(argc>=4&&!strcmp(argv[1],"--exec")) {
        VIMGuestAgent *guest=[[VIMGuestAgent alloc] initWithSocketPath:@(argv[2])];
        NSMutableArray *arguments=[NSMutableArray array];for(int i=4;i<argc;i++)[arguments addObject:@(argv[i])];
        __block BOOL done=NO;__block int code=1;
        [guest connectWithCompletion:^(NSError *error) {
            if(error){NSLog(@"%@",error);done=YES;return;}
            [guest runExecutable:@(argv[3]) arguments:arguments timeout:600 completion:^(NSDictionary *result,NSError *failure) {
                if(failure)NSLog(@"%@",failure);
                else {
                    fwrite([result[@"stdout"] bytes],1,[result[@"stdout"] length],stdout);
                    fwrite([result[@"stderr"] bytes],1,[result[@"stderr"] length],stderr);
                    NSLog(@"Guest exit: %@ signal: %@",result[@"exitcode"],result[@"signal"]);
                    if([result[@"exitcode"] isEqual:@0]&&!result[@"signal"])code=0;
                }done=YES;
            }];
        }];
        NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:620];
        while(!done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        [guest close];return code;
    }
    if(argc==4) {
        VIMGuestAgent *guest=[[VIMGuestAgent alloc] initWithSocketPath:@(argv[1])];
        __block BOOL done=NO;__block int code=1;
        [guest connectWithCompletion:^(NSError *error) {
            if(error){NSLog(@"%@",error);done=YES;return;}
            [guest stageFiles:@{@"guest-macho":[NSURL fileURLWithPath:@(argv[2])],@"transfer-test":[NSURL fileURLWithPath:@(argv[3])]}
                progress:^(uint64_t completed,uint64_t total){if(completed==total)NSLog(@"Transferred %llu bytes",completed);}
                completion:^(NSDictionary *receipt,NSError *failure) {
                if(failure){NSLog(@"Transfer failed: %@ %@",failure,failure.userInfo);done=YES;return;}
                NSData *json=[NSJSONSerialization dataWithJSONObject:receipt options:NSJSONWritingPrettyPrinted error:NULL];
                fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);
                NSString *directory=receipt[@"Directory"];
                NSArray *paths=@[@"-f",[directory stringByAppendingPathComponent:@"guest-macho"],[directory stringByAppendingPathComponent:@"transfer-test"]];
                [guest runExecutable:@"/bin/rm" arguments:paths timeout:15 completion:^(NSDictionary *removed,NSError *removeError) {
                    if(removeError||![removed[@"exitcode"] isEqual:@0]){NSLog(@"Cleanup failed: %@",removeError);done=YES;return;}
                    [guest runExecutable:@"/bin/rmdir" arguments:@[directory] timeout:15 completion:^(NSDictionary *result,NSError *cleanupError) {
                        if(!cleanupError&&[result[@"exitcode"] isEqual:@0])code=0;
                        else NSLog(@"Directory cleanup failed: %@",cleanupError);
                        done=YES;
                    }];
                }];
            }];
        }];
        NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:750];
        while(!done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        [guest close];return code;
    }
    if(argc==3) {
        NSData *tool=[NSData dataWithContentsOfFile:@(argv[2])];
        if(!tool)return 66;
        VIMGuestAgent *guest=[[VIMGuestAgent alloc] initWithSocketPath:@(argv[1])];
        __block BOOL done=NO;__block int code=1;
        [guest connectWithCompletion:^(NSError *error) {
            if(error){NSLog(@"%@",error);done=YES;return;}
            [guest collectStatusWithTool:tool completion:^(NSDictionary *report,NSError *failure) {
                if(failure)NSLog(@"%@",failure);
                else {
                    NSData *json=[NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:NULL];
                    fwrite(json.bytes,1,json.length,stdout);fputc('\n',stdout);code=0;
                }
                done=YES;
            }];
        }];
        NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:150];
        while(!done&&deadline.timeIntervalSinceNow>0)
            [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        [guest close];return code;
    }
    if(argc!=2)return 64;
    VIMQMPConnection *agent=[[VIMQMPConnection alloc] initWithGuestAgentSocketPath:@(argv[1])];
    __block BOOL finished=NO; __block int result=1;
    [agent connectWithCompletion:^(NSError *error) {
        if(error){NSLog(@"Synchronization: %@",error);finished=YES;return;}
        [agent execute:@"guest-info" arguments:@{} completion:^(id info,NSError *infoError) {
            if(infoError || ![info isKindOfClass:NSDictionary.class] || ![info[@"version"] isKindOfClass:NSString.class]) {
                NSLog(@"Guest info: %@",infoError);finished=YES;return;
            }
            NSLog(@"Guest agent: %@",info[@"version"]);
            [agent execute:@"guest-ping" arguments:@{} completion:^(id reply,NSError *pingError) {
                result=pingError?1:0;if(pingError)NSLog(@"Ping: %@",pingError);finished=YES;
            }];
        }];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:25];
    while(!finished && deadline.timeIntervalSinceNow>0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    [agent close];
    puts(result?"FAIL: guest-agent transport":"PASS: guest-agent synchronization, info and ping");
    return result;
} }
