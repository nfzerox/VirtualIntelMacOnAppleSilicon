// A network-only capability service. No commands, paths, disk access, routing
// changes or executable uploads. QEMU and the GUI never acquire root privileges.
#import "VIMNetworkProtocol.h"
#import "VIMNetworkTrust.h"
#import <Security/Security.h>
#import <vmnet/vmnet.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
static NSObject *budgetLock;
static NSUInteger activeNetworks;

@interface VIMNetworkWorker : NSObject
@property interface_ref interface;
@property dispatch_queue_t events;
@property int socket;
@property int wakeRead;
@property int wakeWrite;
@property NSUInteger maximum;
@property BOOL cancelled;
@property BOOL ownsBudget;
- (void)cancel;
- (void)run;
@end
@implementation VIMNetworkWorker
- (instancetype)init {if((self=[super init])){_socket=_wakeRead=_wakeWrite=-1;_events=dispatch_queue_create("com.mac.virtualintelmac.vmnet",DISPATCH_QUEUE_SERIAL);}return self;}
- (void)signal { @synchronized(self){if(_wakeWrite>=0){uint8_t byte=1;(void)write(_wakeWrite,&byte,1);}} }
- (void)cancel {@synchronized(self){_cancelled=YES;}[self signal];}
- (void)dealloc {if(_ownsBudget)@synchronized(budgetLock){activeNetworks--;}}
- (void)run {@autoreleasepool {
    NSMutableData *incoming=[NSMutableData new],*outgoing=[NSMutableData new];
    uint8_t *packet=malloc(self.maximum);BOOL readNetwork=YES;BOOL writeBlocked=NO;
    if(!packet){[self cancel];}
    while(packet) {
        @synchronized(self){if(_cancelled)break;}
        // Bounded framing is exactly QEMU's stream socket Ethernet protocol:
        // a network-order uint32 length followed by one Ethernet frame.
        while(incoming.length>=4) {
            uint32_t count;memcpy(&count,incoming.bytes,4);count=ntohl(count);
            if(count<14||count>self.maximum){[self cancel];break;}
            if(incoming.length<4+count)break;
            struct iovec iov={(uint8_t *)incoming.mutableBytes+4,count};
            struct vmpktdesc frame={.vm_pkt_size=count,.vm_pkt_iov=&iov,.vm_pkt_iovcnt=1};int number=1;
            vmnet_return_t status=vmnet_write(self.interface,&frame,&number);
            writeBlocked=status==VMNET_BUFFER_EXHAUSTED||(status==VMNET_SUCCESS&&!number);
            if(writeBlocked)break;
            if(status!=VMNET_SUCCESS){[self cancel];break;}
            [incoming replaceBytesInRange:NSMakeRange(0,4+count) withBytes:NULL length:0];
        }
        if(readNetwork&&outgoing.length<256*1024) {
            for(NSUInteger batch=0;batch<64&&outgoing.length<256*1024;batch++) {
                struct iovec iov={packet,self.maximum};struct vmpktdesc frame={.vm_pkt_size=self.maximum,.vm_pkt_iov=&iov,.vm_pkt_iovcnt=1};int number=1;
                vmnet_return_t status=vmnet_read(self.interface,&frame,&number);
                if(status!=VMNET_SUCCESS){[self cancel];break;}
                if(!number){readNetwork=NO;break;}
                if(frame.vm_pkt_size<14||frame.vm_pkt_size>self.maximum){[self cancel];break;}
                uint32_t length=htonl((uint32_t)frame.vm_pkt_size);[outgoing appendBytes:&length length:4];[outgoing appendBytes:packet length:frame.vm_pkt_size];
            }
        }
        if(outgoing.length) {
            ssize_t sent=send(self.socket,outgoing.bytes,outgoing.length,0);
            if(sent>0)[outgoing replaceBytesInRange:NSMakeRange(0,sent) withBytes:NULL length:0];
            else if(sent<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)break;
        }
        // No idle timer or polling spin. Only a genuinely backpressured vmnet
        // write needs a short retry; events/socket readiness wake everything else.
        struct pollfd ready[2]={{self.socket,(incoming.length<self.maximum+4&&!writeBlocked?POLLIN:0)|(outgoing.length?POLLOUT:0),0},{self.wakeRead,POLLIN,0}};
        int result=poll(ready,2,writeBlocked?1:(readNetwork&&outgoing.length<256*1024?0:-1));
        if(result<0){if(errno==EINTR)continue;break;}
        if(ready[0].revents&(POLLHUP|POLLERR|POLLNVAL))break;
        if(ready[1].revents&POLLIN){uint8_t drain[128];while(read(self.wakeRead,drain,sizeof(drain))>0){}readNetwork=YES;}
        if(ready[0].revents&POLLIN) {
            uint8_t data[16384];size_t room=self.maximum+4-incoming.length;
            ssize_t received=recv(self.socket,data,MIN(room,sizeof(data)),0);
            if(!received)break;
            if(received>0)[incoming appendBytes:data length:received];
            else if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)break;
        }
    }
    free(packet);
    vmnet_interface_set_event_callback(self.interface,VMNET_INTERFACE_PACKETS_AVAILABLE,NULL,NULL);
    // Drain already-enqueued event blocks before closing their wake descriptor.
    dispatch_sync(self.events,^{});
    vmnet_stop_interface(self.interface,self.events,^(vmnet_return_t status){});
    @synchronized(self){if(_socket>=0)close(_socket);if(_wakeRead>=0)close(_wakeRead);if(_wakeWrite>=0)close(_wakeWrite);_socket=_wakeRead=_wakeWrite=-1;}
}}
@end

@interface VIMNetworkService : NSObject <NSXPCListenerDelegate,VIMNetworkHelperProtocol>
@property VIMNetworkWorker *worker;
@property BOOL starting;
@property BOOL invalidated;
@end
@implementation VIMNetworkService
- (BOOL)listener:(NSXPCListener *)listener shouldAcceptNewConnection:(NSXPCConnection *)connection {
    if(connection.effectiveUserIdentifier==0)return NO;
    if(@available(macOS 13.0,*))[connection setCodeSigningRequirement:VIM_NETWORK_CLIENT_REQUIREMENT];else return NO;
    SecCodeRef code=NULL;SecRequirementRef requirement=NULL;
    NSDictionary *attributes=@{(__bridge NSString *)kSecGuestAttributePid:@(connection.processIdentifier)};
    OSStatus status=SecCodeCopyGuestWithAttributes(NULL,(__bridge CFDictionaryRef)attributes,0,&code);
    if(!status)status=SecRequirementCreateWithString((__bridge CFStringRef)VIM_NETWORK_CLIENT_REQUIREMENT,0,&requirement);
    if(!status)status=SecCodeCheckValidity(code,0,requirement);
    if(code)CFRelease(code);if(requirement)CFRelease(requirement);if(status)return NO;
    VIMNetworkService *client=[VIMNetworkService new];connection.exportedObject=client;
    connection.exportedInterface=[NSXPCInterface interfaceWithProtocol:@protocol(VIMNetworkHelperProtocol)];
    connection.invalidationHandler=^{@synchronized(client){client.invalidated=YES;[client.worker cancel];}};
    [connection resume];return YES;
}
- (void)statusWithReply:(void (^)(NSDictionary *))reply {reply(@{@"Protocol":@(VIM_NETWORK_PROTOCOL),@"SourceHash":VIM_NETWORK_SOURCE_HASH});}
- (void)startMode:(NSString *)mode bridge:(NSString *)bridge reply:(void (^)(NSFileHandle *,NSDictionary *,NSError *))reply {
    @synchronized(self){if(self.starting||self.worker||self.invalidated){reply(nil,nil,VIMNetworkError(@"This network connection is already in use."));return;}self.starting=YES;}
    if(![@[@"host",@"bridged"] containsObject:mode]||![bridge isKindOfClass:NSString.class]){self.starting=NO;reply(nil,nil,VIMNetworkError(@"Choose a supported network mode."));return;}
    if([mode isEqual:@"bridged"]) {
        xpc_object_t interfaces=vmnet_copy_shared_interface_list();__block BOOL found=NO;
        if(interfaces)xpc_array_apply(interfaces,^bool(size_t index,xpc_object_t item){if(xpc_get_type(item)==XPC_TYPE_STRING&&[bridge isEqual:@(xpc_string_get_string_ptr(item))])found=YES;return true;});
        if(!found){self.starting=NO;reply(nil,nil,VIMNetworkError(@"That bridge interface is unavailable. Choose another interface in Network settings."));return;}
    }
    VIMNetworkWorker *worker=[VIMNetworkWorker new];self.worker=worker;
    @synchronized(budgetLock){
        if(activeNetworks>=16){self.worker=nil;self.starting=NO;reply(nil,nil,VIMNetworkError(@"All virtual network connections are in use. Stop another virtual Mac and try again."));return;}
        activeNetworks++;worker.ownsBudget=YES;
    }
    xpc_object_t description=xpc_dictionary_create(NULL,NULL,0);
    xpc_dictionary_set_uint64(description,vmnet_operation_mode_key,[mode isEqual:@"host"]?VMNET_HOST_MODE:VMNET_BRIDGED_MODE);
    xpc_dictionary_set_bool(description,vmnet_allocate_mac_address_key,false);
    if([mode isEqual:@"bridged"])xpc_dictionary_set_string(description,vmnet_shared_interface_name_key,bridge.UTF8String);
    @synchronized(worker){worker.interface=vmnet_start_interface(description,worker.events,^(vmnet_return_t status,xpc_object_t parameters){
        // Publish the returned reference before callback work can use it.
        @synchronized(worker){}
        @synchronized(self){self.starting=NO;}
        uint64_t maximum=parameters?xpc_dictionary_get_uint64(parameters,vmnet_max_packet_size_key):0;
        int sockets[2]={-1,-1},wake[2]={-1,-1};BOOL invalid;
        @synchronized(self){invalid=self.invalidated;}
        if(status!=VMNET_SUCCESS||invalid||maximum<1514||maximum>65536||socketpair(AF_UNIX,SOCK_STREAM,0,sockets)||pipe(wake)) {
            if(sockets[0]>=0)close(sockets[0]);if(sockets[1]>=0)close(sockets[1]);
            if(worker.interface)vmnet_stop_interface(worker.interface,worker.events,^(vmnet_return_t ignored){});
            self.worker=nil;reply(nil,nil,VIMNetworkError([NSString stringWithFormat:@"macOS could not start this network (status %u). Check that the selected network is available.",status]));return;
        }
        worker.socket=sockets[0];worker.wakeRead=wake[0];worker.wakeWrite=wake[1];worker.maximum=maximum;
        for(int index=0;index<2;index++){fcntl(sockets[index],F_SETFD,FD_CLOEXEC);fcntl(wake[index],F_SETFD,FD_CLOEXEC);fcntl(wake[index],F_SETFL,O_NONBLOCK);}
        fcntl(worker.socket,F_SETFL,O_NONBLOCK);int yes=1;setsockopt(worker.socket,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof(yes));
        __weak VIMNetworkWorker *weakWorker=worker;
        vmnet_return_t events=vmnet_interface_set_event_callback(worker.interface,VMNET_INTERFACE_PACKETS_AVAILABLE,worker.events,^(interface_event_t mask,xpc_object_t information){[weakWorker signal];});
        if(events!=VMNET_SUCCESS){[worker cancel];}
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^{[worker run];});
        if(events!=VMNET_SUCCESS){close(sockets[1]);reply(nil,nil,VIMNetworkError(@"macOS could not enable network events. Try starting this Mac again."));return;}
        NSMutableDictionary *details=[@{@"Mode":mode,@"BridgeInterface":bridge} mutableCopy];
        const char *host=parameters?xpc_dictionary_get_string(parameters,vmnet_start_address_key):NULL;
        if(host)details[@"HostAddress"]=@(host);
        NSFileHandle *handle=[[NSFileHandle alloc] initWithFileDescriptor:sockets[1] closeOnDealloc:YES];
        reply(handle,details,nil);
    });}
    if(!worker.interface){self.starting=NO;self.worker=nil;reply(nil,nil,VIMNetworkError(@"macOS’s virtual networking service is unavailable."));}
}
- (void)stopWithReply:(void (^)(void))reply {@synchronized(self){self.invalidated=YES;[self.worker cancel];}reply();}
@end
int main(void){@autoreleasepool {
    if(geteuid()!=0)return 77;signal(SIGPIPE,SIG_IGN);budgetLock=[NSObject new];
    NSXPCListener *listener=[[NSXPCListener alloc] initWithMachServiceName:VIM_NETWORK_SERVICE];
    VIMNetworkService *service=[VIMNetworkService new];listener.delegate=service;[listener resume];[[NSRunLoop currentRunLoop] run];
}return 0;}
