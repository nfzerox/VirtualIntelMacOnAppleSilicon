// Read-only IPC transport probe: no helper, root access, USB or VM required.
#import <Foundation/Foundation.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
@protocol SocketProbe
- (void)inspect:(NSFileHandle *)handle reply:(void (^)(NSDictionary *))reply;
@end
static NSDictionary *Describe(int fd) {
    struct sockaddr_un address={0};socklen_t size=sizeof(address);errno=0;
    int named=getsockname(fd,(struct sockaddr *)&address,&size),nameError=errno;
    int accepting=0;size=sizeof(accepting);errno=0;int accepted=getsockopt(fd,SOL_SOCKET,SO_ACCEPTCONN,&accepting,&size),acceptError=errno;
    int type=0;size=sizeof(type);errno=0;int typed=getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size),typeError=errno;
    return @{@"NameResult":@(named),@"NameErrno":@(nameError),@"Family":@(address.sun_family),@"AcceptResult":@(accepted),@"AcceptErrno":@(acceptError),@"Accepting":@(accepting),@"TypeResult":@(typed),@"TypeErrno":@(typeError),@"Type":@(type)};
}
@interface Probe : NSObject <SocketProbe,NSXPCListenerDelegate>
@end
@implementation Probe
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    connection.exportedInterface=[NSXPCInterface interfaceWithProtocol:@protocol(SocketProbe)];connection.exportedObject=self;[connection resume];return YES;
}
- (void)inspect:(NSFileHandle *)handle reply:(void (^)(NSDictionary *))reply {
    int fd=dup(handle.fileDescriptor);reply(Describe(fd));close(fd);
}
@end
int main(void){@autoreleasepool {
    NSString *directory=[NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    [NSFileManager.defaultManager createDirectoryAtPath:directory withIntermediateDirectories:NO attributes:nil error:nil];
    NSString *path=[directory stringByAppendingPathComponent:@"s"];
    int fd=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un address={0};address.sun_family=AF_UNIX;strlcpy(address.sun_path,path.fileSystemRepresentation,sizeof(address.sun_path));
    NSCAssert(fd>=0&&!bind(fd,(struct sockaddr *)&address,sizeof(address))&&!listen(fd,1),@"Listener creation");
    NSLog(@"Sender: %@",Describe(fd));
    NSXPCListener *listener=NSXPCListener.anonymousListener;Probe *probe=[Probe new];listener.delegate=probe;[listener resume];
    NSXPCConnection *connection=[[NSXPCConnection alloc] initWithListenerEndpoint:listener.endpoint];connection.remoteObjectInterface=[NSXPCInterface interfaceWithProtocol:@protocol(SocketProbe)];[connection resume];
    NSFileHandle *handle=[[NSFileHandle alloc] initWithFileDescriptor:fd closeOnDealloc:YES];
    id<SocketProbe> proxy=[connection remoteObjectProxyWithErrorHandler:^(NSError *error){NSLog(@"FAIL: %@",error);exit(1);}];
    [proxy inspect:handle reply:^(NSDictionary *result){NSLog(@"Receiver: %@",result);[handle closeAndReturnError:NULL];unlink(path.fileSystemRepresentation);rmdir(directory.fileSystemRepresentation);exit(0);}];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,5*NSEC_PER_SEC),dispatch_get_global_queue(0,0),^{exit(2);});
    [[NSRunLoop currentRunLoop] run];
}return 0;}
