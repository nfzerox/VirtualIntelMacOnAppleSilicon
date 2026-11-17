#import "VIMPrivilegedConnection.h"
#include <fcntl.h>
#import "../compat/VIMHelperXPC.h"
#import "../USBHelper/VIMUSBProtocol.h"
#import "../NetworkHelper/VIMNetworkProtocol.h"

@interface VIMPrivilegedConnection ()
@property xpc_connection_t peer;
@property NSString *requirement;
- (void)send:(NSString *)operation payload:(NSDictionary *)payload handle:(NSFileHandle *)handle
        reply:(void (^)(xpc_object_t,NSError *))reply;
@end
@interface VIMPrivilegedProxy : NSObject <VIMUSBHelperProtocol,VIMNetworkHelperProtocol>
@property VIMPrivilegedConnection *connection;
@property(copy) void (^failure)(NSError *);
@end

@implementation VIMPrivilegedConnection
- (instancetype)initWithMachServiceName:(NSString *)name requirement:(NSString *)requirement {
    if((self=[super init])) {
        if(!requirement.length)return nil;
        _requirement=[requirement copy];
        _peer=xpc_connection_create_mach_service(name.UTF8String,dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
        __weak typeof(self) weakSelf=self;
        xpc_connection_set_event_handler(_peer,^(xpc_object_t event){
            if(xpc_get_type(event)==XPC_TYPE_ERROR) {
                VIMPrivilegedConnection *owner=weakSelf;
                if(owner.invalidationHandler)owner.invalidationHandler();
            }
        });
    }return self;
}
- (id)remoteObjectProxyWithErrorHandler:(void (^)(NSError *))handler {
    VIMPrivilegedProxy *proxy=[VIMPrivilegedProxy new];proxy.connection=self;proxy.failure=handler;return proxy;
}
- (void)resume {xpc_connection_resume(self.peer);}
- (void)invalidate {if(_peer)xpc_connection_cancel(_peer);}
- (void)dealloc {if(_peer)xpc_connection_cancel(_peer);}
- (void)send:(NSString *)operation payload:(NSDictionary *)payload handle:(NSFileHandle *)handle reply:(void (^)(xpc_object_t,NSError *))reply {
    xpc_object_t message=xpc_dictionary_create(NULL,NULL,0);
    xpc_dictionary_set_string(message,"operation",operation.UTF8String);VIMHelperSetPayload(message,payload);
    if(handle)xpc_dictionary_set_fd(message,"listener",handle.fileDescriptor);
    NSString *requirement=self.requirement;
    xpc_connection_send_message_with_reply(self.peer,message,dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),^(xpc_object_t response){
        if(!VIMHelperMessageMatches(response,requirement)) {
            reply(NULL,[NSError errorWithDomain:@"VirtualIntelMac.Helper" code:1 userInfo:@{NSLocalizedDescriptionKey:@"The helper connection could not be verified. Try again."}]);return;
        }
        reply(response,VIMHelperReplyError(response));
    });
}
@end

@implementation VIMPrivilegedProxy
- (void)statusWithReply:(void (^)(NSDictionary *))reply {
    [self.connection send:@"status" payload:nil handle:nil reply:^(xpc_object_t response,NSError *error){if(error)self.failure(error);else reply(VIMHelperPayload(response));}];
}
- (void)checkStorageInterface:(NSDictionary *)identity inUserSession:(BOOL)userSession reply:(void (^)(NSDictionary *,NSError *))reply {
    [self.connection send:@"check-storage" payload:@{@"Identity":identity,@"UserSession":@(userSession)} handle:nil
        reply:^(xpc_object_t response,NSError *error){reply(response?VIMHelperPayload(response):nil,error);}];
}
- (void)beginDevice:(NSDictionary *)identity listener:(NSFileHandle *)listener processID:(NSNumber *)processID reply:(void (^)(NSString *,NSError *))reply {
    [self.connection send:@"begin-device" payload:@{@"Identity":identity,@"ProcessID":processID} handle:listener
        reply:^(xpc_object_t response,NSError *error){reply(response?VIMHelperPayload(response)[@"Session"]:nil,error);}];
}
- (void)waitForCapture:(NSString *)sessionID reply:(void (^)(NSError *))reply {
    [self.connection send:@"wait-capture" payload:@{@"Session":sessionID} handle:nil reply:^(xpc_object_t response,NSError *error){reply(error);}];
}
- (void)endSession:(NSString *)sessionID reply:(void (^)(void))reply {
    [self.connection send:@"end-session" payload:@{@"Session":sessionID} handle:nil reply:^(xpc_object_t response,NSError *error){if(error)self.failure(error);else reply();}];
}
- (void)startMode:(NSString *)mode bridge:(NSString *)bridge reply:(void (^)(NSFileHandle *,NSDictionary *,NSError *))reply {
    [self.connection send:@"start-network" payload:@{@"Mode":mode,@"Bridge":bridge} handle:nil reply:^(xpc_object_t response,NSError *error){
        reply(response&&!error?VIMHelperCopyHandle(response,"socket"):nil,response?VIMHelperPayload(response):nil,error);
    }];
}
- (void)stopWithReply:(void (^)(void))reply {
    [self.connection send:@"stop-network" payload:nil handle:nil reply:^(xpc_object_t response,NSError *error){if(error)self.failure(error);else reply();}];
}
@end
