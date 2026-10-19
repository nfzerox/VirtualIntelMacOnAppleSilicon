// Controlled local peer; no QEMU, VM, disk, host input or extra monitor client.
#import <Foundation/Foundation.h>
#import "VIMQMPConnection.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
static void Check(BOOL value,NSString *message){if(!value){fprintf(stderr,"FAIL: %s\n",message.UTF8String);exit(1);}}
static NSDictionary *Read(int fd) {
    NSMutableData *data=[NSMutableData data];char byte;
    while(data.length<65536&&read(fd,&byte,1)==1) {
        if(byte=='\n')return [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        [data appendBytes:&byte length:1];
    }return nil;
}
static void Send(int fd,NSDictionary *object) {
    NSMutableData *data=[[NSJSONSerialization dataWithJSONObject:object options:0 error:NULL] mutableCopy];
    [data appendBytes:"\n" length:1];const uint8_t *p=data.bytes;size_t left=data.length;
    while(left){ssize_t n=write(fd,p,left);Check(n>0,@"Mock reply writes");p+=n;left-=n;}
}
int main(void){@autoreleasepool {
    char directory[]="/tmp/vim-qmp-late.XXXXXX";Check(mkdtemp(directory)!=NULL,@"Private fixture directory");
    NSString *path=[@(directory) stringByAppendingPathComponent:@"qmp"];
    int listener=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un address={.sun_family=AF_UNIX};
    strlcpy(address.sun_path,path.fileSystemRepresentation,sizeof(address.sun_path));
    Check(listener>=0&&!bind(listener,(struct sockaddr *)&address,sizeof(address))&&!listen(listener,1),@"Local listener");
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
        int fd=accept(listener,NULL,NULL);Check(fd>=0,@"Accept owner");
        struct timeval timeout={.tv_sec=15};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        Send(fd,@{@"QMP":@{}});NSDictionary *cap=Read(fd);
        Check([cap[@"execute"] isEqual:@"qmp_capabilities"],@"Handshake");Send(fd,@{@"return":@{},@"id":cap[@"id"]});
        NSDictionary *write=Read(fd);Check([write[@"execute"] isEqual:@"fixture-write-once"],@"One original write");
        // Deliberately exceed the real controller's five-second deadline.
        // The peer executed it, but the owner cannot know that at timeout.
        usleep(6000000);Send(fd,@{@"return":@{@"Accepted":@YES},@"id":write[@"id"]});
        NSDictionary *query=Read(fd);Check([query[@"execute"] isEqual:@"query-status"],@"Timed-out write is never repeated");
        Send(fd,@{@"error":@{@"desc":@"An obsolete error, not this query"},@"id":write[@"id"]});
        Send(fd,@{@"event":@"RESUME"});Send(fd,@{@"return":@{@"status":@"running"},@"id":query[@"id"]});
        NSDictionary *future=Read(fd);Check([future[@"execute"] isEqual:@"fixture-future-id"],@"Fresh next command");
        Send(fd,@{@"return":@{},@"id":@([future[@"id"] unsignedLongLongValue]+1)});
        close(fd);
    });
    VIMQMPConnection *control=[[VIMQMPConnection alloc] initWithSocketPath:path];__block BOOL done=NO;
    [control connectWithCompletion:^(NSError *error){
        Check(!error,@"Connect");
        [control execute:@"fixture-write-once" arguments:@{} completion:^(id value,NSError *timeout){
            Check(timeout!=nil,@"Uncertain original write reports a timeout");
            [control execute:@"query-status" arguments:@{} completion:^(id status,NSError *failure){
                Check(!failure&&[status[@"status"] isEqual:@"running"],@"Ignore expired IDs, consume events, match the fresh query");
                [control execute:@"fixture-future-id" arguments:@{} completion:^(id unused,NSError *future){
                    Check(future!=nil,@"Unexpected future IDs remain errors");done=YES;
                }];
            }];
        }];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:20];
    while(!done&&deadline.timeIntervalSinceNow>0)[NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    Check(done,@"Bounded completion");[control close];close(listener);unlink(path.fileSystemRepresentation);rmdir(directory);
    puts("PASS: uncertain timeout, no write retry, obsolete success/error ignored, events accepted, current reply matched, future ID refused");return 0;
}}
