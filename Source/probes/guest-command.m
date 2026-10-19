// Development diagnostic using the same authenticated VirtIO agent transport
// as the app; no TCP dependency and no shell interpolation of guest commands.
#import <Foundation/Foundation.h>
#import "VIMGuestAgent.h"
int main(int argc,const char **argv) {@autoreleasepool {
    if(argc<3){fprintf(stderr,"usage: guest-command SOCKET /absolute/executable [arguments...]\n");return 64;}
    VIMGuestAgent *agent=[[VIMGuestAgent alloc] initWithSocketPath:@(argv[1])];
    NSMutableArray *arguments=[NSMutableArray new];for(int i=3;i<argc;i++)[arguments addObject:@(argv[i])];
    __block BOOL finished=NO;__block int status=1;
    [agent connectWithCompletion:^(NSError *error){
        if(error){fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);finished=YES;return;}
        [agent runExecutable:@(argv[2]) arguments:arguments timeout:60 completion:^(NSDictionary *reply,NSError *failure){
            if(failure)fprintf(stderr,"%s\n",failure.localizedDescription.UTF8String);
            else {NSData *out=reply[@"stdout"],*err=reply[@"stderr"];fwrite(out.bytes,1,out.length,stdout);fwrite(err.bytes,1,err.length,stderr);status=[reply[@"exitcode"] intValue];}
            finished=YES;
        }];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:75];
    while(!finished&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.02]];
    [agent close];return status;
}}
