// Local fake peer: exercise the real QGA delimiter parser, never a live VM.
#import <Foundation/Foundation.h>
#import "VIMQMPConnection.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void Check(BOOL value, const char *message) {
    if (!value) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}
static BOOL Write(int fd, const void *bytes, size_t length) {
    const char *cursor=bytes;
    while (length) {
        ssize_t count=write(fd,cursor,length);
        if (count<0&&errno==EINTR) continue;
        if (count<=0) return NO;
        cursor+=count; length-=count;
    }
    return YES;
}
static NSDictionary *Read(int fd) {
    NSMutableData *data=[NSMutableData data]; unsigned char byte;
    while(data.length<65536&&read(fd,&byte,1)==1) {
        if(byte==0xff) continue;
        if(byte=='\n') return [NSJSONSerialization JSONObjectWithData:data options:0 error:NULL];
        [data appendBytes:&byte length:1];
    }
    return nil;
}
static BOOL Send(int fd, NSDictionary *object) {
    NSData *json=[NSJSONSerialization dataWithJSONObject:object options:0 error:NULL];
    return Write(fd,json.bytes,json.length)&&Write(fd,"\n",1);
}
int main(void) { @autoreleasepool {
    char directory[]="/tmp/vim-qga-resync.XXXXXX";
    Check(mkdtemp(directory)!=NULL,"Private fixture directory");
    NSString *path=[@(directory) stringByAppendingPathComponent:@"agent"];
    int listener=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un address={.sun_family=AF_UNIX};
    strlcpy(address.sun_path,path.fileSystemRepresentation,sizeof(address.sun_path));
    Check(listener>=0&&!bind(listener,(struct sockaddr *)&address,sizeof(address))&&!listen(listener,1),"Local listener");
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{
        int fd=accept(listener,NULL,NULL); Check(fd>=0,"Accept local owner");
        int one=1; setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof(one));
        struct timeval timeout={.tv_sec=10};
        setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        NSDictionary *sync=Read(fd);
        Check([sync[@"execute"] isEqual:@"guest-sync-delimited"],"Real QGA synchronization request");
        // Old output is not a new response: it must be discarded as it arrives,
        // even when much larger than the retained JSON response limit.
        char stale[4096]; memset(stale,'x',sizeof(stale)); stale[17]='\n';
        for(unsigned i=0;i<1536;i++) if(!Write(fd,stale,sizeof(stale))){close(fd);return;}
        const unsigned char delimiter=0xff;
        if(!Write(fd,&delimiter,1)||!Send(fd,@{@"return":sync[@"arguments"][@"id"]})) {close(fd);return;}
        NSDictionary *query=Read(fd);
        Check([query[@"execute"] isEqual:@"fixture-status"],"No old command is replayed");
        if(!Send(fd,@{@"return":@{@"working":@YES}})){close(fd);return;}
        NSDictionary *large=Read(fd);
        Check([large[@"execute"] isEqual:@"fixture-large"],"Fresh oversized response control");
        // The same data AFTER synchronization must still be refused.
        for(unsigned i=0;i<258;i++) if(!Write(fd,stale+18,sizeof(stale)-18))break;
        close(fd);
    });
    VIMQMPConnection *agent=[[VIMQMPConnection alloc] initWithGuestAgentSocketPath:path];
    __block BOOL done=NO;
    [agent connectWithCompletion:^(NSError *error){
        Check(!error,"Discard six MiB of stale output and synchronize");
        [agent execute:@"fixture-status" arguments:@{} completion:^(id result,NSError *failure){
            Check(!failure&&[result[@"working"] isEqual:@YES],"Fresh response after delimiter");
            [agent execute:@"fixture-large" arguments:@{} completion:^(id ignored,NSError *oversized){
                Check(oversized&&[oversized.localizedDescription containsString:@"size limit"],"Fresh response limit remains enforced");
                done=YES;
            }];
        }];
    }];
    NSDate *deadline=[NSDate dateWithTimeIntervalSinceNow:20];
    while(!done&&deadline.timeIntervalSinceNow>0)
        [NSRunLoop.currentRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    Check(done,"Finite fixture completion");
    [agent close]; close(listener); unlink(path.fileSystemRepresentation); rmdir(directory);
    puts("PASS: discard stale QGA stream through delimiter, match fresh reply, no replay, retain fresh response size limit");
    return 0;
}}
