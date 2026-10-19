// Exercise EOF/event ordering on the production control channel, no VM/disk.
#import <Foundation/Foundation.h>
#import "VIMQMPConnection.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static void Check(BOOL value){if(!value){fprintf(stderr,"FAIL: exit event ordering\n");exit(1);}}
static void Send(int fd,NSDictionary *object) {
    NSMutableData *data=[[NSJSONSerialization dataWithJSONObject:object options:0 error:NULL] mutableCopy];
    [data appendBytes:"\n" length:1];const char *bytes=data.bytes;NSUInteger left=data.length;
    while(left){ssize_t n=write(fd,bytes,left);Check(n>0);bytes+=n;left-=n;}
}
int main(void){@autoreleasepool {
    char directory[]="/tmp/vim-qmp-exit.XXXXXX";Check(mkdtemp(directory)!=NULL);
    NSString *path=[@(directory) stringByAppendingPathComponent:@"control"];
    int listener=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un address={.sun_family=AF_UNIX};
    strlcpy(address.sun_path,path.fileSystemRepresentation,sizeof(address.sun_path));
    Check(listener>=0&&!bind(listener,(struct sockaddr *)&address,sizeof(address))&&!listen(listener,1));
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
        int peer=accept(listener,NULL,NULL);Check(peer>=0);Send(peer,@{@"QMP":@{}});
        NSMutableData *line=[NSMutableData data];char byte;
        while(read(peer,&byte,1)==1&&byte!='\n'){[line appendBytes:&byte length:1];Check(line.length<65536);}
        NSDictionary *cap=[NSJSONSerialization JSONObjectWithData:line options:0 error:NULL];
        Check([cap[@"execute"] isEqual:@"qmp_capabilities"]);
        Send(peer,@{@"return":@{},@"id":cap[@"id"]});
        // No subsequent request will read this. It must survive process-exit
        // teardown, including when the event and EOF arrive together.
        Send(peer,@{@"event":@"SHUTDOWN",@"data":@{@"guest":@YES,@"reason":@"guest-reset"}});close(peer);
    });
    VIMQMPConnection *control=[[VIMQMPConnection alloc] initWithSocketPath:path];
    __block BOOL done=NO;__block NSUInteger events=0;
    control.lifecycleEventHandler=^(NSDictionary *event){Check(NSThread.isMainThread);Check([event[@"data"][@"reason"] isEqual:@"guest-reset"]);events++;};
    [control connectWithCompletion:^(NSError *error){Check(!error);
        [control drainLifecycleEventsAfterExitWithCompletion:^{Check(events==1);Check(NSThread.isMainThread);done=YES;}];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:5];
    while(!done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    Check(done);[control close];close(listener);unlink(path.fileSystemRepresentation);rmdir(directory);
    puts("PASS: queued guest-reset event delivered before clean-exit completion, no replay/second client");
}return 0;}
