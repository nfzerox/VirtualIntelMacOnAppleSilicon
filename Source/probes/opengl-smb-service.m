// Host-only finite native SMB qualification for an existing research VM.
// Use the production local-only DNS-SD publisher, not an ad-hoc TCP relay or
// global client configuration. The VM's normal app still owns QEMU/QMP/QGA.
#import "../VirtualIntelMac/VIMSMBService.h"
#import <signal.h>
@interface VIMSMBProbeMachine : NSObject
@property NSString *name;
@property NSDictionary *configuration;
@end
@implementation VIMSMBProbeMachine
@end
int main(int argc,const char **argv){@autoreleasepool{
    if(argc!=3)return 64;
    NSString *path=[NSString stringWithUTF8String:argv[1]];
    NSDictionary *config=[NSDictionary dictionaryWithContentsOfFile:[path stringByAppendingPathComponent:@"Machine.plist"]];
    int port=atoi(argv[2]);
    if(![[NSUUID alloc] initWithUUIDString:config[@"Identifier"]]||port<1024||port>65535)return 64;
    VIMSMBProbeMachine *machine=[VIMSMBProbeMachine new];
    machine.name=path.lastPathComponent.stringByDeletingPathExtension;machine.configuration=config;
    VIMSMBService *service=[[VIMSMBService alloc] initWithMachine:(id)machine port:(uint16_t)port];
    NSError *error=nil;
    if(![service start:&error]){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
    printf("%s\n",service.URL.absoluteString.UTF8String);fflush(stdout);
    NSDate *end=[NSDate dateWithTimeIntervalSinceNow:120];
    while(end.timeIntervalSinceNow>0&&!service.error)
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.2]];
    BOOL failed=service.error!=nil;[service stop];return failed?1:0;
}}
